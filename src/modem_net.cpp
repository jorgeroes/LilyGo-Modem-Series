#include "modem_net.h"
#include <time.h>
#include <ctype.h>

#ifndef AWS_S3_BUCKET
#error "AWS_S3_BUCKET requerido para sync de hora vía HTTPS Date."
#endif
#ifndef AWS_REGION
#define AWS_REGION "us-east-2"
#endif

#define MODEM_RX_PIN          5
#define MODEM_TX_PIN          4
#define BOARD_PWRKEY_PIN      46
#define MODEM_BAUDRATE        115200
#define POWERON_PULSE_MS      100
#define BOOT_WAIT_MS          3000

#define SerialAT              Serial1

namespace ModemNet {

TinyGsm modem(SerialAT);
static TinyGsmClient s_client(modem);

static void dumpAtProbe(const char* label, const char* atCmd, uint32_t timeoutMs);

// Drena bytes pendientes en el puerto AT del módem. Imprescindible cuando
// BAM está lento y waitResponse expira antes de recibir la respuesta:
// esa respuesta tardía queda en el buffer y contamina al siguiente comando
// (el canal AT se desincroniza). Llamar antes de cada comando sensible.
static void drainAt()
{
    uint32_t idleStart = millis();
    while (millis() - idleStart < 50) {
        while (SerialAT.available()) {
            (void)SerialAT.read();
            idleStart = millis();  // reset: sigue llegando data
        }
        delay(1);
    }
}

static void powerOnModem()
{
    pinMode(BOARD_PWRKEY_PIN, OUTPUT);
    digitalWrite(BOARD_PWRKEY_PIN, LOW);
    delay(100);
    digitalWrite(BOARD_PWRKEY_PIN, HIGH);
    delay(POWERON_PULSE_MS);
    digitalWrite(BOARD_PWRKEY_PIN, LOW);
}

bool begin()
{
    SerialAT.begin(MODEM_BAUDRATE, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);

    Serial.println("[modem] Pulsando PWRKEY...");
    powerOnModem();
    delay(BOOT_WAIT_MS);

    Serial.println("[modem] Esperando respuesta AT...");
    int retry = 0;
    while (!modem.testAT(1000)) {
        Serial.print(".");
        if (++retry > 30) {
            Serial.println("\n[modem] Re-pulsando PWRKEY...");
            powerOnModem();
            delay(BOOT_WAIT_MS);
            retry = 0;
        }
    }
    Serial.println();

    Serial.println("[modem] Reset suave (CFUN=6)...");
    modem.sendAT("+CFUN=6");
    modem.waitResponse();
    delay(10000);
    while (!modem.testAT(1000)) { delay(500); }

    Serial.println("[modem] Esperando SIM_READY...");
    SimStatus sim = SIM_ERROR;
    uint32_t simDeadline = millis() + 15000UL;
    while (millis() < simDeadline) {
        sim = modem.getSimStatus();
        if (sim == SIM_READY) break;
        delay(1000);
    }
    if (sim != SIM_READY) {
        Serial.println("[modem][ERR] SIM no llegó a READY.");
        return false;
    }

    Serial.println("[modem] Esperando registro de red (60 s)...");
    RegStatus reg = REG_NO_RESULT;
    uint32_t regDeadline = millis() + 60000UL;
    while (millis() < regDeadline) {
        reg = modem.getRegistrationStatus();
        if (reg == REG_OK_HOME || reg == REG_OK_ROAMING) break;
        if (reg == REG_DENIED) {
            Serial.println("[modem][ERR] REG_DENIED.");
            return false;
        }
        delay(2000);
    }
    if (reg != REG_OK_HOME && reg != REG_OK_ROAMING) {
        Serial.println("[modem][ERR] Timeout esperando registro.");
        return false;
    }

    Serial.printf("[modem] Operador: %s\n", modem.getOperator().c_str());
    return true;
}

bool connectGprs(const char* apn)
{
    Serial.printf("[modem] gprsConnect APN=\"%s\"...\n", apn);
    for (int i = 0; i < 3; i++) {
        if (modem.gprsConnect(apn, "", "")) {
            String ip = modem.getLocalIP();
            Serial.printf("[modem] IP local: %s\n", ip.c_str());
            if (ip.length() > 0 && ip != "0.0.0.0") return true;
        }
        delay(3000);
    }
    Serial.println("[modem][ERR] gprsConnect fallido.");
    return false;
}

bool configurePublicDns()
{
    // BAM (Entel) entrega DNS interno (172.18.150.65/66, visible en
    // +CGCONTRDP). Cualquier DNS público (8.8.8.8, etc.) queda bloqueado
    // por la SIM y CDNSGIP devuelve 0,10 ("DNS server failure").
    //
    // OJO: la config previa de +CDNSCFG queda PERSISTIDA en NVRAM y CFUN=6
    // no la resetea. Por eso forzamos los DNS de Entel explícitamente en
    // cada arranque, aunque ya estén "configurados". Sin esto, una sesión
    // que escribió DNS públicos antes deja al módem incapaz de resolver.
    Serial.println("[modem] configurePublicDns: forzando DNS Entel 172.18.150.65/66");
    String r;
    modem.sendAT("+CDNSCFG=\"172.18.150.65\",\"172.18.150.66\"");
    int8_t rc = modem.waitResponse(5000, r);
    r.trim();
    Serial.printf("[modem] CDNSCFG SET rc=%d resp:\n%s\n", rc, r.c_str());
    if (rc != 1) return false;

    // Gate activo de DNS: el settle estático de 15 s no cubre el peor caso
    // de BAM (el data path puede tardar >20 s en ponerse operativo post-
    // attach). En lugar de esperar un tiempo fijo, sondeamos CDNSGIP hasta
    // que resuelva httpbin.org. Si el módem devuelve "+CDNSGIP: 1,...",
    // el PDP transporta paquetes y Entel DNS responde → estamos listos.

    // Probe inicial: confirmar que el PDP context tiene DNS asignado.
    // Sin esta línea vemos "DNS no activó" sin saber si el problema es
    // (a) PDP sin DNS asignado, (b) DNS asignado pero no alcanzable.
    dumpAtProbe("CGCONTRDP (DNS asignados al PDP, pre-gate)",
                "+CGCONTRDP", 5000);

    Serial.println("[modem] Gate DNS: esperando resolución de httpbin.org (90 s max)...");
    uint32_t gateStart = millis();
    uint32_t deadline  = gateStart + 90000UL;
    int attempts = 0;
    while (millis() < deadline) {
        attempts++;
        // Drena cualquier respuesta tardía antes de emitir el comando, si
        // no el waitResponse siguiente matchea bytes de un turno anterior.
        drainAt();
        String resp;
        modem.sendAT("+CDNSGIP=\"httpbin.org\"");
        int8_t g = modem.waitResponse(30000, resp);
        resp.trim();
        if (g == 1 && resp.indexOf("+CDNSGIP: 1,") >= 0) {
            Serial.printf("[modem] DNS OK en intento %d (%lu ms)\n",
                          attempts, (unsigned long)(millis() - gateStart));
            return true;
        }
        String snippet = resp;
        snippet.replace('\r', ' ');
        snippet.replace('\n', ' ');
        if (snippet.length() > 80) snippet = snippet.substring(0, 80) + "...";
        Serial.printf("  intento %d rc=%d resp=\"%s\"\n",
                      attempts, g, snippet.c_str());
        delay(3000);
    }
    Serial.printf("[modem][ERR] DNS no se activó en 90 s (%d intentos).\n", attempts);
    return false;
}

bool hardRestart()
{
    // Reset "duro" del módem: CFUN=0 apaga radio, CFUN=1 la enciende y
    // fuerza un re-attach limpio con PDP context nuevo. Lo usamos cuando
    // gprsDisconnect + gprsConnect devuelve la MISMA IP — señal de que
    // el contexto PDP no se liberó y BAM nos dejó en una ruta rota.
    Serial.println("[modem] Hard restart: CFUN=0 -> CFUN=1...");
    modem.sendAT("+CFUN=0");
    modem.waitResponse(10000);
    delay(3000);
    modem.sendAT("+CFUN=1");
    modem.waitResponse(10000);
    delay(2000);

    Serial.println("[modem] Esperando AT...");
    uint32_t atDeadline = millis() + 15000UL;
    while (millis() < atDeadline && !modem.testAT(1000)) { delay(500); }

    Serial.println("[modem] Esperando SIM_READY...");
    SimStatus sim = SIM_ERROR;
    uint32_t simDeadline = millis() + 20000UL;
    while (millis() < simDeadline) {
        sim = modem.getSimStatus();
        if (sim == SIM_READY) break;
        delay(1000);
    }
    if (sim != SIM_READY) {
        Serial.println("[modem][ERR] Hard restart: SIM no llegó a READY.");
        return false;
    }

    Serial.println("[modem] Esperando registro de red (60 s)...");
    RegStatus reg = REG_NO_RESULT;
    uint32_t regDeadline = millis() + 60000UL;
    while (millis() < regDeadline) {
        reg = modem.getRegistrationStatus();
        if (reg == REG_OK_HOME || reg == REG_OK_ROAMING) break;
        if (reg == REG_DENIED) {
            Serial.println("[modem][ERR] Hard restart: REG_DENIED.");
            return false;
        }
        delay(2000);
    }
    if (reg != REG_OK_HOME && reg != REG_OK_ROAMING) {
        Serial.println("[modem][ERR] Hard restart: timeout registro.");
        return false;
    }
    Serial.printf("[modem] Hard restart OK. Operador: %s\n",
                  modem.getOperator().c_str());
    return true;
}

// Convierte año/mes/día/hora/min/seg + timezone (cuartos de hora) a unix epoch UTC.
static uint32_t toEpochUtc(int year, int month, int day,
                           int hour, int minute, int second,
                           float timezoneHours)
{
    struct tm t{};
    t.tm_year = year - 1900;
    t.tm_mon  = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min  = minute;
    t.tm_sec  = second;
    t.tm_isdst = 0;

    // Tratar t como UTC y restar el offset reportado por la red.
    setenv("TZ", "UTC0", 1);
    tzset();
    time_t local = mktime(&t);
    if (local == (time_t)-1) return 0;

    return (uint32_t)(local - (time_t)(timezoneHours * 3600.0f));
}

// Parsea cabecera RFC 1123 "Wed, 22 Apr 2026 12:34:56 GMT" → struct tm UTC.
// Tolerante a variantes: salta cualquier prefijo no-numérico antes del día.
static bool parseHttpDate(const char* dateStr, struct tm& out)
{
    const char* p = dateStr;
    while (*p && !isdigit((unsigned char)*p)) p++;

    int day = 0, year = 0, hour = 0, minute = 0, second = 0;
    char monthStr[8] = {0};
    if (sscanf(p, "%d %7s %d %d:%d:%d",
               &day, monthStr, &year, &hour, &minute, &second) != 6) {
        return false;
    }

    static const char* MONTHS[12] = {
        "Jan","Feb","Mar","Apr","May","Jun",
        "Jul","Aug","Sep","Oct","Nov","Dec"
    };
    int month = 0;
    for (int i = 0; i < 12; i++) {
        if (strncmp(monthStr, MONTHS[i], 3) == 0) { month = i + 1; break; }
    }
    if (month == 0) return false;
    if (year < 2024 || year > 2050) return false;

    out.tm_year  = year - 1900;
    out.tm_mon   = month - 1;
    out.tm_mday  = day;
    out.tm_hour  = hour;
    out.tm_min   = minute;
    out.tm_sec   = second;
    out.tm_isdst = 0;
    return true;
}

// Vuelca a Serial el resultado crudo de un comando AT. Sin parsing, solo para
// diagnóstico puntual (DNS, ping, estado del PDP).
static void dumpAtProbe(const char* label, const char* atCmd, uint32_t timeoutMs)
{
    Serial.printf("[probe-at] %s\n  cmd: AT%s\n", label, atCmd);
    String resp;
    modem.sendAT(atCmd);
    int8_t r = modem.waitResponse(timeoutMs, resp);
    resp.trim();
    Serial.printf("  result=%d resp:\n%s\n", r, resp.c_str());
}

// Hace un GET y devuelve status + headers + body. Maneja HTTP y HTTPS.
static int httpGetProbe(const char* url, ServerSSLVersion ver,
                        String& headersOut, String& bodyOut)
{
    headersOut = ""; bodyOut = "";
    modem.sendAT("+HTTPTERM");
    modem.waitResponse(3000);

    if (!modem.https_begin()) {
        Serial.println("  [probe][ERR] HTTPINIT falló");
        return -100;
    }
    modem.sendAT("+CSSLCFG=\"authmode\",0,0");
    modem.waitResponse();
    if (!modem.https_set_url(url, ver, true)) {
        Serial.println("  [probe][ERR] HTTPPARA URL falló");
        modem.https_end();
        return -101;
    }
    modem.https_set_timeout(30, 30, 30);
    int status = modem.https_get();
    headersOut = modem.https_header();
    bodyOut    = modem.https_body();
    modem.https_end();
    return status;
}

bool syncEpoch(uint32_t& epochOut)
{
    // Drena respuestas tardías del gate DNS antes de arrancar el HTTP loop.
    // En BAM lento, un +CDNSGIP puede llegar 20+ s después del send y
    // contamina el canal AT si no lo descartamos aquí.
    drainAt();

    // BAM (Entel) bloquea NTP (UDP/123) y no entrega NITZ. Sincronizamos
    // desde header Date: de una respuesta HTTPS confiable.
    //
    // Decisión: usamos httpbin.org en lugar del bucket S3 como fuente de hora.
    // El probe S3 con GET unsigned devuelve 713 (HTTPS_RECV_RESPONSE_FAIL) en
    // este firmware del A7670G — el problema es específico al endpoint S3 y
    // se investiga por separado en la ruta del PUT real (que usa SigV4 y
    // potencialmente otra negociación TLS). El sync de hora no debería estar
    // acoplado al endpoint que está fallando.
    String headers, body;
    int status;

    // Usamos HTTP plano (no HTTPS) porque:
    //  1. El header Date: es idéntico con o sin TLS — solo necesitamos la hora.
    //  2. El stack TLS del A7670G es flaky en BAM (716/715 transitorios) y
    //     no queremos acoplar el boot a esa variabilidad.
    //  3. HTTP plano fue 100% confiable (200 OK) en runs con la red estable.
    //
    // Aún así, BAM puede dar 713/HTTPINIT-fail en los primeros requests
    // post-attach. Reintentamos hasta 5 veces con 8 s de pausa.
    Serial.println("[modem] Time sync: HTTP GET http://httpbin.org/get");
    bool got = false;
    for (int attempt = 1; attempt <= 5; attempt++) {
        status = httpGetProbe("http://httpbin.org/get", TINYGSM_SSL_AUTO, headers, body);
        Serial.printf("  attempt=%d -> status=%d headers=%u B body=%u B\n",
                      attempt, status, (unsigned)headers.length(),
                      (unsigned)body.length());
        if (status >= 200 && status < 400 && headers.length() > 0) {
            got = true;
            break;
        }
        if (attempt < 5) {
            Serial.println("  retry en 8 s...");
            delay(8000);
        }
    }
    if (!got) {
        Serial.println("[modem][ERR] httpbin HTTP falló tras 5 intentos.");
        return false;
    }

    // Diagnóstico no bloqueante: ¿llega TLS al bucket S3?
    {
        char s3Url[160];
        snprintf(s3Url, sizeof(s3Url), "https://%s.s3.%s.amazonaws.com/",
                 AWS_S3_BUCKET, AWS_REGION);
        String s3H, s3B;
        int s3St = httpGetProbe(s3Url, TINYGSM_SSL_TLS1_2, s3H, s3B);
        Serial.printf("[modem] Probe S3 (diag, no bloquea): status=%d headers=%u B body=%u B\n",
                      s3St, (unsigned)s3H.length(), (unsigned)s3B.length());
    }

    // Diagnóstico TLS genérico: HTTPS a httpbin (no-AWS). Discrimina entre
    //   - 200 aquí + 713 a S3 → falla S3-específica (cipher/DNS bucket).
    //   - 713 aquí + 713 a S3 → stack HTTPS del módem caído entero.
    //   - 715/error aquí     → TLS handshake roto (ruta BAM o firmware).
    {
        String hH, hB;
        int hSt = httpGetProbe("https://httpbin.org/get",
                               TINYGSM_SSL_TLS1_2, hH, hB);
        Serial.printf("[modem] Probe HTTPS httpbin (diag): status=%d headers=%u B body=%u B\n",
                      hSt, (unsigned)hH.length(), (unsigned)hB.length());
    }

    // Buscar "Date:" case-insensitive (algunos proxies lo emiten en minúsculas).
    String hdrLower = headers;
    hdrLower.toLowerCase();
    int idx = hdrLower.indexOf("\ndate:");
    if (idx < 0 && hdrLower.startsWith("date:")) idx = -1; // empieza al inicio
    int valueStart;
    if (idx >= 0) {
        valueStart = idx + 6;
    } else if (hdrLower.startsWith("date:")) {
        valueStart = 5;
    } else {
        Serial.println("[modem][ERR] Header Date: ausente.");
        return false;
    }
    while (valueStart < (int)headers.length() &&
           (headers[valueStart] == ' ' || headers[valueStart] == '\t')) {
        valueStart++;
    }
    int eol = headers.indexOf('\r', valueStart);
    if (eol < 0) eol = headers.indexOf('\n', valueStart);
    if (eol < 0) eol = headers.length();
    String dateLine = headers.substring(valueStart, eol);

    struct tm t{};
    if (!parseHttpDate(dateLine.c_str(), t)) {
        Serial.printf("[modem][ERR] Parse Date falló: \"%s\"\n", dateLine.c_str());
        return false;
    }

    setenv("TZ", "UTC0", 1);
    tzset();
    time_t epoch = mktime(&t);
    if (epoch == (time_t)-1 || epoch < 1700000000L) {
        Serial.println("[modem][ERR] mktime epoch inválido.");
        return false;
    }
    epochOut = (uint32_t)epoch;
    Serial.printf("[modem] Hora HTTPS: \"%s\" -> epoch=%u\n",
                  dateLine.c_str(), epochOut);
    return true;
}

TinyGsmClient& rawClient() { return s_client; }

}
