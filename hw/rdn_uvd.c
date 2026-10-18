/*
 * UVD bring-up for Turks. Ported from the MIT-licensed Linux radeon files
 * uvd_v1_0.c, uvd_v2_2.c, radeon_uvd.c and evergreen.c; the register
 * sequences are theirs, the structure is ours.
 *
 * Copyright 2010 Jerome Glisse
 * Copyright 2013 Advanced Micro Devices, Inc.
 * Copyright (c) 2026 kouta-kun and Claude
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Christian König, Jerome Glisse
 */

#include <errno.h>
#include <string.h>

#include "rdn_uvd.h"
#include "rdn_uvd_reg.h"

/* Linux's usec_timeout. */
#define RDN_UVD_USEC_TIMEOUT	100000

/* VCPU_CHIP_ID for Turks (uvd_v2_2.c). */
#define RDN_UVD_CHIP_ID_TURKS	0x01000011

#define PAGE_UP(x)		(((x) + 4095u) & ~4095u)

static uint32_t rd(struct rdn_card *card, uint32_t reg)
{
	return rdn_rreg(card, reg);
}

static void wr(struct rdn_card *card, uint32_t reg, uint32_t val)
{
	rdn_wreg(card, reg, val);
}

/* Linux's WREG32_P: keep the bits of `mask`, set the others from `val`. */
static void wr_p(struct rdn_card *card, uint32_t reg, uint32_t val, uint32_t mask)
{
	uint32_t tmp = rd(card, reg);

	tmp &= mask;
	tmp |= val & ~mask;
	wr(card, reg, tmp);
}

static void mdelay(struct rdn_card *card, uint32_t ms)
{
	card->os->delay_us(card->os->cookie, ms * 1000);
}

static uint32_t bswap32(uint32_t v)
{
	return (v << 24) | ((v & 0xff00) << 8) | ((v >> 8) & 0xff00) | (v >> 24);
}

uint32_t rdn_uvd_vcpu_bytes(uint32_t fw_size, uint32_t handles)
{
	return PAGE_UP(fw_size + 8) + RDN_UVD_STACK_SIZE + RDN_UVD_HEAP_SIZE +
	       RDN_UVD_SESSION_SIZE * handles;
}

/* ---- clocks ------------------------------------------------------------ */

static unsigned calc_post_div(unsigned vco_freq, unsigned target_freq,
			      unsigned pd_min, unsigned pd_even)
{
	unsigned post_div = vco_freq / target_freq;

	/* adjust to post divider minimum value */
	if (post_div < pd_min)
		post_div = pd_min;
	/* we always need a frequency less than or equal the target */
	if ((vco_freq / post_div) > target_freq)
		post_div += 1;
	/* post dividers above a certain value must be even */
	if (post_div > pd_even && post_div % 2)
		post_div += 1;
	return post_div;
}

int rdn_uvd_calc_dividers(uint32_t ref_freq, uint32_t vclk, uint32_t dclk,
			  uint32_t *fb_div, uint32_t *vclk_div,
			  uint32_t *dclk_div)
{
	/* The Evergreen limits of evergreen_set_uvd_clocks(). */
	const unsigned vco_max = 250000, fb_factor = 16384, pd_min = 0,
		       pd_max = 128, pd_even = 5;
	const uint32_t fb_mask = 0x03FFFFFF;
	unsigned vco_min = 125000, vco_freq;
	unsigned optimal_score = ~0u;

	if (!ref_freq || !vclk || !dclk)
		return -EINVAL;
	if (vclk > vco_min)
		vco_min = vclk;
	if (dclk > vco_min)
		vco_min = dclk;
	for (vco_freq = vco_min; vco_freq <= vco_max; vco_freq += 100) {
		uint64_t fb = (uint64_t)vco_freq * fb_factor / ref_freq;
		unsigned vdiv, ddiv, score;

		if (fb > fb_mask)
			break;
		fb &= fb_mask;
		vdiv = calc_post_div(vco_freq, vclk, pd_min, pd_even);
		if (vdiv > pd_max)
			break;
		ddiv = calc_post_div(vco_freq, dclk, pd_min, pd_even);
		if (ddiv > pd_max)
			break;
		score = vclk - (vco_freq / vdiv) + dclk - (vco_freq / ddiv);
		if (score < optimal_score) {
			*fb_div = (uint32_t)fb;
			*vclk_div = vdiv;
			*dclk_div = ddiv;
			optimal_score = score;
			if (optimal_score == 0)
				break;
		}
	}
	return optimal_score == ~0u ? -EINVAL : 0;
}

