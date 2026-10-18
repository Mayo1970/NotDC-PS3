/*
	Originally based on nullDC: nullExtDev/modem.cpp

	Created on: Sep 9, 2018

	Copyright 2018 skmp, flyinghead

	This file is part of reicast.

    reicast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    reicast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with reicast.  If not, see <https://www.gnu.org/licenses/>.

	Changed for NotDC (PS3 port) on 2026-10-18.
 */
// G11 stage 1, row NETWORK > Modem: Flycast's modem (core/hw/modem/modem.cpp of df2bdfc9b, no V.42) on area 0
// 0x600000; OFF keeps the old stub. Flycast's sh4_sched callback runs here on the cycles of each MediumUpdate.
#include "plugins/plugin_header.h"
#include "ps3/ps3_dcnet.h"
#include "ps3/ps3_presets.h"
#include "modem_regs.h"
#include <string.h>

#define MODEM_COUNTRY_RES 0
#define MODEM_MAKER_ROCKWELL 1
#define MODEM_DEVICE_TYPE_336K 0

static const u32 MODEM_ID[2] =
{
	MODEM_COUNTRY_RES,
	(MODEM_MAKER_ROCKWELL<<4) | (MODEM_DEVICE_TYPE_336K),
};

static modemreg_t modem_regs;

// One byte more than the DSP RAM: Flycast's odd-address word access reads past 0xFFF.
static u8 dspram[0x1000 + 1];
static_assert(sizeof(regs_write_mask) == sizeof(modem_regs), "regs_write_mask");
static_assert(sizeof(por_dspram) == sizeof(dspram) - 1, "por_dspram");

// The row's value at Init: 0 = the old stub.
static bool s_on;
static HollyRaiseInterruptFP* s_raise;
static HollyCancelInterruptFP* s_cancel;
// Flycast's sh4_sched_now64() and its one request: s_cps cycles make a second (SH4_CLOCK_EFF, as the time bases).
static u64 s_now;
static u32 s_cps;
static bool s_sched_on;
static s32 s_sched_left;

enum ModemStates
{
	MS_INVALID,				//needs reset
	MS_RESET,				//reset is low
	MS_RESETING,			//reset is hi
	MS_ST_CONTROLER,		//Controller self test
	MS_ST_DSP,				//DSP self test
	MS_END_DSP,				//DSP self test end
	MS_NORMAL,	 			//Normal operation

};
static ModemStates state = MS_INVALID;

enum ConnectState
{
	DISCONNECTED,
	DIALING,
	RINGING,
	HANDSHAKING,
	PRE_CONNECTED,
	CONNECTED,
};
static ConnectState connect_state = DISCONNECTED;

static void schedule_callback(int ms);

static void update_interrupt()
{
	modem_regs.reg1e.RDBIA = modem_regs.reg1e.RDBIE && modem_regs.reg1e.RDBF;
	modem_regs.reg1e.TDBIA = modem_regs.reg1e.TDBIE && modem_regs.reg1e.TDBE;

	if (modem_regs.reg1f.NCIA || modem_regs.reg1f.NSIA || modem_regs.reg1e.RDBIA || modem_regs.reg1e.TDBIA)
		s_raise(holly_EXP_8BIT);
	else
		s_cancel(holly_EXP_8BIT);
}

static u32 get_masked_status(u32 reg)
{
	u8 int_mask = dspram[regs_int_mask_addr[reg]];
	return int_mask & modem_regs.ptr[reg];
}

#define SET_STATUS_BIT(reg, bit, value) do {		\
	if ((bit) != (value))							\
	{												\
		if (!modem_regs.reg1f.NSIE)					\
		{											\
			bit = (value);							\
		}											\
		else										\
		{											\
			u8 before = get_masked_status(reg);		\
			bit = (value);							\
			if (before != get_masked_status(reg))	\
				modem_regs.reg1f.NSIA = 1;			\
		}											\
	}												\
} while (false);

