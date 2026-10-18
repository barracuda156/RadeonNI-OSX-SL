# Video decode acceleration on Leopard PPC

Read this when someone asks whether PowerVLC (or anything else) can decode
H.264/MPEG-2 on the Radeon HD 7570 under Mac OS X 10.5.9. Findings of
2026-10-17, read-only checks on the G5 (10.5.9, 9L31b).

## Verdict per candidate

| Candidate | Verdict | Why |
|---|---|---|
| VideoDecodeAcceleration.framework | Not usable | Absent (not in `/System/Library/Frameworks`); it appeared in 10.6.3 and only for NVIDIA 9400M/320M/GT330M on Intel. |
| `AppleVA.framework` (private, "Apple Video Acceleration") + `AppleVAH264HW.component` | Present, not usable for our card | Real GPU decode path, see below. Private, undocumented, and keyed to Apple's own GPU drivers. |
| QuickTimeH264.component (software AVC) | Usable but CPU only | No GPU decode. Same CPU path as before. |
| `ATIRadeon*VADriver`, `ATIRadeon*DVDDriver`, `GeForceVADriverPPC` bundles | Not usable | Old Radeon (8500/9700/X1000) and GeForce renderers for DVD Player's MPEG-2 (shader/CPU assisted). Our card has no such driver. |
| CVOpenGLTexture / GL_APPLE_ycbcr_422 / client storage | Usable (offload only) | Colour conversion and scaling on the GPU, no decoding. Needs our GL to expose the extensions (extension list is the user's decision). |
| Other PPC projects | No GPU decode known | Software ffmpeg with AltiVec only. |

## Evidence

- `/System/Library/PrivateFrameworks/AppleVA.framework` exists (links only
  Cocoa, IOKit, ApplicationServices; no OpenGL). It exports
  `_AVAFCreateRenderer`, `_AVAFGetGPURenderer`, `_AVAFQTInitDecoder`,
  `_AVAFQTDecodePicture[Async]`, `_AVAFDeinterlacer*`, `_AVA_HDCreateDecoder`
  (source path in strings: `AppleVA-4.1.17/AVA_HD`).
- `/System/Library/QuickTime/AppleVAH264HW.component` links QuickTime,
  OpenGL, QuartzCore, CoreVideo, IOKit. Strings show H.264 slice decoders
  for specific hardware: `create_slice_dec_ATI_UVD`,
  `create_slice_dec_NVIDIA` (VP3 film grain), GEN4/VP1 variants. So the
  code is the Snow Leopard style engine (ATI UVD, NVIDIA PureVideo, Intel)
  backported in 10.5.x; whether it ever activates on PPC is unproven.
- The renderer comes from a per-GPU `*VADriver.bundle` in
  `/System/Library/Extensions`, each exporting `AVACreateRenderer`,
  `AVAGetRendererInfo`, `AVA_HDCreateHWRenderer`. Installed:
  ATIRadeon8500/9700/X1000 VADriver, GeForceVADriverPPC, AppleVADriver
  (AltiVec `AVA_HD_P4/P5` and Intel `I4/I5/x86` dylibs). `AppleVADriver`'s
  own check warns "neither ALTIVEC nor VSP renderer bit is set": the PPC
  fallback is AltiVec on the CPU.
- The ATI UVD path talks to Apple's closed ATI driver through IOKit
  user-clients (`attached-gpu-control-path`). Our kext has no UVD support
  (UVD3 firmware, ring, VCPU), so there is nothing to call even if a bundle
  matched.
- PowerVLC 's `plugins` has `libavcodec_plugin`, `libmacosx_qt_plugin`,
  `libqtcapture_plugin`; no VDA/AppleVA module.
- Public: AppleVAH264HW appeared with the Oct 2008 NVIDIA 9400M MacBooks and
  copying it elsewhere did nothing
  (https://www.macg.co/news/voir/132268/quicktime-le-fichier-mysterieux ,
  https://www.engadget.com/2008-10-21-new-macbooks-adding-nvidia-hardware-h-264-decoding.html);
  Snow Leopard API limited to those chips
  (https://forums.macrumors.com/threads/snow-leopard-h-264-hardware-acceleration-and-opencl-requirements.715966/);
  PPC forum consensus: no H.264 hardware, VLC/MPlayer AltiVec ffmpeg beats
  QuickTime (https://forums.macrumors.com/threads/1268445). No source found
  of anyone getting GPU decode on a PPC Mac.

## What it would take

1. Cheap, real: make PowerVLC's Mac video output use the GPU for YUV->RGB
   and scaling (GL_APPLE_ycbcr_422 / client storage, or a shader on our
   r600 Mesa). Needs the output plugin to upload planar/422 and our GL to
   offer the path. Saves CPU, not decode work.
2. Hard, speculative: real decode on UVD3 (Turks has UVD 3.0). Needs
   firmware (not redistributable; the project forbids committing it), a UVD
   ring/VCPU bring-up in the kext, a bitstream-to-hardware API (VA-API-like
   userland), and a PowerVLC module. Nothing in Leopard helps; the AppleVA
   private ABI would still have to be reverse-engineered and would match
   none of our hardware. Not planned.