static int send_upll_ctlreq(struct rdn_card *card, uint32_t reg)
{
	unsigned i;

	/* make sure UPLL_CTLREQ is deasserted */
	wr_p(card, reg, 0, ~UPLL_CTLREQ_MASK);
	mdelay(card, 10);
	/* assert UPLL_CTLREQ */
	wr_p(card, reg, UPLL_CTLREQ_MASK, ~UPLL_CTLREQ_MASK);
	/* wait for CTLACK and CTLACK2 to get asserted */
	for (i = 0; i < 100; ++i) {
		uint32_t mask = UPLL_CTLACK_MASK | UPLL_CTLACK2_MASK;

		if ((rd(card, reg) & mask) == mask)
			break;
		mdelay(card, 10);
	}
	/* deassert UPLL_CTLREQ */
	wr_p(card, reg, 0, ~UPLL_CTLREQ_MASK);
	if (i == 100) {
		rdn_log(card->os, RDN_LOG_ERROR, "timeout setting UVD clocks");
		return -ETIMEDOUT;
	}
	return 0;
}

int rdn_uvd_set_clocks(struct rdn_card *card, uint32_t ref_freq,
		       uint32_t vclk, uint32_t dclk)
{
	uint32_t fb_div = 0, vclk_div = 0, dclk_div = 0;
	int r;

	/* bypass vclk and dclk with bclk */
	wr_p(card, CG_UPLL_FUNC_CNTL_2, VCLK_SRC_SEL(1) | DCLK_SRC_SEL(1),
	     ~(VCLK_SRC_SEL_MASK | DCLK_SRC_SEL_MASK));
	/* put PLL in bypass mode */
	wr_p(card, CG_UPLL_FUNC_CNTL, UPLL_BYPASS_EN_MASK, ~UPLL_BYPASS_EN_MASK);

	if (!vclk || !dclk) {
		/* keep the bypass mode, put PLL to sleep */
		wr_p(card, CG_UPLL_FUNC_CNTL, UPLL_SLEEP_MASK, ~UPLL_SLEEP_MASK);
		return 0;
	}
	r = rdn_uvd_calc_dividers(ref_freq, vclk, dclk, &fb_div, &vclk_div,
				  &dclk_div);
	if (r)
		return r;

	/* set VCO_MODE to 1 */
	wr_p(card, CG_UPLL_FUNC_CNTL, UPLL_VCO_MODE_MASK, ~UPLL_VCO_MODE_MASK);
	/* toggle UPLL_SLEEP to 1 then back to 0 */
	wr_p(card, CG_UPLL_FUNC_CNTL, UPLL_SLEEP_MASK, ~UPLL_SLEEP_MASK);
	wr_p(card, CG_UPLL_FUNC_CNTL, 0, ~UPLL_SLEEP_MASK);
	/* deassert UPLL_RESET */
	wr_p(card, CG_UPLL_FUNC_CNTL, 0, ~UPLL_RESET_MASK);
	mdelay(card, 1);

	r = send_upll_ctlreq(card, CG_UPLL_FUNC_CNTL);
	if (r)
		return r;

	/* assert UPLL_RESET again */
	wr_p(card, CG_UPLL_FUNC_CNTL, UPLL_RESET_MASK, ~UPLL_RESET_MASK);
	/* disable spread spectrum */
	wr_p(card, CG_UPLL_SPREAD_SPECTRUM, 0, ~SSEN_MASK);
	/* set feedback divider */
	wr_p(card, CG_UPLL_FUNC_CNTL_3, UPLL_FB_DIV(fb_div), ~UPLL_FB_DIV_MASK);
	/* set ref divider to 0 */
	wr_p(card, CG_UPLL_FUNC_CNTL, 0, ~UPLL_REF_DIV_MASK);

	if (fb_div < 307200)
		wr_p(card, CG_UPLL_FUNC_CNTL_4, 0, ~UPLL_SPARE_ISPARE9);
	else
		wr_p(card, CG_UPLL_FUNC_CNTL_4, UPLL_SPARE_ISPARE9, ~UPLL_SPARE_ISPARE9);

	/* set PDIV_A and PDIV_B */
	wr_p(card, CG_UPLL_FUNC_CNTL_2, UPLL_PDIV_A(vclk_div) | UPLL_PDIV_B(dclk_div),
	     ~(UPLL_PDIV_A_MASK | UPLL_PDIV_B_MASK));

	/* give the PLL some time to settle */
	mdelay(card, 15);
	/* deassert PLL_RESET */
	wr_p(card, CG_UPLL_FUNC_CNTL, 0, ~UPLL_RESET_MASK);
	mdelay(card, 15);
	/* switch from bypass mode to normal mode */
	wr_p(card, CG_UPLL_FUNC_CNTL, 0, ~UPLL_BYPASS_EN_MASK);

	r = send_upll_ctlreq(card, CG_UPLL_FUNC_CNTL);
	if (r)
		return r;

	/* switch VCLK and DCLK selection */
	wr_p(card, CG_UPLL_FUNC_CNTL_2, VCLK_SRC_SEL(2) | DCLK_SRC_SEL(2),
	     ~(VCLK_SRC_SEL_MASK | DCLK_SRC_SEL_MASK));
	mdelay(card, 100);
	return 0;
}

