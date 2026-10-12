# g5-netboot: a rescue Linux for the G5's disk

Boots ArchPOWER's `linux-ppc64` kernel with a small initramfs that runs sshd
(key login only) from Open Firmware through GRUB, so the G5's disk can be
written while nothing on it is mounted. Tiger does not allow that from the
running system (`dd` to `/dev/rdisk0` says "Resource busy").

    scripts/g5-netboot/build.sh          # kernel + initramfs -> build/netboot/
    scripts/g5-netboot/grub.sh [secs]    # core.elf: GRUB with its config inside
    scripts/g5-netboot/qtest.sh          # try it in QEMU pseries; ssh -p 2260 root@127.0.0.1

On the G5 (2026-10-12): `/rdn/{core.elf,vmlinuz,initrd.img}` on the Tiger
volume; `nvram boot-device='hd:3,\rdn\core.elf'`; restart. To go back:

    nvram --update-config 'boot-device=hd:,\\:tbxi'; reboot -f      # from the rescue system
    sudo nvram 'boot-device=hd:,\\:tbxi'                            # from Tiger

(the value has two backslashes before the colon). Option at power-on also
picks a normal volume. Keys that may log in: `private/ssh/g5rescue.pub`,
`private/ssh/tiger_rsa.pub` and the building user's `~/.ssh/id_*.pub`.
The rescue system waits `rdn.wait` seconds for a first login and otherwise
tries to restore the boot device and restart; once someone has logged in it
stays up.

`craftmap.py` writes the partition map that adds the Leopard volume at block
67108864 (see docs/JOURNAL.md, 2026-10-12).
