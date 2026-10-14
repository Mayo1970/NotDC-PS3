#pragma once
// OPTIMIZATION_POTENTIAL P3 (EMULATION -> GD-ROM Async): disc reads on a worker thread. Levels: 0 off, 1 data
// fills, 2 also a CD-DA read-ahead. Only the lock is for any thread; the rest is for the SH4 thread.
#include <stdint.h>   // not types.h: ImgReader.h has its own verify

enum { GDR_DMA, GDR_PIO, GDR_CDDA, GDR_N };   // who read on the SH4 thread, for the [gdr] line

void gda_start();   // after the drive opened: reads the preset, starts the worker
void gda_stop();    // joins the worker; before the drive closes or the process exits
void gda_lock();    // the drive's FILE and buffers
void gda_unlock();

bool gda_async();   // data fills go to the worker
void gda_fill(uint8_t* dst, uint32_t fad, uint32_t count, uint32_t secsz);   // one fill in flight at a time
bool gda_busy();    // that fill has not landed
void gda_wait();    // until it lands

bool gda_cdda(uint8_t* dst, uint32_t fad);             // a sector from the read-ahead; false: read it yourself
void gda_cdda_next(bool playing, uint32_t fad);        // where CD-DA plays next
bool gda_cdda_on();                                    // the read-ahead runs (level 2)
void gda_cdda_seek(uint32_t fad);                      // CD-DA Miss: play starts here, read its window now
void gda_cdda_loop(uint32_t start, uint32_t end);      // + LOOP AHEAD: at end the play turns back to start
void gda_cdda_reset();                                 // P34: a disc swap empties the read-ahead ring
#define GDA_NO_LOOP 0xFFFFFFFFu                        // gda_cdda_loop: the play does not turn back

void gda_count(int who, uint32_t sectors, uint64_t ticks);   // a read the SH4 thread did itself
void gda_stats(double secs);                                  // [gdr] line, once a second