/* ---- the ring ---------------------------------------------------------- */

static void ring_put(struct rdn_uvd *uvd, uint32_t value)
{
	if (uvd->swapped)
		value = bswap32(value);
	rdn_vram_write32(uvd->accel, uvd->ring_offset + uvd->wptr * 4, value);
	uvd->wptr = (uvd->wptr + 1) & (uvd->ring_words - 1);
}

static int ring_begin(struct rdn_uvd *uvd, uint32_t words)
{
	struct rdn_card *card = uvd->accel->card;
	uint32_t i;

	if (words >= uvd->ring_words)
		return -ENOMEM;
	for (i = 0; i < RDN_UVD_USEC_TIMEOUT; i++) {
		uint32_t rptr = rd(card, UVD_RBC_RB_RPTR) & (uvd->ring_words - 1);
		uint32_t used = (uvd->wptr - rptr) & (uvd->ring_words - 1);

		if (uvd->ring_words - 1 - used >= words)
			return 0;
		card->os->delay_us(card->os->cookie, 1);
	}
	return -EBUSY;
}

static void ring_commit(struct rdn_uvd *uvd)
{
	struct rdn_card *card = uvd->accel->card;

	rdn_hdp_flush(uvd->accel);
	wr(card, UVD_RBC_RB_WPTR, uvd->wptr);
	(void)rd(card, UVD_RBC_RB_WPTR);
}

static int ring_test(struct rdn_uvd *uvd)
{
	struct rdn_card *card = uvd->accel->card;
	uint32_t tmp = 0;
	unsigned i;
	int r;

	wr(card, UVD_CONTEXT_ID, 0xCAFEDEAD);
	r = ring_begin(uvd, 2);
	if (r)
		return r;
	ring_put(uvd, RDN_UVD_PACKET0(UVD_CONTEXT_ID, 0));
	ring_put(uvd, 0xDEADBEEF);
	ring_commit(uvd);
	for (i = 0; i < RDN_UVD_USEC_TIMEOUT; i++) {
		tmp = rd(card, UVD_CONTEXT_ID);
		if (tmp == 0xDEADBEEF)
			break;
		card->os->delay_us(card->os->cookie, 1);
	}
	if (i >= RDN_UVD_USEC_TIMEOUT) {
		rdn_log(card->os, RDN_LOG_ERROR,
			"uvd ring test failed (UVD_CONTEXT_ID=0x%08X)", (unsigned)tmp);
		return -EINVAL;
	}
	rdn_log(card->os, RDN_LOG_INFO, "uvd ring test succeeded in %u usecs", i);
	return 0;
}

