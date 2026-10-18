#pragma once
// modem_check: Flycast core/network/netservice.h, for the reference build.
#include "types.h"
namespace net { namespace modbba {
bool start();
void stop();
void writeModem(u8 b);
int readModem();
int modemAvailable();
} }
