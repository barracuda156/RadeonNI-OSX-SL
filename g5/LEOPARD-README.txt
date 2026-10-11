RadeonNI for Mac OS X 10.5 (Leopard, PowerPC): EXPERIMENTAL
============================================================

A framebuffer driver for the AMD Radeon HD 7570 / HD 6670 family (Turks:
PCI IDs 1002:675d, 6758, 6759, 675b, 675f). It has NEVER run on Mac OS X
10.5. The only machine it has run on is a Power Mac G5 under Mac OS X
10.4.11 with a 1002:675d card. For 10.5 it was checked on paper (class
layouts and exported symbols against Apple's public 10.5 sources), not on
a Mac. Treat it as a test, not as an installation.

What it is: the screen of the card, from the monitor's EDID. No OpenGL, no
Quartz Extreme, no hardware cursor, no 2D plug-in: those are for 10.4 only.

What you can do, safest first. Run these in Terminal, in this folder.
You need an administrator password. Turn on Remote Login (System
Preferences, Sharing) first so that you can get in over ssh if a screen
stays dark.

  1. sudo sh ./install.sh --check
       Asks the system whether it would load the kext (kextload -t -n).
       Writes nothing, loads nothing. If it prints "undefined symbol"
       lines or "rejects the kext", send me the whole output.

  2. sudo sh ./install.sh --try
       Loads the kext from /tmp without installing it. If the Mac panics,
       it is a one-off: a restart brings it back as before. Afterwards:
         grep RadeonNI /var/log/system.log
       The window server looks for framebuffers only when it starts, so
       log out and in again to see whether the card's monitor lights up.

  3. sudo sh ./install.sh
       Installs it into /System/Library/Extensions so that it loads at
       every boot. Only if 1 and 2 went well. To undo: sudo sh ./uninstall.sh
       (or Safe Boot with Shift held, or single-user mode Command-S, then
       rm -rf /System/Library/Extensions/RadeonNI.kext).

If the card's ROM has no address in Open Firmware, install.sh asks for a
VBIOS image of the card ("sudo sh ./install.sh /path/to/vbios.rom").

What to send back: the output of the commands above, the output of
"sw_vers" and "uname -a", the Mac model ("system_profiler
SPHardwareDataType"), and whether the monitor showed anything. The most
useful part is every line of the system log that starts with "RadeonNI:":
this build lists the connectors the card's VBIOS describes ("board: path
...") and says which one answered with an EDID. Also say which port the
monitor is on and whether it is a direct cable.

The driver is MIT-licensed (LICENSE). Nothing is ever written to the card.