/* ---- start and stop ---------------------------------------------------- */

/* uvd_v2_2_resume(): tell the UVD memory controller where things are. */
static void program_memory(struct rdn_uvd *uvd)
{
	struct rdn_card *card = uvd->accel->card;
	uint64_t gpu = uvd->accel->vram_base + uvd->vcpu_offset;
	uint64_t addr = gpu >> 3;
	uint32_t size = PAGE_UP(uvd->fw_size + 4) >> 3;

	/* the VCPU memory controller bits 0-27 */
	wr(card, UVD_VCPU_CACHE_OFFSET0, (uint32_t)addr);
	wr(card, UVD_VCPU_CACHE_SIZE0, size);

	addr += size;
	size = RDN_UVD_HEAP_SIZE >> 3;
	wr(card, UVD_VCPU_CACHE_OFFSET1, (uint32_t)addr);
	wr(card, UVD_VCPU_CACHE_SIZE1, size);

	addr += size;
	size = (RDN_UVD_STACK_SIZE + RDN_UVD_SESSION_SIZE * uvd->handles) >> 3;
	wr(card, UVD_VCPU_CACHE_OFFSET2, (uint32_t)addr);
	wr(card, UVD_VCPU_CACHE_SIZE2, size);

	/* bits 28-31 */
	addr = (gpu >> 28) & 0xF;
	wr(card, UVD_LMI_ADDR_EXT, (uint32_t)((addr << 12) | (addr << 0)));

	/* bits 32-39 */
	addr = (gpu >> 32) & 0xFF;
	wr(card, UVD_LMI_EXT40_ADDR, (uint32_t)(addr | (0x9 << 16) | (0x1u << 31)));

	/* tell firmware which hardware it is running on */
	wr(card, UVD_VCPU_CHIP_ID_REG, RDN_UVD_CHIP_ID_TURKS);
}

