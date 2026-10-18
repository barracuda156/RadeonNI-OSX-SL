/*
 * Test of the UVD bring-up (hw/rdn_uvd.c) against a model of the block:
 * the PLL handshake, the VCPU coming out of reset, the memory window, the
 * ring and indirect buffer packets (read byte-swapped when the swap register
 * says so) and the VCPU's fence write. It checks the order of what the code
 * does, not what the real card does; nothing here has run on a card.
 *
 * Run natively and under qemu-ppc: the output must be identical.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hw/rdn_uvd.h"
#include "../hw/rdn_uvd_reg.h"

#define APERTURE	(8u << 20)
#define VRAM_BASE	0xF00000000ull
#define FW_SIZE		4096u
#define RING_BYTES	4096u
#define VCPU_OFFSET	0x100000u
#define RING_OFFSET	0x10000u

static uint32_t regs[0x20000 / 4];
static uint8_t *mem;		/* the aperture, bytes as the card stores them */
static unsigned writes;
static int failures;
static uint32_t ring_read;	/* the model's read pointer, dwords */

static uint32_t bswap32(uint32_t v)
{
	return (v << 24) | ((v & 0xff00) << 8) | ((v >> 8) & 0xff00) | (v >> 24);
}

static uint32_t mem_le32(uint32_t offset)
{
	return mem[offset] | (mem[offset + 1] << 8) | (mem[offset + 2] << 16) |
	       ((uint32_t)mem[offset + 3] << 24);
}

/* A word of the ring or an IB as the VCPU sees it. */
static uint32_t cmd_word(uint32_t offset)
{
	uint32_t v = mem_le32(offset);

	return regs[UVD_LMI_SWAP_CNTL / 4] == 0xa ? bswap32(v) : v;
}

static void reg_write(uint32_t reg, uint32_t val);

static void run_packets(uint32_t offset, uint32_t words)
{
	uint32_t i = 0;

	while (i < words) {
		uint32_t w = cmd_word(offset + 4 * i++);
		uint32_t n, reg;

		if ((w >> 30) != 0) {
			printf("FAIL: packet type %u at word %u\n", w >> 30, i - 1);
			failures++;
			continue;
		}
		reg = (w & 0xffff) << 2;
		for (n = ((w >> 16) & 0x3fff) + 1; n && i < words; n--, reg += 4)
			reg_write(reg, cmd_word(offset + 4 * i++));
	}
}

static void reg_write(uint32_t reg, uint32_t val)
{
	writes++;
	regs[reg / 4] = val;
	switch (reg) {
	case UVD_RBC_RB_WPTR: {
		uint32_t ring_words = RING_BYTES / 4, base = regs[UVD_RBC_RB_BASE / 4];
		uint32_t rptr = ring_read;

		while (rptr != (val & (ring_words - 1))) {
			/* one packet at a time: ring words are walked as packets */
			uint32_t w = cmd_word(base + 4 * rptr);
			uint32_t n = ((w >> 16) & 0x3fff) + 1, r = (w & 0xffff) << 2, k;

			rptr = (rptr + 1) & (ring_words - 1);
			for (k = 0; k < n; k++, r += 4) {
				uint32_t v = cmd_word(base + 4 * rptr);

				rptr = (rptr + 1) & (ring_words - 1);
				reg_write(r, v);
			}
		}
		ring_read = rptr;
		regs[UVD_RBC_RB_RPTR / 4] = rptr;
		break;
	}
	case UVD_RBC_IB_SIZE:
		run_packets(regs[UVD_RBC_IB_BASE / 4], val);
		break;
	case UVD_GPCOM_VCPU_CMD:
		if (val == 0) {	/* write the fence */
			uint32_t off = regs[UVD_GPCOM_VCPU_DATA0 / 4], v = regs[UVD_GPCOM_VCPU_DATA1 / 4];

			mem[off] = v; mem[off + 1] = v >> 8; mem[off + 2] = v >> 16; mem[off + 3] = v >> 24;
		}
		break;
	case UVD_RBC_RB_RPTR:
		ring_read = val;
		break;
	}
}

static uint32_t reg_read(uint32_t reg)
{
	uint32_t v = regs[reg / 4];

	switch (reg) {
	case CG_UPLL_FUNC_CNTL:
		/* the PLL acknowledges a control request */
		if (v & UPLL_CTLREQ_MASK)
			v |= UPLL_CTLACK_MASK | UPLL_CTLACK2_MASK;
		return v;
	case UVD_STATUS:
		/* the VCPU runs once it is out of reset, clocked, with the UMC unstalled */
		return (!regs[UVD_SOFT_RESET / 4] && (regs[UVD_VCPU_CNTL / 4] & (1 << 9)) &&
			!(regs[UVD_LMI_CTRL2 / 4] & (1 << 8))) ? 2 : 0;
	}
	return v;
}

static uint32_t os_read(void *c, uint32_t off) { (void)c; return reg_read(off); }
static void os_write(void *c, uint32_t off, uint32_t v) { (void)c; reg_write(off, v); }
static void os_delay(void *c, uint32_t us) { (void)c; (void)us; }
static uint64_t os_time(void *c) { static uint64_t t; (void)c; return t += 1; }
static void os_log(void *c, enum rdn_log_level l, const char *f, va_list ap)
{
	(void)c;
	if (l == RDN_LOG_ERROR) {
		printf("log: ");
		vprintf(f, ap);
		printf("\n");
	}
}

static void check(int ok, const char *what)
{
	printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		failures++;
}