// Flycast's MEDAL/MEDAM word, little-endian in two bytes.
static u32 meda() { return modem_regs.reg18 | (modem_regs.reg19 << 8); }
static void set_meda(u32 v) { modem_regs.reg18 = (u8)v; modem_regs.reg19 = (u8)(v >> 8); }

static void ControllerTestEnd();
static void DSPTestStart();
static void DSPTestEnd();

static u64 last_dial_time;
static bool data_sent;

static int modem_sched_func(int jitter)
{
	int callback_cycles = 0;

	switch (state)
	{
	case MS_ST_CONTROLER:
		ControllerTestEnd();
		break;
	case MS_ST_DSP:
		DSPTestStart();
		break;
	case MS_END_DSP:
		DSPTestEnd();
		break;
	case MS_NORMAL:
		modem_regs.reg1f.NEWC = 0;		// Not needed when state is CONNECTED but who cares

		switch (connect_state)
		{
		case DIALING:
			if (last_dial_time != 0 && s_now - last_dial_time >= (u64)(s_cps + (u32)jitter))
			{
				printf("[modem] ringing\n");
				connect_state = RINGING;
				schedule_callback(100);
			}
			else
			{
				last_dial_time = s_now;

				modem_regs.reg1e.TDBE = 1;
				schedule_callback(1000);	// To switch to Ringing state
			}
			break;
		case RINGING:
			last_dial_time = 0;
			modem_regs.reg1f.NEWS = 1;
			if (!modem_regs.reg09.DATA)
			{
				SET_STATUS_BIT(0x0f, modem_regs.reg0f.RI, 1);
				SET_STATUS_BIT(0x0b, modem_regs.reg0b.ATV25, 1);
			}
			break;
		case HANDSHAKING:
			if (modem_regs.reg12 == 0xAA)
			{
				// V8 AUTO mode
				dspram[0x302] |= 1 << 3;				// ANSam detected
			}
			modem_regs.reg1f.NEWS = 1;
			SET_STATUS_BIT(0x0f, modem_regs.reg0f.RI, 0);
			SET_STATUS_BIT(0x0b, modem_regs.reg0b.ATV25, 0);

			callback_cycles = s_cps / 1000 * 500;		// 500 ms
			connect_state = PRE_CONNECTED;

			break;

		case PRE_CONNECTED:
			if (modem_regs.reg03.RLSDE)
				SET_STATUS_BIT(0x0f, modem_regs.reg0f.RLSD, 1);
			if (modem_regs.reg12 == 0xAA)
			{
				// V8 AUTO mode
				dspram[0x302] |= 1 << 4;				// protocol octet received
				dspram[0x302] = (dspram[0x302] & 0x1f) | (dspram[0x304] & 0xe0);	// Received Call Function
				dspram[0x301] |= 1 << 4;				// JM detected
				dspram[0x303] |= 0xE0;					// Received protocol bits (?)
				dspram[0x2e3] = 5;						// Symbol rate 3429
				dspram[0x2e4] = 0xe;					// V.34 Receiver Speed 33.6
				dspram[0x2e5] = 0xe;					// V.34 Transmitter Speed 33.6
				dspram[0x239] = 12;						// RTD 0 @ 3429 sym rate
				if (modem_regs.reg08.ASYN)
				{
					modem_regs.reg12 = 0xce;		// CONF V34 - K56flex
					modem_regs.reg0e.SPEED = 0x10;	// 33.6k
				}
				else
				{
					// Force the driver to ASYN=1 so it sends raw data
					modem_regs.reg12 = 0xa1;		// CONF V23 75 TX/1200 RX
					modem_regs.reg0e.SPEED = 0x02;	// 1.2k
				}
				if (modem_regs.reg1f.NSIE)
				{
					// CONF
					if (dspram[regs_int_mask_addr[0x12]] & (1 << 7))
						modem_regs.reg1f.NSIA = 1;
					// SPEED
					if (dspram[regs_int_mask_addr[0x0e]] & 0x1f)
						modem_regs.reg1f.NSIA = 1;
				}
				modem_regs.reg09.DATA = 1;
				modem_regs.reg15.AUTO = 0;
			}
			modem_regs.reg14 = 0x00;			// ABCODE: no error
			if (modem_regs.reg1f.NSIE)
			{
				// ABCODE
				if (dspram[regs_int_mask_addr[0x014]])
					modem_regs.reg1f.NSIA = 1;
			}
			modem_regs.reg1f.NEWS = 1;
			SET_STATUS_BIT(0x0f, modem_regs.reg0f.DSR, 1);
			if (modem_regs.reg02.v0.RTSDE)
				SET_STATUS_BIT(0x0f, modem_regs.reg0f.RTSDT, 1);

			// Energy detected. Required for games to detect the connection
			SET_STATUS_BIT(0x0f, modem_regs.reg0f.FED, 1);

			// V.34 Remote Modem Data Rate Capability
			dspram[0x208] = 0xff;	// 2.4 - 19.2 kpbs supported
			dspram[0x209] = 0xbf;	// 21.6 - 33.6 kpbs supported, asymmetric supported

			printf("[modem] connected: CONF %02X ASYN %d SPEED %d\n", modem_regs.reg12, modem_regs.reg08.ASYN,
			       modem_regs.reg0e.SPEED);
			dcnet_start();
			connect_state = CONNECTED;
			callback_cycles = s_cps / 1000000 * 238;	// 238 us
			data_sent = false;

			break;

		case CONNECTED:
			// Critical: lower stalls some sockets (Sonic Adventure 2, Samba de Amigo PAL browsers). 143 us per byte is
			// 56K; 57600 at 10 bits would be 174, 38400 260.
			callback_cycles = s_cps / 1000000 * 143;
			modem_regs.reg1e.TDBE = 1;

			// Let WinCE send data first to avoid choking it
			if (!modem_regs.reg1e.RDBF && data_sent)
			{
				int c = dcnet_read();
				if (c >= 0)
				{
					modem_regs.reg00 = c & 0xFF;
					modem_regs.reg1e.RDBF = 1;
					if (modem_regs.reg04.FIFOEN)
						SET_STATUS_BIT(0x0c, modem_regs.reg0c.RXFNE, 1);
					SET_STATUS_BIT(0x01, modem_regs.reg01.RXHF, 1);
				}
			}

			break;

		default:
			break;
		}
		break;

	default:
		break;
	}
	update_interrupt();

	return callback_cycles;
}

