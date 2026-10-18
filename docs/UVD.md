# UVD (hardware video decode): feasibility, paper check

Read this before starting any work on video decode acceleration on the 7570. Status: **nothing
implemented and nothing run on the card**; every statement below is read from the sources named,
marked `[V]` verified in the file given, `[I]` inference. See also `docs/LEOPARD-VIDEO-DECODE.md`
(what Leopard itself offers, if that file exists).

## Why

PowerVLC 2.1.0 on the G5 decodes H.264 on the CPU (ffmpeg). 1080p H.264 drops nearly every frame
(journal 2026-10-17); 704x400 MPEG-4 plays. The GPU only converts colour and scales.

## What the card has

- Turks (HD 7570) has a UVD block (UVD 3.x on Northern Islands; MPEG-2, H.264, VC-1, MPEG-4
  part 2). [I] from Linux treating Turks like Cypress/Barts in `radeon_uvd.c`.
- Firmware: Linux loads `radeon/SUMO_uvd.bin` for `CHIP_TURKS` [V `radeon_uvd.c`, the `SUMO`
  case lists SUMO, PALM, CAYMAN, BARTS, TURKS, CAICOS]. On this host:
  `/lib/firmware/radeon/SUMO_uvd.bin.zst`, 130075 bytes compressed. Not in `firmware/` yet; it would
  go there like the other microcode (git-ignored, injected, shipped unmodified with its licence).

## What exists in Mesa 26.2.4 (`third_party/mesa-26.2.4`)

- `src/gallium/drivers/r600/radeon_uvd.c` and `r600_uvd.c` implement the decoder: they build the
  UVD message, feedback, bitstream, DPB and context buffers and submit them through the winsys.
  [V] `r600_pipe.c`: `create_video_codec` is set only if `info.ip[AMD_IP_UVD].num_queues != 0`.
- The decoder needs from the winsys: `cs_create(..., AMD_IP_UVD, ...)`, `cs_add_buffer`,
  `buffer_get_virtual_address`, `buffer_map`, `cs_flush`, `fence_wait`
  [V `radeon_uvd.c`]. Our winsys (`mesa/target/rdn_target.c`) has the graphics ring only.
- Mesa converts picture parameters (`pipe_h264_picture_desc`) to UVD messages itself; a program
  would feed it slices and parameters.

## What Linux `radeon` does to start UVD (the part we would port into `hw/`)

- `radeon_uvd_init`: firmware into a VCPU buffer. `uvd_v2_2_resume` (and `uvd_v1_0_resume`):
  program the memory-controller window (`UVD_LMI_ADDR_EXT`, `UVD_LMI_EXT40_ADDR`, firmware offset
  and size). `uvd_v1_0_start`: reset, `UVD_LMI_CTRL`, swap control, boot the VCPU, set up the ring
  (`UVD_RBC_*`). `uvd_v1_0_init`: ring test.
- Clocks: `evergreen_set_uvd_clocks` programs the UPLL (VCLK/DCLK dividers, 53300 and 40000 in
  10 kHz units while decoding, 10000 idle) [V `uvd_v1_0.c`, `evergreen.c`].
- Big endian: `uvd_v1_0.c` has `#ifdef __BIG_ENDIAN` setting `UVD_LMI_SWAP_CNTL` and
  `UVD_MP_SWAP_CNTL` [V]. So the engine itself can swap; messages from Mesa are little-endian
  structures that the big-endian host writes, as for the graphics ring (`mesa/patches/0006`,
  `0007`).
- Memory rule: message and feedback buffers must lie inside one 256 MB segment and relocations may
  not cross a 256 MB boundary [V `radeon_uvd.c`, lines ~600-615]. Our heap is 222 MB inside the
  256 MB aperture [V `rdnuc info`], so it fits if decode buffers come from there.

## What would have to be built

1. `hw/`: UVD bring-up (firmware, MC window, clocks via AtomBIOS PLL tables, start, ring test),
   behind `hw/rdn_os.h` like the rest.
2. Kext: a UVD ring, a user-client method to submit a UVD command buffer by GPU address, a fence
   for it, power/clock management so the UVD block does not stay clocked.
3. `mesa/target`: a UVD queue in the winsys (`info.ip[AMD_IP_UVD]`), so `r600_uvd` loads.
4. Mesa for Darwin: build the r600 video code (`radeon_uvd.c`, `vl`) into the bundle or a
   separate library; check whether `src/gallium/auxiliary/vl` builds on big-endian Tiger/Leopard
   (`bool` size rule in `CLAUDE.md`).
5. VLC side: ffmpeg's H.264 hwaccel hooks (VDPAU-style) or VLC's `vlc_va_t` interface (what
   VideoToolbox and VA-API modules implement), as a new PowerVLC plug-in that converts to
   `pipe_h264_picture_desc`, decodes into a video buffer, and hands the frame to the GL output
   as a texture (same card, no copy).

## Risks and unknowns

- The block may not start on this board without the right UPLL setup; nothing has read a UVD
  register on this card. Reads of UVD registers while the block is unclocked are not known to be
  safe; do not try on the host without the user's yes and `scripts/card-quiet.sh apply`
  (`CLAUDE.md`, host safety rules).
- Whether decode targets must be in VRAM or may be in GART memory [unknown].
- Performance: UVD3 decodes 1080p H.264 up to level 4.1 on Linux (VA-API users) [I]. UVD does
  the entropy decoding too, so the CPU would only parse headers and slice boundaries [I]; the
  PCIe x4 link carries just the bitstream.
- Effort: large (weeks), mostly steps 2 and 5.

## Suggested order

1. Extract `SUMO_uvd.bin` into `firmware/` and read its header (version, size) offline.
2. On the Linux tool path with the card quieted (user's yes first): clock the UPLL and boot the
   UVD VCPU with the firmware, read `UVD_STATUS`; stop there if it does not come up.
3. Ring test, then one H.264 I-frame message from Mesa's own `radeon_uvd.c` through a test
   program, compared with a CPU decode.
4. Kext and winsys work, then the VLC module.
