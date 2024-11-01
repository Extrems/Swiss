/* 
 * Copyright (c) 2024, Extrems <extrems@extremscorner.org>
 * 
 * This file is part of Swiss.
 * 
 * Swiss is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * Swiss is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * with Swiss.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <ppu_intrinsics.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "bba.h"
#include "common.h"
#include "dolphin/exi.h"
#include "dolphin/os.h"
#include "emulator_eth.h"
#include "interrupt.h"
#include "w5500.h"

#define exi_channel ((*VAR_EXI_SLOT & 0x30) >> 4)
#define exi_device  ((*VAR_EXI_SLOT & 0xC0) >> 6)
#define exi_regs    (*(volatile uint32_t **)VAR_EXI2_REGS)

static struct {
	bool sendok;
	bool interrupt;
	uint16_t wr;
	uint16_t rd;
	bba_page_t (*page)[8];
	struct {
		void *data;
		size_t size;
		bba_callback callback;
	} output;
} w5500;

static void exi_clear_interrupts(bool exi, bool tc, bool ext)
{
	exi_regs[0] = (exi_regs[0] & (0x3FFF & ~0x80A)) | (ext << 11) | (tc << 3) | (exi << 1);
}

static void exi_select(void)
{
	exi_regs[0] = (exi_regs[0] & 0x405) | ((1 << EXI_DEVICE_0) << 7) | (EXI_SPEED_32MHZ << 4);
}

static void exi_deselect(void)
{
	exi_regs[0] &= 0x405;
}

static void exi_imm_write(uint32_t data, uint32_t len)
{
	exi_regs[4] = data;
	exi_regs[3] = ((len - 1) << 4) | (EXI_WRITE << 2) | 0b01;
	while (exi_regs[3] & 0b01);
}

static uint32_t exi_imm_read(uint32_t len)
{
	exi_regs[3] = ((len - 1) << 4) | (EXI_READ << 2) | 0b01;
	while (exi_regs[3] & 0b01);
	return exi_regs[4] >> ((4 - len) * 8);
}

static uint32_t exi_imm_read_write(uint32_t data, uint32_t len)
{
	exi_regs[4] = data;
	exi_regs[3] = ((len - 1) << 4) | (EXI_READ_WRITE << 2) | 0b01;
	while (exi_regs[3] & 0b01);
	return exi_regs[4] >> ((4 - len) * 8);
}

static void exi_dma_write(const void *buf, uint32_t len, bool sync)
{
	exi_regs[1] = (uint32_t)buf;
	exi_regs[2] = OSRoundUp32B(len);
	exi_regs[3] = (EXI_WRITE << 2) | 0b11;
	while (sync && (exi_regs[3] & 0b01));
}

static void exi_dma_read(void *buf, uint32_t len, bool sync)
{
	exi_regs[1] = (uint32_t)buf;
	exi_regs[2] = OSRoundUp32B(len);
	exi_regs[3] = (EXI_READ << 2) | 0b11;
	while (sync && (exi_regs[3] & 0b01));
}

static void w5500_read_cmd(uint32_t cmd, void *buf, uint32_t len)
{
	cmd &= ~W5500_RWB;
	cmd  = (cmd << 16) | (cmd >> 16);

	exi_select();
	exi_imm_write(cmd, 3);
	exi_dma_read(buf, len, true);
	exi_deselect();
}

static void w5500_write_cmd(uint32_t cmd, const void *buf, uint32_t len)
{
	cmd |= W5500_RWB;
	cmd  = (cmd << 16) | (cmd >> 16);

	exi_select();
	exi_imm_write(cmd, 3);
	exi_dma_write(buf, len, true);
	exi_deselect();
}

static uint8_t w5500_read_reg8(W5500Reg addr)
{
	uint8_t data;
	uint32_t cmd = W5500_OM(1) | addr;

	cmd = (cmd << 16) | (cmd >> 16);

	exi_select();
	data = exi_imm_read_write(cmd, 4);
	exi_deselect();

	return data;
}

static void w5500_write_reg8(W5500Reg addr, uint8_t data)
{
	uint32_t cmd = W5500_RWB | W5500_OM(1) | addr;

	cmd = (cmd << 16) | (cmd >> 16);

	exi_select();
	exi_imm_read_write(cmd | data, 4);
	exi_deselect();
}

static uint16_t w5500_read_reg16(W5500Reg16 addr)
{
	uint16_t data;
	uint32_t cmd = W5500_OM(2) | addr;

	cmd = (cmd << 16) | (cmd >> 16);

	exi_select();
	exi_imm_write(cmd, 3);
	data = exi_imm_read(2);
	exi_deselect();

	return data;
}

static void w5500_write_reg16(W5500Reg16 addr, uint16_t data)
{
	uint32_t cmd = W5500_RWB | W5500_OM(2) | addr;

	cmd = (cmd << 16) | (cmd >> 16);

	exi_select();
	exi_imm_write(cmd, 3);
	exi_imm_write(data << 16, 2);
	exi_deselect();
}

static void w5500_interrupt(void)
{
	w5500_write_reg8(W5500_SIMR, 0);
	uint8_t ir = w5500_read_reg8(W5500_S0_IR);

	if (ir & W5500_Sn_IR_SENDOK) {
		w5500_write_reg8(W5500_S0_IR, W5500_Sn_IR_SENDOK);
		w5500.sendok = true;
	}

	if (ir & W5500_Sn_IR_RECV) {
		w5500_write_reg8(W5500_S0_IR, W5500_Sn_IR_RECV);

		uint16_t rd = w5500.rd;
		uint16_t rs = w5500_read_reg16(W5500_RXBUF_S(0, rd));
		w5500.rd = rd + rs;

		rd += sizeof(rs);
		rs -= sizeof(rs);

		DCInvalidateRange(__builtin_assume_aligned(w5500.page, 32), rs);
		w5500_read_cmd(W5500_RXBUF_S(0, rd), w5500.page, rs);
		eth_mac_receive(w5500.page, rs);

		w5500_write_reg16(W5500_S0_RX_RD, w5500.rd);
		w5500_write_reg8(W5500_S0_CR, W5500_Sn_CR_RECV);
	}

	w5500_write_reg8(W5500_SIMR, W5500_SIMR_S(0));
}

static void exi_callback()
{
	if (EXILock(exi_channel, exi_device, exi_callback)) {
		if (w5500.interrupt) {
			w5500_interrupt();
			w5500.interrupt = false;
		}

		if (w5500.output.callback && w5500.sendok) {
			bba_output(w5500.output.data, w5500.output.size);
			w5500.output.callback();
			w5500.output.callback = NULL;
		}

		EXIUnlock(exi_channel);
	}
}

static void exi_interrupt_handler(OSInterrupt interrupt, OSContext *context)
{
	exi_clear_interrupts(true, false, false);
	w5500.interrupt = true;
	exi_callback();
}

static void debug_interrupt_handler(OSInterrupt interrupt, OSContext *context)
{
	PI[0] = 1 << 12;
	w5500.interrupt = true;
	exi_callback();
}

void bba_output(const void *data, size_t size)
{
	DCStoreRange(__builtin_assume_aligned(data, 32), size);
	w5500_write_cmd(W5500_TXBUF_S(0, w5500.wr), data, size);
	w5500.wr += size;

	w5500_write_reg16(W5500_S0_TX_WR, w5500.wr);
	w5500_write_reg8(W5500_S0_CR, W5500_Sn_CR_SEND);
	w5500.sendok = false;
}

void bba_output_async(const void *data, size_t size, bba_callback callback)
{
	w5500.output.data = (void *)data;
	w5500.output.size = size;
	w5500.output.callback = callback;

	exi_callback();
}

void bba_init(void **arenaLo, void **arenaHi)
{
	w5500.sendok = w5500_read_reg16(W5500_S0_TX_FSR) == W5500_TX_BUFSIZE;

	w5500.wr = w5500_read_reg16(W5500_S0_TX_WR);
	w5500.rd = w5500_read_reg16(W5500_S0_RX_RD);

	*arenaHi -= sizeof(*w5500.page); w5500.page = *arenaHi;

	if (exi_channel < EXI_CHANNEL_2) {
		OSInterrupt interrupt = OS_INTERRUPT_EXI_0_EXI + (3 * exi_channel);
		set_interrupt_handler(interrupt, exi_interrupt_handler);
		unmask_interrupts(OS_INTERRUPTMASK(interrupt) & (OS_INTERRUPTMASK_EXI_0_EXI | OS_INTERRUPTMASK_EXI_1_EXI));
	} else {
		set_interrupt_handler(OS_INTERRUPT_PI_DEBUG, debug_interrupt_handler);
		unmask_interrupts(OS_INTERRUPTMASK_PI_DEBUG);
	}
}