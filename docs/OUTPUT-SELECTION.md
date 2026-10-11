# Choosing the connector automatically: research

Read this before changing `rdn_output_detect()`. Written 2026-10-11 from the
Linux `radeon` sources in `third_party/linux` and this repository. **Research
only: none of the proposal below is implemented**, and none of it has been
run on a board other than the 7570's.

## What the driver does today

`rdn_output_detect()` (`hw/rdn_modeset.c`) builds a list and takes the first
entry that answers with an EDID: (1) DVI-I as the 7570 board has it, (2) the
"DisplayPort" lines of that board, over AUX, (3) the same lines as TMDS over
plain DDC, (4) every digital connector the VBIOS's object table gives, minus
what was tried. It is sequential and uses no hot-plug sense to choose: a
connector with no monitor costs a failed DDC read, a DisplayPort one up to
three seconds (`rdn_dp_detect()` waits 3 s while the HPD line reads high, 1 s
while low). Only one output is ever driven (CRTC 0).

## What the VBIOS and the chip give (the "link")

A connector is a *display path* in `Object_Header` (`board_outputs_read()`):

| Item | Where | Used for |
|---|---|---|
| connector object id, enum | path's `usConnObjectId` | type (DVI-I/D, HDMI, DP, VGA), which of several of a kind |
| device tag | path's `usDeviceTag` | which encoders may drive it (Linux links encoder and connector by tag overlap, `radeon_link_encoder_connector()`) |
| encoder object ids | path's graphic object ids | UNIPHY0/1/2 for digital, DAC1/2 for analog; the enum of the UNIPHY object is the **link** (enum 2 = link B) |
| DDC line | connector object's `ATOM_I2C_RECORD` (`ucAccess`) | EDID over I2C, or the AUX channel for DisplayPort |
| hot-plug pin | connector object's `ATOM_HPD_INT_RECORD` -> `GPIO_Pin_LUT` entry | `DC_HPD1..6`: sense and the `ucHPD_ID` the transmitter commands take |
| router objects, `ENCODER_CAP` record | other tables | DDC/HPD muxes (mostly notebooks); DP link rate caps. Not read. |

On DCE5 the digital encoder is fixed by the transmitter and link: UNIPHYn
link A is DIG 2n, link B is DIG 2n+1 (`radeon_atom_pick_dig_encoder()`). The
CRTC is free: any of the six can feed any DIG through `SelectCRTC_Source`
(the DRM core picks; ours uses 0).

## What Linux does to decide a connector has a display

`radeon_dvi_detect()` / `radeon_dp_detect()` (`radeon_connectors.c`):

1. **Hot-plug sense** (`evergreen_hpd_sense()`: `DC_HPDx_INT_STATUS & SENSE`)
   decides whether to look at all; a connector whose HPD did not change is not
   probed again.
2. **DDC probe**, then the EDID. If HPD said "plugged" but DDC fails, try
   again after a second ("the pins for DDC do not make contact at the same
   time as HPD").
3. **Digital or analog** from the EDID's input bit. An analog EDID on a DVI-I
   leads to the DAC path (load detection), not TMDS.
4. **Shared DDC**: boards with DVI-D + HDMI, or VGA + HDMI, share one DDC line
   (`shared_ddc`, set when two connectors have the same I2C id). With both
   digital "HPD is our only option": the EDID answers on the shared line
   whichever port the monitor is on, and only that port's HPD tells which.
5. **Analog** (VGA, DVI-I's analog half): `DAC load detect`, only when forced,
   as it is destructive.
6. **Board quirks** (`radeon_atom_apply_quirks()`): subsystem-ID fixes for
   connector types and lines. None of the entries I read is for an Evergreen
   or Northern Islands device ID.

For DisplayPort: sink type from the BIOS (`DPEncoderService`, GET_SINK_TYPE,
a connector object id), then DPCD over AUX; anything that is not a DP sink
is handled as DVI/HDMI over DDC.

## Proposed algorithm (not implemented)

1. Keep building the candidate list as now. Keep the built-in 7570 entries
   first, so that board's behaviour is unchanged (the object table agrees
   with them: `tests/board_outputs.c`).
2. **Sense first.** For every candidate whose hot-plug pin is known, enable
   it (`rdn_dp_hpd_init()`) and read the sense bit after a settling time that
   has to be measured (the code programs a connection timer, 0x9c4; its unit
   was not looked up). Candidates whose
   pin is unknown (no HPD record, GPIO register is not `0x64b4`) stay in the
   list as "unknown".
3. **EDID for the plugged ones**, then for the unknown ones, in table order;
   DisplayPort by AUX, the others by DDC. Never probe an unplugged DisplayPort
   (saves the 1 s to 3 s wait per connector).
4. **Shared lines**: if two candidates have the same DDC id and both are
   digital, accept an EDID only from the one whose HPD reads high; with no HPD
   on either, take the first and log that it is a guess.
5. **Choose**: with one monitor, that connector; with several (a later
   extended-desktop step, `EXTENDED-DESKTOP-TODO.md`) keep them all and give
   each its CRTC.
6. **Log the decision** exactly as today (`board: path ...`, `EDID found`),
   plus the sense bit of every candidate, so a report shows the wiring and
   what the pins read.

## Things this does not solve

- **VGA and the analog half of DVI-I** need the DAC path: `SelectCRTC_Source`
  to DAC1/DAC2, `DAC1EncoderControl`, `DAC1OutputControl`, load detection.
  Not started. A 6670 with only a VGA monitor cannot be driven.
- **Dual-link** (above 165 MHz: 2560x1600) needs link A and B of one UNIPHY
  together (`fDualLinkConnector`); the driver is single-link. 1920x1200 at
  60 Hz reduced blanking is 154 MHz and fits.
- **DVI/HDMI adapters on a DisplayPort connector** (DP++) answer as DDC on the
  same connector; the "twin" candidate covers that only where the lines match.
- **Router objects** and external bridges (DP to VGA) are not read.
- **A wrong table**: boards with a wrong object table need a quirk keyed on
  the subsystem ID; there is no way to know in advance.
- **Hot-plug between connectors** reuses `rdn_output_detect()` from the kext
  (`monitorReturned()`); with sense-first it would also need to poll the
  sense of the other connectors, which today it does not.

## What would settle the open points

The 6670 report: the `board: path` lines (which UNIPHY, link, DDC line and
pin its DVI port uses; whether DDC is shared with another connector; whether
an analog path exists), and the sense bit of each candidate if the next build
logs it. A ROM dump of that card (`kext/RomProbe`, needs Xcode, or the
VBIOS file) would let `tests/board_outputs.c` check the parse against a
second board; it would live in `private/`, never in the repository.
