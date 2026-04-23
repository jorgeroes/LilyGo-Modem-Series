#include "aws_uploader.h"

#include <SD.h>
#include <time.h>
#include <mbedtls/sha256.h>
#include <mbedtls/md.h>
#include <esp_heap_caps.h>

#include "modem_net.h"

#ifndef AWS_ACCESS_KEY_ID
#error "AWS_ACCESS_KEY_ID no definido. Crear secrets.ini gitignored."
#endif
#ifndef AWS_SECRET_ACCESS_KEY
#error "AWS_SECRET_ACCESS_KEY no definido."
#endif
#ifndef AWS_REGION
#define AWS_REGION "us-east-2"
#endif
#ifndef AWS_S3_BUCKET
#error "AWS_S3_BUCKET no definido."
#endif

#define S3_SERVICE       "s3"
// AT+HTTPDATA del A7670 acepta hasta ~100 KB por request. Si el CSV crece más
// (sesión > ~45 min entre PUTs), este uploader fallará y habrá que migrar a
// chunks o a cliente TLS streaming. Documentado en AWS_ESP32_SIM_GUIDE.md §3.
#define MAX_PUT_BYTES    (96UL * 1024UL)

namespace AwsUploader {

static const char HEXDIGITS[] = "0123456789abcdef";

static void toHex(const uint8_t* in, size_t len, char* out)
{
    for (size_t i = 0; i < len; i++) {
        out[2*i]     = HEXDIGITS[(in[i] >> 4) & 0xF];
        out[2*i + 1] = HEXDIGITS[in[i] & 0xF];
    }
    out[2*len] = 0;
}

static void sha256(const uint8_t* data, size_t len, uint8_t out[32])
{
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, data, len);
    mbedtls_sha256_finish(&ctx, out);
    mbedtls_sha256_free(&ctx);
}

static void hmacSha256(const uint8_t* key, size_t keyLen,
                       const uint8_t* msg, size_t msgLen,
                       uint8_t out[32])
{
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, info, 1);
    mbedtls_md_hmac_starts(&ctx, key, keyLen);
    mbedtls_md_hmac_update(&ctx, msg, msgLen);
    mbedtls_md_hmac_finish(&ctx, out);
    mbedtls_md_free(&ctx);
}

static void formatAmzDate(uint32_t epoch, char dateStamp[9], char amzDate[17])
{
    time_t t = (time_t)epoch;
    struct tm tm_;
    gmtime_r(&t, &tm_);
    strftime(dateStamp, 9,  "%Y%m%d",          &tm_);
    strftime(amzDate,   17, "%Y%m%dT%H%M%SZ",  &tm_);
}