static void schedule_callback(int ms)
{
	s_sched_on = true;
	s_sched_left = (s32)(s_cps / 1000 * ms);
}

#define SetReg16(rh,rl,v) {modem_regs.ptr[rh]=(v)>>8;modem_regs.ptr[rl]=(v)&0xFF; }

static void NormalDefaultRegs()
{
	// Default values for normal state
	memset(&modem_regs, 0, sizeof(modem_regs));
	memcpy(dspram, por_dspram, sizeof(por_dspram));
	modem_regs.reg05.CEQ = 1;
	modem_regs.reg12 = 0x76;	// CONF: V.32 bis TCM 14400
	modem_regs.reg09.DATA = 1;
	modem_regs.reg09.DTMF = 1;
	modem_regs.reg15.HWRWK = 1;
	modem_regs.reg07.RDLE = 1;
	modem_regs.reg15.RDWK = 1;
	modem_regs.reg03.RLSDE = 1;
	modem_regs.reg02.TDE = 1;
	modem_regs.reg13.TLVL = 0x9;

	modem_regs.reg1e.TDBE = 1;
	connect_state = DISCONNECTED;
	last_dial_time = 0;
}
static void DSPTestEnd()
{
	state = MS_NORMAL;

	NormalDefaultRegs();
}
static void DSPTestStart()
{
	state = MS_END_DSP;

	modem_regs.reg1e.TDBE = 1;
	SetReg16(0x1B,0x1A,0xF083);	// EC checksum
	SetReg16(0x19,0x18,0x46EE); // Multiplier checksum
	SetReg16(0x17,0x16,0x00FA); // RAM checksum
	SetReg16(0x15,0x14,0x0A09); // ROM checksum
	SetReg16(0x13,0x12,0x3730); // Part number
	SetReg16(0x11,0x00,0x2041); // Revision level

	schedule_callback(50);
}
static void ControllerTestEnd()
{
	state = MS_ST_DSP;

	schedule_callback(50);
}

