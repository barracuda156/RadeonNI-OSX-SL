RadeonNI for Mac OS X 10.5 (Leopard, PowerPC), with OpenGL acceleration
=======================================================================

A driver for the AMD Radeon HD 7570 / HD 6670 family (Turks: PCI IDs
1002:675d, 6758, 6759, 675b, 675f). This build has been run on one machine:
a Power Mac G5 Late 2005 (PowerMac11,2) with a 1002:675d card under Mac OS X
10.5.9 (9L31b). It is a test release, not a finished product.

What works there: the screen at the monitor's native mode (1920x1080 seen),
OpenGL 4.6 compatibility (Mesa 26.2.4 on the card) for programs' windows and
full screen: Tux Racer, Quake 3 (timedemo four, 1920x1080: 185 fps),
Pirates!, the Doom 3 demo menus; the desktop is whole.
What does not: Quartz Extreme and Core Image run in software (System
Profiler says "Not Supported" / "Software"), the hardware cursor is not
enabled, sleep and display sleep are not checked, and a Mac that is not a
PowerMac11,2 has not been tried at all.

Install (Terminal, in this folder, administrator password needed). Turn on
Remote Login (System Preferences, Sharing) first so that you can get in
over ssh if the screen stays dark.

  sudo sh ./install.sh --check      writes and loads nothing
  sudo sh ./install.sh --accel      kext + OpenGL bundle + 2D plug-in
                                    + microcode; then restart

Undo: sudo sh ./uninstall.sh, or Safe Boot (Shift held at startup), or
single-user mode (Command-S):
  /sbin/fsck -fy && /sbin/mount -uw /
  rm -rf /System/Library/Extensions/RadeonNI.kext
  rm -f /System/Library/Extensions.mkext /System/Library/Extensions.kextcache
  reboot

Games and screensavers: Leopard's screensaver and sleep timers cover a game
that is waiting for you; set them to "Never" while testing.

What to send back: "sw_vers", "uname -a", the Mac model ("system_profiler
SPHardwareDataType"), the lines of /var/log/system.log that start with
"RadeonNI:", and what the monitor showed. Programs that start from the
Terminal write the bundle's log if RDN_GLD_LOG=/tmp/rdngld.log is set.

The driver is MIT-licensed (LICENSE); the microcode is AMD's (LICENSE.radeon).
Nothing is ever written to the card's flash.
