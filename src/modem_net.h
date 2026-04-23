#pragma once

#include <Arduino.h>
#include <TinyGsmClient.h>

namespace ModemNet {

extern TinyGsm modem;

bool begin();
bool connectGprs(const char* apn);
bool configurePublicDns();
bool hardRestart();
bool syncEpoch(uint32_t& epochOut);
TinyGsmClient& rawClient();

}
