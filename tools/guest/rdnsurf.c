/*
 * rdnsurf: the surfaces the kext knows a picture buffer for.
 *
 *   rdnsurf
 *
 * Prints id, size and aperture offset of every surface with a buffer
 * (RDN_UC_SURFACE_LIST) and the ID locked for reading now. Needs only
 * IOKit, so it builds on the G5:
 *   gcc -I<repo>/hw -o rdnsurf rdnsurf.c -framework IOKit -framework CoreFoundation
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdint.h>
#include <IOKit/IOKitLib.h>
#include "rdn_user.h"

int main(void)
{
	io_service_t service = IOServiceGetMatchingService(kIOMasterPortDefault,
		IOServiceMatching(RDN_UC_SERVICE_CLASS));
	io_connect_t conn;
	struct rdn_user_surfaces list;
	IOByteCount size = sizeof(list);
	int locked = 0;
	uint32_t i;

	if (!service || IOServiceOpen(service, mach_task_self(), RDN_UC_TYPE, &conn)) {
		fprintf(stderr, "cannot reach the accelerator\n");
		return 1;
	}
	if (IOConnectMethodScalarIStructureO(conn, RDN_UC_SURFACE_LIST, 0, &size, &list)) {
		fprintf(stderr, "surface list failed\n");
		return 1;
	}
	for (i = 0; i < list.count; i++)
		printf("surface 0x%x (%u): %ux%u, %u bytes a row, aperture offset 0x%x\n",
		       (unsigned)list.surface[i].id, (unsigned)list.surface[i].id,
		       (unsigned)list.surface[i].width, (unsigned)list.surface[i].height,
		       (unsigned)list.surface[i].row_bytes, (unsigned)list.surface[i].offset);
	if (!IOConnectMethodScalarIScalarO(conn, RDN_UC_SURFACE_LOCKED, 0, 1, &locked))
		printf("%u surfaces with a buffer; locked for reading: %u\n",
		       (unsigned)list.count, (unsigned)locked);
	IOServiceClose(conn);
	return 0;
}
