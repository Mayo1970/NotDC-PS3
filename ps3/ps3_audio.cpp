// AICA audio sink (ps3_audio.h API) on a PSL1GHT 48 kHz float32 audio port.
// Port setup and feeder thread follow ioQuake3-PS3 ps3_snd.c and xashPS3 s_ps3.c.
#include "types.h"
#include "ps3/ps3_audio.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <audio/audio.h>
#include <sys/thread.h>
#include <sys/event_queue.h>
#include <sysmodule/sysmodule.h>
#include "ps3_timebase.h"

#define OUT_RATE    48000
#define IN_RATE     44100
#define CHANNELS    2
#define BLOCK       256     // frames per port block, fixed by the audio port
#define RING_FRAMES 8192    // ~170 ms at 48 kHz; power of two

extern u64 SndWaitTicks;

static audioPortConfig port_cfg;
static u32 port_num;
static sys_event_queue_t evq;
static sys_ipc_key_t evq_key;
static sys_ppu_thread_t feeder_tid;
static volatile int feeder_quit;
static bool running;
static volatile int aica_ready;

// Single producer (SH4 thread) / single consumer (feeder thread); indices only grow.
static f32 ring[RING_FRAMES * CHANNELS] __attribute__((aligned(16)));
static volatile u32 ring_wr;
static volatile u32 ring_rd;

// 44.1 -> 48 kHz linear resampler state; phase counts in 1/OUT_RATE steps.
static s16 prev_l, prev_r;
static u32 phase;

// Port blocks queued after the one playing (~16 ms). A slot the feeder misses would
// replay 8-block-old audio (a 42.7 ms echo), so every slot up to the lead is written.
#define LEAD_BLOCKS 3

static volatile u32 st_in, st_blocks, st_silent, st_dropped, st_late;

static void feeder_thread(void* arg)
{
	volatile u64* read_index = (volatile u64*)(uintptr_t)port_cfg.readIndex;
	f32* data = (f32*)(uintptr_t)port_cfg.audioDataStart;
	u32 num_blocks = (u32)port_cfg.numBlocks;
	// audioDataStart alignment is not guaranteed, so stage then memcpy (xashPS3).
	static f32 staging[BLOCK * CHANNELS] __attribute__((aligned(16)));
	u32 wr_slot = (u32)((*read_index + 1) % num_blocks);

	while (!feeder_quit)
	{
		sys_event_t ev;
		if (sysEventQueueReceive(evq, &ev, 20 * 1000) != 0)
			continue;
		if (feeder_quit)
			break;

		u32 ri = (u32)(*read_index % num_blocks);
		u32 ahead = (wr_slot + num_blocks - ri - 1) % num_blocks;
		// The reader passed wr_slot: restart right after the playing block.
		if (ahead > LEAD_BLOCKS)
		{
			wr_slot = (ri + 1) % num_blocks;
			ahead = 0;
			st_late++;
		}

		while (ahead < LEAD_BLOCKS)
		{
			// Whole blocks only: a partial block would add a gap inside every block when starved.
			u32 rd = ring_rd;
			if (ring_wr - rd >= BLOCK)
			{
				__sync_synchronize();
				for (u32 i = 0; i < BLOCK; i++)
				{
					u32 s = ((rd + i) & (RING_FRAMES - 1)) * CHANNELS;
					staging[i * 2 + 0] = ring[s + 0];
					staging[i * 2 + 1] = ring[s + 1];
				}
				__sync_synchronize();
				ring_rd = rd + BLOCK;
				st_blocks++;
			}
			else if (ahead == 0)
			{
				// Starved with nothing queued: silence beats replaying a stale slot.
				memset(staging, 0, sizeof(staging));
				st_silent++;
			}
			else
				break;

			memcpy(data + wr_slot * CHANNELS * BLOCK, staging, sizeof(staging));
			wr_slot = (wr_slot + 1) % num_blocks;
			ahead++;
		}
	}
	sysThreadExit(0);
}

void ps3_audio_init()
{
	if (running)
		return;

	s32 ret = sysModuleLoad(SYSMODULE_AUDIO);
	if (ret != 0 && ret != (s32)SYSMODULE_ERR_DUPLICATE)
	{
		printf("[aud] sysModuleLoad(AUDIO) failed: 0x%08X\n", (unsigned)ret);
		return;
	}
	if ((ret = audioInit()) != 0)
	{
		printf("[aud] audioInit failed: 0x%08X\n", (unsigned)ret);
		return;
	}

	audioPortParam params;
	memset(&params, 0, sizeof(params));
	params.numChannels = AUDIO_PORT_2CH;
	params.numBlocks = AUDIO_BLOCK_8;
	params.attrib = AUDIO_PORT_INITLEVEL;
	params.level = 1.0f;

	if ((ret = audioPortOpen(&params, &port_num)) != 0)
	{
		printf("[aud] audioPortOpen failed: 0x%08X\n", (unsigned)ret);
		audioQuit();
		return;
	}
	if ((ret = audioGetPortConfig(port_num, &port_cfg)) != 0 ||
	    !port_cfg.readIndex || !port_cfg.audioDataStart || !port_cfg.numBlocks)
	{
		printf("[aud] audioGetPortConfig failed: 0x%08X\n", (unsigned)ret);
		audioPortClose(port_num);
		audioQuit();
		return;
	}
	if ((ret = audioCreateNotifyEventQueue(&evq, &evq_key)) != 0)
	{
		printf("[aud] audioCreateNotifyEventQueue failed: 0x%08X\n", (unsigned)ret);
		audioPortClose(port_num);
		audioQuit();
		return;
	}
	if ((ret = audioSetNotifyEventQueue(evq_key)) != 0)
	{
		printf("[aud] audioSetNotifyEventQueue failed: 0x%08X\n", (unsigned)ret);
		sysEventQueueDestroy(evq, 0);
		audioPortClose(port_num);
		audioQuit();
		return;
	}
	sysEventQueueDrain(evq);

	if ((ret = audioPortStart(port_num)) != 0)
	{
		printf("[aud] audioPortStart failed: 0x%08X\n", (unsigned)ret);
		audioRemoveNotifyEventQueue(evq_key);
		sysEventQueueDestroy(evq, 0);
		audioPortClose(port_num);
		audioQuit();
		return;
	}

	ring_wr = ring_rd = 0;
	prev_l = prev_r = 0;
	phase = 0;
	feeder_quit = 0;

	// Priority 100, above the main thread's 1001: each block has a ~5.3 ms deadline.
	ret = sysThreadCreate(&feeder_tid, feeder_thread, NULL, 100, 0x4000,
	                      THREAD_JOINABLE, (char*)"nulldc_audio");
	if (ret != 0)
	{
		printf("[aud] feeder thread create failed: 0x%08X\n", (unsigned)ret);
		audioPortStop(port_num);
		audioRemoveNotifyEventQueue(evq_key);
		sysEventQueueDestroy(evq, 0);
		audioPortClose(port_num);
		audioQuit();
		return;
	}

	running = true;
	printf("[aud] port %u open: %d Hz float32, %u ch, %u blocks, AudioBuffers=%u\n",
	       (unsigned)port_num, OUT_RATE, (unsigned)port_cfg.channelCount,
	       (unsigned)port_cfg.numBlocks, (unsigned)settings.emulator.AudioBuffers);
}

