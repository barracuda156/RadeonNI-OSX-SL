# Platform state: phase 1, the G5, DisplayPort, Open Firmware

Moved out of CLAUDE.md. Read when working on the framebuffer kext, the real
G5, modes, DisplayPort, Open Firmware or the QEMU guest. Update it when that
state changes (dated entries; the journal keeps the history).

**Phase 1 is complete (2026-10-04), every milestone confirmed on screen by
the user.** Tiger 10.4.11 in QEMU shows its desktop on the real card at
1366x768 and 1920x1080, at 8, 16 and 32 bpp, with resolution changes from
System Preferences and a software cursor.

- The card is Turks PRO `1002:675d` at `0000:10:00.0` (audio at `10:00.1`),
  GDDR5, monitor on the DVI-I connector. VBIOS dump in `private/vbios.rom`.
- `hw/` cold-POSTs the card, reads the EDID over DDC, starts the display
  engine clock and sets modes through AtomBIOS. No MC microcode is needed.
- `kext/RadeonNI` is the `IOFramebuffer` subclass built on it. In the guest
  it is loaded by hand (`scripts/kext.sh up`); the window server only looks
  for framebuffers when it starts, so `up` restarts it. Installed in
  `/System/Library/Extensions` it comes up at boot by itself (verified in
  the guest with the G5 package, then uninstalled).
- `scripts/make-g5-package.sh` builds `build/RadeonNI-g5.tar.gz` for the
  real Power Mac G5. It has never run on a real Mac.
- `images/tiger-g5.qcow2` (2026-10-05) is a copy of the guest's disk with
  the G5 package installed, to be written to a disk for the real G5
  (`docs/PLAN.md`, "First run on the real G5"). Never booted there. The
  guest's own disk has the snapshot `before-g5-image` from the same moment.
- The G5 (`PowerMac11,2`, 192.168.1.127 since 2026-10-07, before that
  .128) runs with the card and the
  installed phase 1 kext at 1920x1080 (2026-10-05, kernel log read over
  ssh). The card is in the G5, not in the host, so nothing on the host can
  use it. Reach the G5 like the guest but on port 22: user `tiger`, key
  `private/ssh/tiger_rsa`, the old-algorithm options from
  `scripts/tiger.sh ssh`; `sudo` there asks for the password (`tiger`).
  Open Firmware assigns the card's ROM BAR there (journal). Since the
  same day it boots with `install.sh --accel` and `~/gl/qe` reports
  Quartz Extreme in use; the user has not yet said what they see.
- DisplayPort works (2026-10-07, G5, seen by the user: "It looks about
  correct"): the monitor is on the card's DisplayPort connector, 4 lanes
  at 1.62 Gbit/s, 1920x1080, Quartz Extreme in use. `hw/rdn_dp.c`; the
  kext tries DVI-I first, then DisplayPort, and logs DPCD and the
  training result. No Linux trace of DisplayPort exists; `tests/dp_link`
  is a simulated sink. Not done: hot-plug and monitor wake (the link is
  trained only at a mode set), 2.7 Gbit/s on a real sink, passive
  adapters. The kext before it on the G5 is `~/RadeonNI.kext.before-dp`
  (journal 2026-10-07; `docs/HARDWARE.md`, "DisplayPort connector").
- Open Firmware (2026-10-09, `docs/OPEN-FIRMWARE.md`, journal; **done and
  permanent on the G5**, seen by the user): one client, `of/` -> `rdnk.elf`
  (built from `hw/`, no VBIOS inside: it finds the card in the device tree
  and reads the ROM at run time; a ROM it cannot use leaves the Mac booting
  as without it). `g5/of-install.sh` (in the package, with `rdnk.elf` and
  `of-block.txt` from `scripts/of-hook.py block`) appends a block to
  `nvramrc`, marked `( RadeonNI-OF begin )` ... `( RadeonNI-OF end )`
  (never `\` comments: Open Firmware reads nvramrc as one line), saves the
  old text and `use-nvramrc?` in `/Library/RadeonNI/OpenFirmware`, sets
  `use-nvramrc?` true; `of-uninstall.sh` takes only the block out. The
  block patches `quit` and `mac-boot` in RAM (cells checked first): Cmd-Opt-O-F
  (Win+Alt+O+F on the wired PC keyboard, held from power-on) gives the
  console and `ok` prompt on the 7570's monitor; a normal boot runs the client,
  which POSTs the card, sets 1920x1080, marks `/chosen` `rdn-chain`
  and chains `mac-boot`, so Apple's logo shows and the kext (`ConsoleWarm`,
  no rainbow) takes over. A missing file or a failing client falls back to the
  plain boot (`catch`). The kext checks nvramrc (property `OFHook`) and, on a
  PowerMac11,x without the block, shows a notice a minute after start
  (`rdn_ofhook=0` silences); it never writes NVRAM. NVRAM loss removes the
  block: run the installer again. Developing: `python3 -I scripts/of-hook.py
  apply --oneshot` (one boot, then clears itself), `scripts/of-run.py`
  (console session over telnet), host mock `make -C of build/mockf`. The
  G5's wired keyboard must be plugged in before power-up. After a hang expect
  two power cycles. Never store long lines in `boot-command`.
- Linux install of the Open Firmware support (2026-10-10, `docs/OPEN-FIRMWARE.md`, "Installing
  from Linux"): `g5/linux/` and `scripts/make-linux-package.sh`, for ArchPOWER and the like;
  client on the HFS boot partition, block written with powerpc-utils' `nvram`. Tested only
  with a fake `nvram` (`tests/linux_install.sh`); never run on a G5 under Linux.
- The host's sshd accepts Tiger's old ssh algorithms
  (`/etc/ssh/sshd_config.d/10-tiger.conf`, user's request).
- The tag `working-framebuffer` marks the confirmed phase 1 state including
  the G5 install package.
- The guest: snapshots `clean-install` and `pre-kext-install` (no kext
  installed in either), user `tiger` / password `tiger`,
  `scripts/tiger.sh ssh`. QEMU must be the patched build (`patches/qemu/`).
  Screensaver and display sleep are off in the guest.
- `radeon` is blacklisted on the host (`/etc/modprobe.d/osx-gpu.conf`)
  because unbinding it led to a host kernel oops.

What was left out on purpose is listed in `docs/PLAN.md` under "Known gaps
after phase 1" and "Deferred to the real G5".
