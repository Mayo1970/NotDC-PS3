#pragma once
// G11: the DCNet link (ps3/ps3_dcnet.cpp). Every call is for the SH4 thread; a PPU thread owns the socket.
#include "types.h"

void dcnet_start();       // the modem connected: drops the last call's bytes, the thread dials DCNet
void dcnet_stop();        // hang up: the thread closes the socket
void dcnet_write(u8 b);   // one byte to DCNet; dropped while the ring is full
int dcnet_read();         // one byte from DCNet, or -1