int main(void)
{
	struct rdn_os os;
	struct rdn_card card;
	struct rdn_accel accel;
	struct rdn_uvd uvd;
	uint8_t fw[FW_SIZE];
	uint32_t fb, vd, dd, seq, i, ib_off = 0x200000;
	int r;

	/* the divider search against Linux's algorithm (run in Python on the same inputs) */
	check(!rdn_uvd_calc_dividers(2700, 53300, 40000, &fb, &vd, &dd) &&
	      fb == 970296 && vd == 3 && dd == 4, "dividers for 533/400 MHz at 27 MHz: fb 970296, 3, 4");
	check(!rdn_uvd_calc_dividers(2700, 10000, 10000, &fb, &vd, &dd) &&
	      fb == 849540 && vd == 14 && dd == 14, "dividers for 100/100 MHz: fb 849540, 14, 14");
	check(rdn_uvd_calc_dividers(0, 10000, 10000, &fb, &vd, &dd) < 0, "no reference clock: error");

	mem = calloc(1, APERTURE);
	memset(&os, 0, sizeof(os));
	os.mmio_read32 = os_read;
	os.mmio_write32 = os_write;
	os.delay_us = os_delay;
	os.time_ms = os_time;
	os.log = os_log;
	memset(&card, 0, sizeof(card));
	card.os = &os;
	memset(&accel, 0, sizeof(accel));
	accel.card = &card;
	accel.aperture = (volatile uint32_t *)mem;
	accel.aperture_size = APERTURE;
	accel.vram_base = VRAM_BASE;
	for (i = 0; i < FW_SIZE; i++)
		fw[i] = (uint8_t)(i * 7 + 1);
	regs[UVD_RBC_RB_BASE / 4] = 0;

	r = rdn_uvd_init(&uvd, &accel, 2700, fw, FW_SIZE, RDN_UVD_DEFAULT_HANDLES,
			 VCPU_OFFSET, RING_OFFSET, RING_BYTES);
	check(r == 0, "init succeeds (VCPU boots, ring test passes)");
	check(uvd.ready, "engine ready");
	check(memcmp(mem + VCPU_OFFSET, fw, FW_SIZE) == 0, "firmware copied byte for byte");
	check(mem[VCPU_OFFSET + FW_SIZE] == 0 && mem[VCPU_OFFSET + uvd.vcpu_bytes - 1] == 0,
	      "the rest of the VCPU region is zero");
	check(regs[UVD_VCPU_CHIP_ID_REG / 4] == 0x01000011, "chip id of Turks");
	check(regs[UVD_VCPU_CACHE_OFFSET0 / 4] == (uint32_t)((VRAM_BASE + VCPU_OFFSET) >> 3),
	      "cache 0 at the firmware");
	check(regs[UVD_VCPU_CACHE_SIZE0 / 4] == (4096 + 4096) / 8, "cache 0 size: page aligned firmware + 4");
	check(regs[UVD_VCPU_CACHE_OFFSET1 / 4] == regs[UVD_VCPU_CACHE_OFFSET0 / 4] + regs[UVD_VCPU_CACHE_SIZE0 / 4],
	      "heap follows the firmware");
	check(regs[UVD_LMI_ADDR_EXT / 4] == ((0u << 12) | 0u) && regs[UVD_LMI_EXT40_ADDR / 4] == (0xFu | (0x7u << 16) | (1u << 31)),
	      "address bits 28-31 and 32-39 (ring's segment last)");
	check(regs[UVD_LMI_SWAP_CNTL / 4] == (RDN_BIG_ENDIAN ? 0xa : 0),
	      "ring and IB swapped (0xa) on a big-endian host only");
	check((regs[CG_UPLL_FUNC_CNTL / 4] & (UPLL_BYPASS_EN_MASK | UPLL_SLEEP_MASK)) ==
	      (UPLL_BYPASS_EN_MASK | UPLL_SLEEP_MASK), "clocks down again after init (PLL bypassed and asleep)");

	/* two indirect buffers, each writes UVD_CONTEXT_ID; the fence follows each */
	r = rdn_uvd_wake(&uvd);
	check(r == 0 && (regs[CG_UPLL_FUNC_CNTL / 4] & UPLL_BYPASS_EN_MASK) == 0 &&
	      regs[CG_UPLL_FUNC_CNTL_3 / 4] == 970296, "wake: PLL out of bypass, fb divider 970296");
	for (i = 0; i < 2; i++) {
		uint32_t w0 = RDN_UVD_PACKET0(UVD_CONTEXT_ID, 0), w1 = 0x1000 + i;

		/* the IB's words are in the ring's byte order */
		if (uvd.swapped) {
			w0 = bswap32(w0);
			w1 = bswap32(w1);
		}
		rdn_vram_write32(&accel, ib_off, w0);
		rdn_vram_write32(&accel, ib_off + 4, w1);
		r = rdn_uvd_ib_submit(&uvd, VRAM_BASE + ib_off, 2, &seq);
		check(r == 0 && seq == i + 1, "IB submitted, fence numbered in turn");
		check(rdn_uvd_fence_wait(&uvd, seq, 10) == 0, "fence reached");
		check(regs[UVD_CONTEXT_ID / 4] == 0x1000 + i, "the IB ran");
	}
	check(!rdn_uvd_fence_done(&uvd, 3), "a fence not yet emitted is not done");
	check(rdn_uvd_ib_submit(&uvd, 0x100000000ull + ib_off, 2, &seq) < 0, "an IB outside the ring's 4 GB segment is refused");
	rdn_uvd_fini(&uvd);
	check(!uvd.ready && regs[UVD_VCPU_CNTL / 4] == 0, "stopped: VCPU clock off");

	free(mem);
	printf("%s\n", failures ? "FAIL" : "PASS");
	return failures != 0;
}
