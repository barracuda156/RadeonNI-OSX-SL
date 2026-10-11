# Mac OS X 10.5 (Leopard, PowerPC): framebuffer kext, paper-checked only

Read this before touching 10.5 support. **Nothing here has run on a 10.5
Mac.** The only thing known is that the Tiger-built kext *links* against
Leopard's published kernel export lists once two stubs are added, and
that the class layouts the kext depends on are the same in Apple's public
10.4 and 10.5-era sources. Acceleration is not part of it (below).

Build and package: `scripts/leopard.sh build|check|package`. The kext is
built in the Tiger guest with `make LEOPARD=1` (its own `build-leopard/`),
which adds `kext/RadeonNI/leopard.s`. `g5/install.sh` accepts 10.5 only in
a package with the file `LEOPARD` and without `--accel`; `--check`
(`kextload -t -n`, writes nothing) and `--try` (load from /tmp, not
installed) are for a first run.

## What was compared (2026-10-11, from Apple's public sources)

Sources: `apple-oss-distributions/xnu` tags `xnu-792.24.17` (10.4.11) and
`xnu-1228.15.4` (10.5.8), `IOGraphics` 179 (10.4.11) against 191.5, 193.2.2
and 233.3, `IOPCIFamily` 79.1 to 162.3. Which tag shipped in which 10.5.x was
not confirmed; every tag up to 233.3 (IOGraphics) and 162.3 (IOPCIFamily) gave
the same answer, and the layouts only move in IOGraphics 305 and
IOPCIFamily 196 (10.6). Reading only; nothing copied.

| Class | Result |
|---|---|
| IOFramebuffer, IOGraphicsDevice | same 97 virtual and reserved slots, same 54 data members (`Point` is `IOGPoint`, typedef'd back to `Point`, same struct) |
| IOService, IORegistryEntry | slots compatible; one reserved slot became a method (below); `IOPMpriv *priv` is `IOServicePM *pwrMgt`, a pointer either way |
| IOUserClient | `externalMethod` took reserved slot 0; ivar block is 32 bytes either way (`void *[8]` against 3 + 1 bytes + `void *[7]`) |
| IOPCIDevice, IOAGPDevice | own slots and data members unchanged through IOPCIFamily 162.3 |
| IOAccelerator | exists in IOGraphics 233.3, no data members |
| IOMemoryDescriptor, IOBufferMemoryDescriptor, IOWorkLoop, IOTimerEventSource | compatible (reserved slots used, none moved) |

Method used (and its trap): a class's vtable slots are its `virtual`
declarations plus one per `OSMetaClassDeclareReservedUnused`; `...Used`
declares nothing and the method that took the slot is declared in its
place. Comparing textual order instead gives false "moves" wherever a
newer header redeclares an inherited virtual (an override takes no slot).

## What breaks, and the stubs

Leopard's 10.4 compatibility libraries (`com.apple.kernel.iokit`, `.libkern`,
`.mach`, `.bsd`, `.6.0`: version 7.9.9, compatible from 1.0.0b1) satisfy the
kext's "6.0" requests. Of the 586 symbols the kext imports, 426 are in the
kernel's export lists (`_IOLockLock` as an alias of `lck_mtx_lock`), 160
come from the PCI and graphics families, and **two do not exist in
Leopard**: `IOService::_RESERVEDIOService4` and
`IOUserClient::_RESERVEDIOUserClient0`. Every vtable of a Tiger-built
subclass points at them. They are `IOService::systemWillShutdown` and
`IOUserClient::externalMethod` in Leopard; `leopard.s` defines the old names
as jumps to those (through CTR, the kernel is out of range of `b`). IOGraphics
does not override `systemWillShutdown`, so the jump is exact.

For the 160 family symbols only names and reserved slots were checked: all
31 `IOFramebuffer`, 17 `IOAGPDevice` and 13 `IOPCIDevice` reserved slots
the kext references are still unused in the Leopard-era headers, and every
method name is in the header. Signatures were not compared.

## Outputs from the VBIOS (2026-10-11)

The first run on a Mac with a retail HD 6670 (`1002:6758`) found no EDID: the
driver only knew the 7570 board's two connectors (DVI-I on UNIPHY0, DDC 0x93;
"DisplayPort" on UNIPHY2, DDC 0x92). The BIOS answered sink type 0x04, dual-link
DVI-D, on the "DisplayPort" lines, and HPD4 read high. Since then
`rdn_output_detect()` (`hw/rdn_modeset.c`) tries, in order: the two built in; the
DisplayPort lines as TMDS (`card->twin`); and the outputs the VBIOS's object
table describes (`board_outputs_read()`, a port of
`radeon_get_atom_connector_info_from_object_table()`: UNIPHY and link from the
encoder object, DDC line from the connector's I2C record, hot-plug pin from its
HPD record through the GPIO pin table). Every path is logged as `board: path ...`.
**First run (same day, a retail HD 6670, DVI direct cable):** the table was
HDMI-A on UNIPHY2 (DDC 0x94, HPD1), dual-link DVI-D on UNIPHY0 link A (DDC 0x92,
HPD4) and VGA on DAC1 (DDC 0x90, no HPD). The EDID came from the DVI port's
line 0x92, found through the "twin" candidate, which pairs that line with the
7570's UNIPHY2: the picture went out of the HDMI port's transmitter and the
DVI port stayed dark (log: `EDID found`, mode set returned 0, no display). Fixed:
with a usable table only the built-in outputs the table also has are tried,
then the table's; the twin is only for a VBIOS with no digital path.
`tests/board_outputs.c` now rebuilds that case (two DDC lines moved in the
7570's connector records) and fails on the old order.

Link B is supported in the transmitter blocks (`rdn_output.linkb`); nothing has
driven a link B output. `tests/board_outputs.c` checks the parse against the
7570's VBIOS: it must return the built-in wiring.

## Not done, not known

- **A load on a real 10.5.** `kextload -t -n` there is the first test.
- The family kexts' own version requirement: `Info.plist` asks for
  `IOGraphicsFamily` and `IOPCIFamily` 1.0; not checked against 10.5's.
- Runtime behaviour: the window server and `IOFramebuffer` of 10.5 differ
  from 10.4 in ways the headers do not show (mode validation, display
  change notifications, clamshell).
- **Acceleration is out**: the GL bundle follows the Tiger `gld*` interface,
  the window server hook (`cglsSetInteger`, twelve checked words) is
  10.4.11's, and the 2D plug-in is Tiger's. 10.5 would be a new interface
  study (VMsvga2's 10.5/10.6 signatures are the only lead, `[I]` in
  `RESEARCH.md`).
- The Tiger kext does not include `leopard.s`: `systemWillShutdown` does not
  exist there and the link would fail.