/* uvd_v1_0_start() */
static int vcpu_start(struct rdn_uvd *uvd)
{
	struct rdn_card *card = uvd->accel->card;
	uint32_t lmi_swap_cntl = 0, mp_swap_cntl = 0;
	uint64_t ring_gpu = uvd->accel->vram_base + uvd->ring_offset;
	uint32_t rb_bufsz, status = 0;
	int i, j, r = 0;

	/* disable clock gating */
	wr(card, UVD_CGC_GATE, 0);
	/* disable interrupt */
	wr_p(card, UVD_MASTINT_EN, 0, ~(1 << 1));
	/* stall UMC and register bus before resetting VCPU */
	wr_p(card, UVD_LMI_CTRL2, 1 << 8, ~(1 << 8));
	wr_p(card, UVD_RB_ARB_CTRL, 1 << 3, ~(1 << 3));
	mdelay(card, 1);

	/* put LMI, VCPU, RBC etc... into reset */
	wr(card, UVD_SOFT_RESET, LMI_SOFT_RESET | VCPU_SOFT_RESET |
	   LBSI_SOFT_RESET | RBC_SOFT_RESET | CSM_SOFT_RESET |
	   CXW_SOFT_RESET | TAP_SOFT_RESET | LMI_UMC_SOFT_RESET);
	mdelay(card, 5);

	/* take UVD block out of reset */
	wr_p(card, SRBM_SOFT_RESET_UVD_REG, 0, ~SOFT_RESET_UVD);
	mdelay(card, 5);

	/* initialize UVD memory controller */
	wr(card, UVD_LMI_CTRL, 0x40 | (1 << 8) | (1 << 13) | (1 << 21) |
	   (1 << 9) | (1 << 20));

	if (uvd->swapped) {
		/* swap (8 in 32) RB and IB */
		lmi_swap_cntl = 0xa;
		mp_swap_cntl = 0;
	}
	wr(card, UVD_LMI_SWAP_CNTL, lmi_swap_cntl);
	wr(card, UVD_MP_SWAP_CNTL, mp_swap_cntl);

	wr(card, UVD_MPC_SET_MUXA0, 0x40c2040);
	wr(card, UVD_MPC_SET_MUXA1, 0x0);
	wr(card, UVD_MPC_SET_MUXB0, 0x40c2040);
	wr(card, UVD_MPC_SET_MUXB1, 0x0);
	wr(card, UVD_MPC_SET_ALU, 0);
	wr(card, UVD_MPC_SET_MUX, 0x88);

	/* take all subblocks out of reset, except VCPU */
	wr(card, UVD_SOFT_RESET, VCPU_SOFT_RESET);
	mdelay(card, 5);

	/* enable VCPU clock */
	wr(card, UVD_VCPU_CNTL, 1 << 9);
	/* enable UMC */
	wr_p(card, UVD_LMI_CTRL2, 0, ~(1 << 8));
	wr_p(card, UVD_RB_ARB_CTRL, 0, ~(1 << 3));

	/* boot up the VCPU */
	wr(card, UVD_SOFT_RESET, 0);
	mdelay(card, 10);

	for (i = 0; i < 10; ++i) {
		for (j = 0; j < 100; ++j) {
			status = rd(card, UVD_STATUS);
			if (status & 2)
				break;
			mdelay(card, 10);
		}
		r = 0;
		if (status & 2)
			break;
		rdn_log(card->os, RDN_LOG_ERROR,
			"uvd not responding (status 0x%08x), resetting the VCPU", (unsigned)status);
		wr_p(card, UVD_SOFT_RESET, VCPU_SOFT_RESET, ~VCPU_SOFT_RESET);
		mdelay(card, 10);
		wr_p(card, UVD_SOFT_RESET, 0, ~VCPU_SOFT_RESET);
		mdelay(card, 10);
		r = -EIO;
	}
	if (r) {
		rdn_log(card->os, RDN_LOG_ERROR, "uvd not responding, giving up");
		return r;
	}

	/* enable interrupt (the mask only; nothing is routed) */
	wr_p(card, UVD_MASTINT_EN, 3 << 1, ~(3 << 1));
	/* force RBC into idle state */
	wr(card, UVD_RBC_RB_CNTL, 0x11010101);
	/* set the write pointer delay */
	wr(card, UVD_RBC_RB_WPTR_CNTL, 0);
	/* program the 4GB memory segment for rptr and ring buffer */
	wr(card, UVD_LMI_EXT40_ADDR,
	   (uint32_t)((ring_gpu >> 32) | (0x7 << 16) | (0x1u << 31)));
	/* initialize the ring buffer's read and write pointers */
	wr(card, UVD_RBC_RB_RPTR, 0x0);
	uvd->wptr = rd(card, UVD_RBC_RB_RPTR);
	wr(card, UVD_RBC_RB_WPTR, uvd->wptr);
	/* set the ring address */
	wr(card, UVD_RBC_RB_BASE, (uint32_t)ring_gpu);
	/* set ring buffer size (log2 of bytes) */
	for (rb_bufsz = 0; (1u << rb_bufsz) < uvd->ring_words * 4; rb_bufsz++)
		;
	wr_p(card, UVD_RBC_RB_CNTL, (1 << 8) | rb_bufsz, ~0x11f1f);
	return 0;
}

/* uvd_v1_0_stop() */
static void vcpu_stop(struct rdn_uvd *uvd)
{
	struct rdn_card *card = uvd->accel->card;

	/* force RBC into idle state */
	wr(card, UVD_RBC_RB_CNTL, 0x11010101);
	/* stall UMC and register bus before resetting VCPU */
	wr_p(card, UVD_LMI_CTRL2, 1 << 8, ~(1 << 8));
	wr_p(card, UVD_RB_ARB_CTRL, 1 << 3, ~(1 << 3));
	mdelay(card, 1);
	/* put VCPU into reset */
	wr(card, UVD_SOFT_RESET, VCPU_SOFT_RESET);
	mdelay(card, 5);
	/* disable VCPU clock */
	wr(card, UVD_VCPU_CNTL, 0x0);
	/* unstall UMC and register bus */
	wr_p(card, UVD_LMI_CTRL2, 0, ~(1 << 8));
	wr_p(card, UVD_RB_ARB_CTRL, 0, ~(1 << 3));
}

