/*
 * rdnuvd: ask the kext to bring UVD up and look at the result.
 *
 *   rdnuvd info          the engine's state (starts nothing)
 *   rdnuvd init          RDN_UC_UVD_INIT: PLL, firmware, VCPU, ring test
 *   rdnuvd test          init if needed, then run one indirect buffer that
 *                        writes UVD_CONTEXT_ID, wait for its fence, read it back
 *   rdnuvd regs          UVD_STATUS and a few more registers
 *
 * The kext does nothing about UVD unless its personality has FW_UVD, and
 * only when a program calls init: this tool is that program. Builds on the
 * G5 itself (IOKit only; needs hw/rdn_user.h and hw/rdn_uvd_reg.h):
 *   gcc -I<repo>/hw -o rdnuvd rdnuvd.c -framework IOKit -framework CoreFoundation
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <mach/mach.h>
#include <IOKit/IOKitLib.h>
#include "rdn_user.h"
#include "rdn_uvd_reg.h"

static io_connect_t conn;
static struct rdn_user_info info;
static volatile uint32_t *aperture;

static int reg(uint32_t off, uint32_t *v)
{
	int out = 0;
	int kr = IOConnectMethodScalarIScalarO(conn, RDN_UC_REG_READ, 1, 1, (int)off, &out);

	*v = (uint32_t)out;
	return kr;
}

static int open_accel(void)
{
	io_service_t service = IOServiceGetMatchingService(kIOMasterPortDefault,
		IOServiceMatching(RDN_UC_SERVICE_CLASS));
	IOByteCount size = sizeof(info);
	vm_address_t addr = 0;
	vm_size_t len = 0;

	if (!service || IOServiceOpen(service, mach_task_self(), RDN_UC_TYPE, &conn) ||
	    IOConnectMethodScalarIStructureO(conn, RDN_UC_GET_INFO, 0, &size, &info) ||
	    info.version != RDN_USER_VERSION ||
	    IOConnectMapMemory(conn, RDN_UC_MEMORY_APERTURE, mach_task_self(), &addr, &len,
			       kIOMapAnywhere)) {
		fprintf(stderr, "cannot reach the accelerator\n");
		return -1;
	}
	aperture = (volatile uint32_t *)addr;
	return 0;
}

static void show_info(void)
{
	int up = 0, vcpu = 0, ring = 0;
	int kr = IOConnectMethodScalarIScalarO(conn, RDN_UC_UVD_INFO, 0, 3, &up, &vcpu, &ring);

	if (kr)
		printf("UVD info: 0x%x (a kext without UVD methods)\n", kr);
	else
		printf("UVD %s; VCPU region at aperture offset 0x%x, ring at 0x%x\n",
		       up ? "up" : "not started", (unsigned)vcpu, (unsigned)ring);
}

static void show_regs(void)
{
	static const struct { const char *name; uint32_t off; } r[] = {
		{ "UVD_STATUS", UVD_STATUS }, { "UVD_SOFT_RESET", UVD_SOFT_RESET },
		{ "UVD_VCPU_CNTL", UVD_VCPU_CNTL }, { "UVD_RBC_RB_RPTR", UVD_RBC_RB_RPTR },
		{ "UVD_RBC_RB_WPTR", UVD_RBC_RB_WPTR }, { "UVD_CONTEXT_ID", UVD_CONTEXT_ID },
		{ "CG_UPLL_FUNC_CNTL", CG_UPLL_FUNC_CNTL }, { "UVD_LMI_SWAP_CNTL", UVD_LMI_SWAP_CNTL },
	};
	unsigned i;
	uint32_t v;

	for (i = 0; i < sizeof(r) / sizeof(r[0]); i++)
		if (!reg(r[i].off, &v))
			printf("  %-18s 0x%08x\n", r[i].name, (unsigned)v);
}

static int do_init(void)
{
	int status = 0;
	int kr = IOConnectMethodScalarIScalarO(conn, RDN_UC_UVD_INIT, 0, 1, &status);

	printf("UVD init: kern_return 0x%x, UVD_STATUS 0x%08x\n", kr, (unsigned)status);
	return kr;
}

static int do_test(void)
{
	int off = 0, fence = 0, reached = 0;
	uint32_t v = 0;
	int kr;

	if (do_init())
		return 1;
	if (IOConnectMethodScalarIScalarO(conn, RDN_UC_ALLOC, 2, 1, 4096, 4096, &off)) {
		fprintf(stderr, "no video memory for the buffer\n");
		return 1;
	}
	/* in the ring's byte order: the host's own, stored with one 32-bit store each */
	aperture[off / 4] = RDN_UVD_PACKET0(UVD_CONTEXT_ID, 0);
	aperture[off / 4 + 1] = 0x600DF00D;
	kr = IOConnectMethodScalarIScalarO(conn, RDN_UC_UVD_SUBMIT, 2, 1, off, 2, &fence);
	printf("UVD submit: kern_return 0x%x, fence %d\n", kr, fence);
	if (kr)
		return 1;
	kr = IOConnectMethodScalarIScalarO(conn, RDN_UC_UVD_FENCE_WAIT, 2, 1, fence, 1000, &reached);
	printf("fence wait: kern_return 0x%x, %s\n", kr, reached ? "reached" : "NOT reached in 1 s");
	if (!reg(UVD_CONTEXT_ID, &v))
		printf("UVD_CONTEXT_ID = 0x%08x (%s)\n", (unsigned)v,
		       v == 0x600DF00D ? "the buffer ran" : "the buffer did not run");
	IOConnectMethodScalarIScalarO(conn, RDN_UC_FREE, 1, 0, off);
	return reached && v == 0x600DF00D ? 0 : 1;
}

int main(int argc, char **argv)
{
	const char *cmd = argc > 1 ? argv[1] : "info";

	if (open_accel())
		return 1;
	if (!strcmp(cmd, "info"))
		show_info();
	else if (!strcmp(cmd, "init"))
		return do_init() ? 1 : 0;
	else if (!strcmp(cmd, "test"))
		return do_test();
	else if (!strcmp(cmd, "regs"))
		show_regs();
	else {
		fprintf(stderr, "usage: rdnuvd info | init | test | regs\n");
		return 2;
	}
	return 0;
}