void ps3_audio_term()
{
	aica_ready = 0;
	if (!running)
		return;
	running = false;

	u64 rv;
	feeder_quit = 1;
	sysThreadJoin(feeder_tid, &rv);
	audioPortStop(port_num);
	audioRemoveNotifyEventQueue(evq_key);
	sysEventQueueDestroy(evq, 0);
	audioPortClose(port_num);
	audioQuit();
	printf("[aud] shut down\n");
}

void ps3_audio_aica_ready()
{
	printf("[aud] AICA ready, sink enabled\n");
	aica_ready = 1;
}

// Returns false when the ring stays full (AudioBuffers 0: at once; else after 100 ms).
static bool ring_wait_space()
{
	if (ring_wr - ring_rd < RING_FRAMES)
		return true;
	if (settings.emulator.AudioBuffers == 0)
		return false;

	u64 t0 = gettime();
	for (int spins = 0; ring_wr - ring_rd >= RING_FRAMES && spins < 2000; spins++)
		usleep(50);
	SndWaitTicks += gettime() - t0;
	return ring_wr - ring_rd < RING_FRAMES;
}

// One 44.1 kHz sample from AICA_Sample(); emits the 48 kHz frames that fall before it.
void ps3_audio_push_sample(s16 l, s16 r)
{
	if (!aica_ready || !running)
		return;
	st_in++;

	const f32 scale = 1.0f / (32768.0f * OUT_RATE);
	while (phase < OUT_RATE)
	{
		if (!ring_wait_space())
		{
			st_dropped++;
			phase += IN_RATE;
			continue;
		}
		u32 w = ring_wr;
		u32 s = (w & (RING_FRAMES - 1)) * CHANNELS;
		ring[s + 0] = (f32)(prev_l * (s32)(OUT_RATE - phase) + l * (s32)phase) * scale;
		ring[s + 1] = (f32)(prev_r * (s32)(OUT_RATE - phase) + r * (s32)phase) * scale;
		__sync_synchronize();
		ring_wr = w + 1;
		phase += IN_RATE;
	}
	phase -= OUT_RATE;
	prev_l = l;
	prev_r = r;
}

// OPTIMIZATION_PLAN 5.13 A2: the SPU writes the frames and ring_wr itself (spu/spe_aica_snd.h); no push runs.
void ps3_audio_ring_info(u32* ring_ea, u32* frames, u32* wr_ea, u32* rd_ea, u32* wr_now)
{
	*ring_ea = (u32)(uintptr_t)ring;
	*frames = RING_FRAMES;
	*wr_ea = (u32)(uintptr_t)&ring_wr;
	*rd_ea = (u32)(uintptr_t)&ring_rd;
	*wr_now = ring_wr;
}

// 5.13 A2 pacing of the SH4 thread, as ring_wait_space: waits until frame `end` fits. Returns the ticks waited.
u64 ps3_audio_spu_wait(u32 end)
{
	if (!running || !aica_ready || settings.emulator.AudioBuffers == 0 || end - ring_rd <= RING_FRAMES)
		return 0;
	const u64 t0 = gettime();
	for (int spins = 0; end - ring_rd > RING_FRAMES && spins < 2000 && aica_ready; spins++)
		usleep(50);
	const u64 t = gettime() - t0;
	SndWaitTicks += t;
	return t;
}

// Called once per second from the monitor thread.
void ps3_audio_stats()
{
	if (!running)
		return;
	static u32 l_in, l_out, l_blocks, l_silent, l_dropped, l_late;
	u32 in = st_in, out = ring_wr, blocks = st_blocks, silent = st_silent, dropped = st_dropped;
	u32 late = st_late;
	printf("[aud] in=%u/s out=%u/s blocks=%u silent=%u dropped=%u late=%u fill=%u\n",
	       in - l_in, out - l_out, blocks - l_blocks, silent - l_silent,
	       dropped - l_dropped, late - l_late, ring_wr - ring_rd);
	l_in = in; l_out = out; l_blocks = blocks; l_silent = silent; l_dropped = dropped;
	l_late = late;
}