int rdn_uvd_wake(struct rdn_uvd *uvd)
{
	return rdn_uvd_set_clocks(uvd->accel->card, uvd->ref_freq, RDN_UVD_VCLK,
				  RDN_UVD_DCLK);
}

int rdn_uvd_idle(struct rdn_uvd *uvd)
{
	return rdn_uvd_set_clocks(uvd->accel->card, uvd->ref_freq, 0, 0);
}

int rdn_uvd_init(struct rdn_uvd *uvd, struct rdn_accel *accel,
		 uint32_t ref_freq, const uint8_t *fw, uint32_t fw_size,
		 uint32_t handles, uint32_t vcpu_offset, uint32_t ring_offset,
		 uint32_t ring_bytes)
{
	struct rdn_card *card = accel->card;
	volatile uint8_t *bytes = (volatile uint8_t *)accel->aperture;
	uint32_t vcpu_bytes = rdn_uvd_vcpu_bytes(fw_size, handles);
	uint32_t i;
	int r;

	if (!accel->aperture || !fw || !fw_size || (fw_size & 3) || !handles ||
	    (vcpu_offset & 4095) || (ring_offset & 4095) ||
	    ring_bytes < 256 || (ring_bytes & (ring_bytes - 1)))
		return -EINVAL;
	if ((uint64_t)vcpu_offset + vcpu_bytes > accel->aperture_size ||
	    (uint64_t)ring_offset + ring_bytes + RDN_UVD_RING_EXTRA > accel->aperture_size)
		return -EINVAL;
	if (vcpu_offset < ring_offset + ring_bytes + RDN_UVD_RING_EXTRA &&
	    ring_offset < vcpu_offset + vcpu_bytes)
		return -EINVAL;	/* the two overlap */
	/* the ring and the VCPU region must share the upper address bits */
	if (((accel->vram_base + vcpu_offset) >> 32) != ((accel->vram_base + ring_offset) >> 32))
		return -EINVAL;

	memset(uvd, 0, sizeof(*uvd));
	uvd->accel = accel;
	uvd->ref_freq = ref_freq;
	uvd->handles = handles;
	uvd->fw_size = fw_size;
	uvd->vcpu_offset = vcpu_offset;
	uvd->vcpu_bytes = vcpu_bytes;
	uvd->ring_offset = ring_offset;
	uvd->ring_words = ring_bytes / 4;
	uvd->fence_offset = ring_offset + ring_bytes;
	uvd->swapped = RDN_BIG_ENDIAN;

	/* radeon_uvd_resume(): the firmware as in the file, the rest zero */
	for (i = 0; i < fw_size; i++)
		bytes[vcpu_offset + i] = fw[i];
	for (; i < vcpu_bytes; i++)
		bytes[vcpu_offset + i] = 0;
	for (i = 0; i < ring_bytes + RDN_UVD_RING_EXTRA; i += 4)
		rdn_vram_write32(accel, ring_offset + i, 0);
	rdn_hdp_flush(accel);

	/* raise clocks while booting up the VCPU */
	r = rdn_uvd_wake(uvd);
	if (r)
		goto done;
	program_memory(uvd);
	r = vcpu_start(uvd);
	if (r)
		goto done;
	r = ring_test(uvd);
	if (r)
		goto done;

	r = ring_begin(uvd, 10);
	if (r)
		goto done;
	ring_put(uvd, RDN_UVD_PACKET0(UVD_SEMA_WAIT_FAULT_TIMEOUT_CNTL, 0));
	ring_put(uvd, 0xFFFFF);
	ring_put(uvd, RDN_UVD_PACKET0(UVD_SEMA_WAIT_INCOMPLETE_TIMEOUT_CNTL, 0));
	ring_put(uvd, 0xFFFFF);
	ring_put(uvd, RDN_UVD_PACKET0(UVD_SEMA_SIGNAL_INCOMPLETE_TIMEOUT_CNTL, 0));
	ring_put(uvd, 0xFFFFF);
	/* clear timeout status bits */
	ring_put(uvd, RDN_UVD_PACKET0(UVD_SEMA_TIMEOUT_STATUS, 0));
	ring_put(uvd, 0x8);
	ring_put(uvd, RDN_UVD_PACKET0(UVD_SEMA_CNTL, 0));
	ring_put(uvd, 3);
	ring_commit(uvd);
	uvd->ready = true;
	rdn_log(card->os, RDN_LOG_INFO, "uvd initialized");
done:
	/* lower clocks again */
	(void)rdn_uvd_idle(uvd);
	return r;
}