bool putObjectFromSd(const char* sdPath, const char* key, uint32_t epochNow)
{
    File f = SD.open(sdPath, FILE_READ);
    if (!f) {
        Serial.printf("[aws][ERR] No se pudo abrir %s\n", sdPath);
        return false;
    }
    size_t fileSize = f.size();
    if (fileSize == 0) {
        Serial.printf("[aws] %s vacío, se omite PUT\n", sdPath);
        f.close();
        return true;
    }
    if (fileSize > MAX_PUT_BYTES) {
        Serial.printf("[aws][ERR] %s pesa %u B (>%lu B). Migrar a chunks.\n",
                      sdPath, (unsigned)fileSize, MAX_PUT_BYTES);
        f.close();
        return false;
    }

    // Cargar el CSV en RAM (PSRAM si disponible). Es el límite real del MVP.
    uint8_t* buf = (uint8_t*)heap_caps_malloc(fileSize, MALLOC_CAP_SPIRAM);
    if (!buf) buf = (uint8_t*)malloc(fileSize);
    if (!buf) {
        Serial.printf("[aws][ERR] malloc(%u) falló\n", (unsigned)fileSize);
        f.close();
        return false;
    }
    // El driver SD del ESP32 puede devolver menos bytes de los pedidos en
    // un solo read() (cache interno, boundaries de cluster). Leemos en
    // bucle hasta completar o detectar EOF real.
    size_t totalRead = 0;
    while (totalRead < fileSize) {
        int n = f.read(buf + totalRead, fileSize - totalRead);
        if (n <= 0) break;  // EOF o error
        totalRead += n;
    }
    f.close();
    if (totalRead != fileSize) {
        Serial.printf("[aws][ERR] read incompleto (%u/%u)\n",
                      (unsigned)totalRead, (unsigned)fileSize);
        free(buf);
        return false;
    }

    uint8_t payloadHash[32];
    sha256(buf, fileSize, payloadHash);
    char payloadHashHex[65];
    toHex(payloadHash, 32, payloadHashHex);

    char dateStamp[9], amzDate[17];
    formatAmzDate(epochNow, dateStamp, amzDate);

    String host = String(AWS_S3_BUCKET) + ".s3." + AWS_REGION + ".amazonaws.com";
    String canonicalUri = String("/") + key;

    String canonicalRequest;
    canonicalRequest.reserve(512);
    canonicalRequest += "PUT\n";
    canonicalRequest += canonicalUri;
    canonicalRequest += "\n\n";
    canonicalRequest += "host:" + host + "\n";
    canonicalRequest += "x-amz-content-sha256:" + String(payloadHashHex) + "\n";
    canonicalRequest += "x-amz-date:" + String(amzDate) + "\n";
    canonicalRequest += "\n";
    canonicalRequest += "host;x-amz-content-sha256;x-amz-date\n";
    canonicalRequest += payloadHashHex;

    uint8_t crHash[32];
    sha256((const uint8_t*)canonicalRequest.c_str(), canonicalRequest.length(), crHash);
    char crHashHex[65];
    toHex(crHash, 32, crHashHex);

    String credentialScope = String(dateStamp) + "/" + AWS_REGION + "/" + S3_SERVICE + "/aws4_request";
    String stringToSign = String("AWS4-HMAC-SHA256\n") + amzDate + "\n" + credentialScope + "\n" + crHashHex;

    uint8_t kSigning[32];
    {
        String prefix = String("AWS4") + AWS_SECRET_ACCESS_KEY;
        hmacSha256((const uint8_t*)prefix.c_str(), prefix.length(),
                   (const uint8_t*)dateStamp, strlen(dateStamp), kSigning);
        hmacSha256(kSigning, 32, (const uint8_t*)AWS_REGION, strlen(AWS_REGION), kSigning);
        hmacSha256(kSigning, 32, (const uint8_t*)S3_SERVICE, strlen(S3_SERVICE), kSigning);
        hmacSha256(kSigning, 32, (const uint8_t*)"aws4_request", 12, kSigning);
    }

    uint8_t sig[32];
    hmacSha256(kSigning, 32,
               (const uint8_t*)stringToSign.c_str(), stringToSign.length(),
               sig);
    char sigHex[65];
    toHex(sig, 32, sigHex);

    String authorization = String("AWS4-HMAC-SHA256 Credential=") + AWS_ACCESS_KEY_ID + "/" +
                           credentialScope +
                           ", SignedHeaders=host;x-amz-content-sha256;x-amz-date" +
                           ", Signature=" + sigHex;

    String url = String("https://") + host + canonicalUri;

    auto& modem = ModemNet::modem;
    if (!modem.https_begin()) {
        Serial.println("[aws][ERR] https_begin");
        free(buf);
        return false;
    }
    // AWS S3 exige TLS 1.2. AUTO negocia mal en el A7670 → status 713.
    if (!modem.https_set_url(url, TINYGSM_SSL_TLS1_2, true)) {
        Serial.println("[aws][ERR] https_set_url");
        modem.https_end();
        free(buf);
        return false;
    }
    modem.https_set_timeout(30, 30, 30);
    modem.https_set_content_type("text/csv");
    modem.https_add_header("x-amz-date",            amzDate);
    modem.https_add_header("x-amz-content-sha256",  payloadHashHex);
    modem.https_add_header("Authorization",         authorization.c_str());

    Serial.printf("[aws] PUT %s (%u B)\n", url.c_str(), (unsigned)fileSize);
    int status = modem.https_put((const char*)buf, fileSize);
    free(buf);

    if (status >= 200 && status < 300) {
        Serial.printf("[aws] OK status=%d\n", status);
        modem.https_end();
        return true;
    }

    Serial.printf("[aws][ERR] status=%d, body:\n%s\n",
                  status, modem.https_body().c_str());
    modem.https_end();
    return false;
}

}