//End the reset and start internal tests
static void ControllerTestStart()
{
	//Set Self test values :)
	state = MS_ST_CONTROLER;
	//k, lets set values

	//1E:3 -> set
	modem_regs.reg1e.TDBE = 1;

	// RAM1 0xEA3C or 0x451, RAM2 0x5536 or 0x49A5, ROM1 0x5F4C, ROM2 0x3835 or 0x3836,
	// timer/ROM/RAM 0x801 or 0xDE00, part number 0x3730 or 0x3731, revision level 0x4241.
	SetReg16(0x1D, 0x1C, 0xEA3C);
	SetReg16(0x1B, 0x1A, 0x5536);
	SetReg16(0x19, 0x18, 0x5F4C);
	SetReg16(0x17, 0x16, 0x3835);
	SetReg16(0x15, 0x14, 0x801);
	SetReg16(0x13, 0x12, 0x3730);
	SetReg16(0x11, 0x0, 0x4241);

	ControllerTestEnd();
}

static void modem_reset(u32 v)
{
	if (v == 0)
	{
		memset(&modem_regs, 0, sizeof(modem_regs));
		state = MS_RESET;
		dcnet_stop();
	}
	else
	{
		if (state == MS_RESET)
		{
			memset(&modem_regs, 0, sizeof(modem_regs));
			state = MS_RESETING;
			ControllerTestStart();
			printf("[modem] reset\n");
		}
		modem_regs.ptr[0x20] = v;
	}
}

static void check_start_handshake()
{
	if (modem_regs.reg09.DTR
			&& (modem_regs.reg09.DATA || modem_regs.reg15.AUTO)
			&& connect_state == RINGING)
	{
		printf("[modem] handshake\n");
		connect_state = HANDSHAKING;
		schedule_callback(1);
	}
}

static bool word_dspram_write;
static bool module_download;
static u32 reg1b_save;
static u8 download_crc;
static u8 s_warned;   // bit n: warning n printed once

static void warn_once(u32 n, const char* text)
{
	if (s_warned & (1u << n))
		return;
	s_warned |= 1u << n;
	printf("[modem] %s\n", text);
}