void rdn_uvd_fini(struct rdn_uvd *uvd)
{
	if (!uvd->accel)
		return;
	vcpu_stop(uvd);
	uvd->ready = false;
	(void)rdn_uvd_idle(uvd);
}

/* ---- work -------------------------------------------------------------- */

int rdn_uvd_ib_submit(struct rdn_uvd *uvd, uint64_t gpu_addr, uint32_t words,
		      uint32_t *seq)
{
	uint64_t ring_gpu = uvd->accel->vram_base + uvd->ring_offset;
	uint32_t fence_low = (uint32_t)(ring_gpu + uvd->ring_words * 4);
	int r;

	if (!uvd->ready || !words)
		return -EINVAL;
	if ((gpu_addr >> 32) != (ring_gpu >> 32))
		return -EINVAL;
	r = ring_begin(uvd, 16);
	if (r)
		return r;
	*seq = ++uvd->fence_emitted;
	ring_put(uvd, RDN_UVD_PACKET0(UVD_RBC_IB_BASE, 0));
	ring_put(uvd, (uint32_t)gpu_addr);
	ring_put(uvd, RDN_UVD_PACKET0(UVD_RBC_IB_SIZE, 0));
	ring_put(uvd, words);
	/* the fence: the VCPU writes `seq` to memory, then traps (uvd_v1_0_fence_emit) */
	ring_put(uvd, RDN_UVD_PACKET0(UVD_GPCOM_VCPU_DATA0, 0));
	ring_put(uvd, fence_low);
	ring_put(uvd, RDN_UVD_PACKET0(UVD_GPCOM_VCPU_DATA1, 0));
	ring_put(uvd, *seq);
	ring_put(uvd, RDN_UVD_PACKET0(UVD_GPCOM_VCPU_CMD, 0));
	ring_put(uvd, 0);
	ring_put(uvd, RDN_UVD_PACKET0(UVD_GPCOM_VCPU_DATA0, 0));
	ring_put(uvd, 0);
	ring_put(uvd, RDN_UVD_PACKET0(UVD_GPCOM_VCPU_DATA1, 0));
	ring_put(uvd, 0);
	ring_put(uvd, RDN_UVD_PACKET0(UVD_GPCOM_VCPU_CMD, 0));
	ring_put(uvd, 2);
	ring_commit(uvd);
	return 0;
}

bool rdn_uvd_fence_done(struct rdn_uvd *uvd, uint32_t seq)
{
	rdn_hdp_flush(uvd->accel);
	return (int32_t)(rdn_vram_read32(uvd->accel, uvd->fence_offset) - seq) >= 0;
}

int rdn_uvd_fence_wait(struct rdn_uvd *uvd, uint32_t seq, uint32_t timeout_ms)
{
	struct rdn_card *card = uvd->accel->card;
	uint64_t end = card->os->time_ms(card->os->cookie) + timeout_ms;

	while (!rdn_uvd_fence_done(uvd, seq)) {
		if (card->os->time_ms(card->os->cookie) >= end)
			return -ETIMEDOUT;
		card->os->delay_us(card->os->cookie, 100);
	}
	return 0;
}
