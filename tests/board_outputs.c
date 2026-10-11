/*
 * Hardware-free test of the outputs read from the VBIOS's object table
 * (rdn_output_detect() in rdn_modeset.c): on the VBIOS of the 7570, the
 * wiring the driver has built in must come back from the table, and with no
 * display on any line the detection must fail cleanly after trying every
 * candidate (the two built in, the DisplayPort lines as TMDS, the board's).
 *
 * Built for x86 and for big-endian PowerPC; both must print the same.
 *
 * Usage: board_outputs <vbios.rom>
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../hw/rdn_card.h"
#include "../hw/rdn_mode.h"
#include "../hw/rdn_i2c.h"

#define MMIO_WORDS	(0x20000 / 4)

static uint32_t regs[MMIO_WORDS];
static uint64_t clock_ms;

static uint32_t os_mmio_read32(void *c, uint32_t off)
{
	(void)c;
	/* Open-drain lines read back high: nothing pulls them down. */
	return off / 4 < MMIO_WORDS ? regs[off / 4] : 0xffffffffu;
}

static void os_mmio_write32(void *c, uint32_t off, uint32_t v)
{
	(void)c;
	if (off / 4 < MMIO_WORDS)
		regs[off / 4] = v;
}

static void os_delay_us(void *c, uint32_t usec)
{
	(void)c;
	(void)usec;
}

static uint64_t os_time_ms(void *c)
{
	(void)c;
	return clock_ms++;
}

static void *os_alloc(void *c, size_t size)
{
	(void)c;
	return calloc(1, size);
}

static void os_free(void *c, void *ptr)
{
	(void)c;
	free(ptr);
}

static void os_log(void *c, enum rdn_log_level level, const char *fmt,
		   va_list ap)
{
	(void)c;
	(void)level;
	/* Only the lines of the output list; the rest is not under test. */
	if (strncmp(fmt, "board:", 6) && strstr(fmt, "no EDID") == NULL &&
	    strstr(fmt, "EDID found") == NULL)
		return;
	vprintf(fmt, ap);
	printf("\n");
}

int main(int argc, char **argv)
{
	static uint8_t bios[0x20000];
	uint8_t edid[RDN_EDID_MAX_SIZE];
	static struct rdn_card card;
	struct rdn_os os;
	FILE *f;
	int i, r, dvi = 0, dp = 0;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <vbios.rom>\n", argv[0]);
		return 2;
	}
	f = fopen(argv[1], "rb");
	if (!f || fread(bios, 1, sizeof(bios), f) < 0x200) {
		fprintf(stderr, "cannot read %s\n", argv[1]);
		return 2;
	}
	fclose(f);

	memset(&os, 0, sizeof(os));
	os.mmio_read32 = os_mmio_read32;
	os.mmio_write32 = os_mmio_write32;
	os.delay_us = os_delay_us;
	os.time_ms = os_time_ms;
	os.alloc = os_alloc;
	os.free = os_free;
	os.log = os_log;
	if (rdn_card_init(&card, &os, bios)) {
		fprintf(stderr, "VBIOS not usable\n");
		return 2;
	}

	r = rdn_output_detect(&card, edid);
	printf("detect with no display: %d, %d board outputs\n", r,
	       card.num_board);
	if (r > 0) {
		printf("FAIL: found an EDID where there is none\n");
		return 1;
	}
	for (i = 0; i < card.num_board; i++) {
		const struct rdn_output *o = &card.board[i];

		printf("board output %d: %s, connector 0x%02x, UNIPHY%d%s, "
		       "DIG %d, DDC 0x%02x, HPD %d%s\n", i, o->name,
		       o->connector_id, o->transmitter_sel,
		       o->linkb ? " link B" : "", o->dig_encoder, o->i2c_id,
		       o->hpd, o->displayport ? ", DisplayPort" : "");
		/* What the driver has built in for the 7570. */
		if (!o->displayport && o->transmitter_sel == 0 &&
		    !o->linkb && o->dig_encoder == 0 && o->i2c_id == 0x93 &&
		    o->hpd == 0)
			dvi = 1;
		if (o->displayport && o->transmitter_sel == 2 && !o->linkb &&
		    o->dig_encoder == 4 && o->i2c_id == 0x92 && o->hpd == 3)
			dp = 1;
	}
	if (!dvi || !dp) {
		printf("FAIL: the built-in wiring (DVI-I %d, DisplayPort %d) "
		       "is not what the object table says\n", dvi, dp);
		return 1;
	}
	rdn_card_fini(&card);
	printf("PASS: the object table gives the built-in DVI-I and "
	       "DisplayPort wiring\n");
	return 0;
}