static void ModemNormalWrite(u32 reg, u32 data)
{
	u32 old = modem_regs.ptr[reg];
	modem_regs.ptr[reg] = (old & ~regs_write_mask[reg]) | (data & regs_write_mask[reg]);

	switch(reg)
	{
	case 0x02:
		modem_regs.reg0f.RTSDT = modem_regs.reg02.v0.RTSDE && connect_state == CONNECTED;
		break;

	case 0x06:
		if (modem_regs.reg06.PEN)
			warn_once(0, "parity not supported");
		if (modem_regs.reg06.HDLC)
			warn_once(1, "HDLC mode not supported");
		break;

	case 0x09:
		check_start_handshake();	// DTR and DATA
		break;

	case 0x10:	// TBUFFER
		if (module_download)
		{
			download_crc = (download_crc << 1) + ((download_crc & 0x80) >> 7) + (data & 0xFF);
		}
		else if (connect_state == DISCONNECTED || connect_state == DIALING)
		{
			if (connect_state == DISCONNECTED)
			{
				printf("[modem] dialing\n");
				connect_state = DIALING;
			}
			schedule_callback(100);
		}
		else if (connect_state == CONNECTED && modem_regs.reg08.RTS)
		{
			data_sent = true;
			dcnet_write(data);
			modem_regs.reg1e.TDBE = 0;
		}
		break;

	case 0x14:	// ABCODE
		if (data == 0x4f || data == 0x5f)
		{
			reg1b_save = modem_regs.ptr[0x1b];
			modem_regs.ptr[0x1b] = data;
			module_download = true;
			download_crc = 0;
		}
		else if (data == 0 && module_download)
		{
			// If module download is in progress, this signals the end of it
			modem_regs.reg17 = 0xFF;
			modem_regs.reg16 = download_crc;
			// Restore reg 1b
			modem_regs.ptr[0x1b] = reg1b_save;
			module_download = false;
		}
		break;

	case 0x15:
		check_start_handshake();	// AUTO
		break;

		//Data Write Regs, for transfers to DSP
	case 0x18:	// MEDAL
		break;

	case 0x19:	// MEDAM
		word_dspram_write = true;
		break;

	case 0x1a:
		if (connect_state == CONNECTED && modem_regs.reg1a.SCIBE)
			warn_once(2, "unexpected state: connected and SCIBE==1");
		break;

		//Address low
	case 0x1C:	// MEADDL
		break;

	case 0x1D:	// MEADDH
		if (modem_regs.reg1c_1d.MEMW && !(old & (1 << 5)))
		{
			word_dspram_write = false;
		}
		if (modem_regs.reg1c_1d.MEACC)
		{
			modem_regs.reg1c_1d.MEACC = 0;
			modem_regs.reg1f.NEWS = 1;
			u32 dspram_addr = modem_regs.reg1c_1d.MEMADD_l | (modem_regs.reg1c_1d.MEMADD_h << 8);
			// Flycast's even addresses go through a host u16: the same little-endian bytes as its odd ones.
			if (modem_regs.reg1c_1d.MEMW)
			{
				if (word_dspram_write)
				{
					dspram[dspram_addr] = meda() & 0xFF;
					dspram[dspram_addr + 1] = (meda() >> 8) & 0xFF;
				}
				else
					dspram[dspram_addr] = meda() & 0xFF;
			}
			else
				set_meda(dspram[dspram_addr] | (dspram[dspram_addr + 1] << 8));
		}
		break;

	case 0x1F:
		if (!modem_regs.reg1f.NCIE)
			modem_regs.reg1f.NCIA = 0;
		if (modem_regs.reg1f.NEWC)
		{
			if(modem_regs.reg1a.SFRES)
			{
				modem_regs.reg1a.SFRES = 0;
				modem_reset(0);
				modem_reset(1);
				modem_regs.reg1f.NEWC = 1;
				modem_regs.ptr[0x20] = 0;
			}
			else
			{
				modem_regs.reg1f.NEWC = 0;	// accept the settings
				if (modem_regs.reg1f.NCIE)
					modem_regs.reg1f.NCIA = 1;
			}
		}
		// Don't allow NEWS to be set if 0
		if ((old & (1 << 3)) == 0)
			modem_regs.reg1f.NEWS = 0;
		if (!modem_regs.reg1f.NEWS)
		{
			modem_regs.reg1f.NSIA = 0;
		}
		break;

	default:
		break;
	}
	update_interrupt();
}

