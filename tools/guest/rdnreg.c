/*
 * rdnreg: read card registers listed on standard input (hex or decimal byte
 * offsets, one per line) through RDN_UC_REG_READ and print "offset value".
 * Read only. Builds on the G5 (IOKit only):
 *   gcc -I<repo>/hw -o rdnreg rdnreg.c -framework IOKit -framework CoreFoundation
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <mach/mach.h>
#include <IOKit/IOKitLib.h>
#include "rdn_user.h"

int main(void)
{
	io_service_t service = IOServiceGetMatchingService(kIOMasterPortDefault,
		IOServiceMatching(RDN_UC_SERVICE_CLASS));
	io_connect_t conn;
	char line[64];

	if (!service || IOServiceOpen(service, mach_task_self(), RDN_UC_TYPE, &conn)) {
		fprintf(stderr, "cannot reach the accelerator\n");
		return 1;
	}
	while (fgets(line, sizeof(line), stdin)) {
		unsigned long off = strtoul(line, NULL, 0);
		int out = 0;

		if (IOConnectMethodScalarIScalarO(conn, RDN_UC_REG_READ, 1, 1, (int)off, &out))
			printf("0x%lx ERR\n", off);
		else
			printf("0x%lx 0x%08x\n", off, (unsigned)out);
	}
	return 0;
}