static u32 ModemReadMem_A0_006(u32 addr, u32 size)
{
	u32 reg = (addr & 0x7FF) >> 2;

	if (reg < 0x100)
		return MODEM_ID[reg & 1];

	reg -= 0x100;
	if (reg < 0x21)
	{
		switch (state)
		{
		case MS_NORMAL:
			{
				// Dial tone is detected if TONEA, TONEB and TONEC are set
				SET_STATUS_BIT(0x0f, modem_regs.reg0f.CTS, modem_regs.reg08.RTS && connect_state == CONNECTED);
				SET_STATUS_BIT(0x0b, modem_regs.reg0b.TONEA, connect_state == DISCONNECTED);
				SET_STATUS_BIT(0x0b, modem_regs.reg0b.TONEB, connect_state == DISCONNECTED);
				SET_STATUS_BIT(0x0b, modem_regs.reg0b.TONEC, connect_state == DISCONNECTED);
				// FIXME This should be reset if transmit buffer is full
				if (modem_regs.reg04.FIFOEN || module_download)
					SET_STATUS_BIT(0x0d, modem_regs.reg0d.TXFNF, 1);
				u8 data = modem_regs.ptr[reg];
				if (reg == 0x00)	// RBUFFER
				{
					modem_regs.reg1e.RDBF = 0;
					SET_STATUS_BIT(0x0c, modem_regs.reg0c.RXFNE, 0);
					SET_STATUS_BIT(0x01, modem_regs.reg01.RXHF, 0);
					update_interrupt();
				}

				return data;
			}

		case MS_ST_CONTROLER:
		case MS_ST_DSP:
		case MS_END_DSP:
			if (reg == 0x10)
				// don't reset TBDE to help kos modem self test
				return 0;
			else
				return modem_regs.ptr[reg];

		case MS_RESETING:
			return 0; //still reset

		default:
			return 0;
		}
	}

	return 0;
}

static void ModemWriteMem_A0_006(u32 addr, u32 data, u32 size)
{
	u32 reg = (addr & 0x7FF) >> 2;
	if (reg < 0x100)
		return;

	reg -= 0x100;
	if (reg < 0x20)
	{
		if (state == MS_NORMAL)
			ModemNormalWrite(reg,data);
		return;
	}
	if (reg == 0x20)
	{
		//Hard reset
		modem_reset(data);
		return;
	}
}

//006* , on area0
u32 FASTCALL libExtDevice_ReadMem_A0_006(u32 addr,u32 size)
{
	return s_on ? ModemReadMem_A0_006(addr, size) : 0;
}
void FASTCALL libExtDevice_WriteMem_A0_006(u32 addr,u32 data,u32 size)
{
	if (s_on)
		ModemWriteMem_A0_006(addr, data, size);
}
//010* , on area0
u32 FASTCALL libExtDevice_ReadMem_A0_010(u32 addr,u32 size)
{
	return 0;
}
void FASTCALL libExtDevice_WriteMem_A0_010(u32 addr,u32 data,u32 size)
{

}
//Area 5
u32 FASTCALL libExtDevice_ReadMem_A5(u32 addr,u32 size)
{
	return 0;
}
void FASTCALL libExtDevice_WriteMem_A5(u32 addr,u32 data,u32 size)
{
}

// Flycast sh4_sched: a callback that is due runs once, late by its jitter; its result asks for the next one.
void FASTCALL libExtDevice_Update(u32 cycles)
{
	if (!s_on)
		return;
	s_now += cycles;
	if (!s_sched_on)
		return;
	s_sched_left -= (s32)cycles;
	if (s_sched_left > 0)
		return;
	const int jitter = -s_sched_left;
	s_sched_on = false;
	const int next = modem_sched_func(jitter);
	if (next > 0)
	{
		s_sched_on = true;
		s_sched_left = next > jitter ? next - jitter : 0;
	}
}

s32 FASTCALL libExtDevice_Load()
{
	return rv_ok;
}

void FASTCALL libExtDevice_Unload()
{

}

// Flycast ModemReset: only the link goes; the chip keeps its state until the game resets it.
void FASTCALL libExtDevice_Reset(bool Manual)
{
	if (s_on)
		dcnet_stop();
}

s32 FASTCALL libExtDevice_Init(ext_device_init_params* p)
{
	s_raise = p->RaiseInterrupt;
	s_cancel = p->CancelInterrupt;
	s_on = g_modem_preset != 0;
	s_cps = SH4_CLOCK_EFF;
	printf("[modem] Modem %s\n", s_on ? "DCNET: Flycast's modem, no V.42, dials dcnet.flyca.st" : "OFF: no modem");
	return rv_ok;
}


void FASTCALL libExtDevice_Term()
{
	if (s_on)
		dcnet_stop();
}
