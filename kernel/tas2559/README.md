# TAS2559/TAS2560 Chiron port — experimental

Original source: LineageOS/android_kernel_xiaomi_msm8998 commit
`7231883a368604f90806e167890e00f515a9927c`, `sound/soc/codecs/tas2559/`.
The TI/Xiaomi GPLv2 notices remain in the source files.

## Verified

- Core, regmap and codec sources compile against Linux 6.18 for arm64.
- ASoC component, I2C remove, hrtimer and firmware-request APIs migrated.
- Separate I2C clients/regmaps replace mutation of a client's address.
- Container preflight rejects truncated strings, sections and burst commands.
- Actual vendor parser tested with synthetic inputs and the phone's firmware
  and calibration, including every allocation failure under ASan/UBSan.
- Separate `tas2559-pgid.c` diagnostic on `6.18.58-chiron-audio10` read
  TAS2559 PGID 0xa0 and TAS2560 ID 0x00 after a GPIO41 reset pulse. It returned
  GPIO41 low and was unloaded. No firmware programming or playback occurred.

## Not yet safe to load as a playback driver

The original full driver's probe retains its `-EOPNOTSUPP` guard. A separate,
default-off `diagnostic_program` module parameter selects a one-shot diagnostic
instead; it does not enable or fall through to the original probe.
Do not remove the guard to enable the original probe wholesale. IRQs now use a real
thread, initially disabled requests, mutex-protected enable depth, and separate
wanted/processing/removing state. Suspend masks and synchronizes IRQs before
work cancellation; teardown blocks rearming. These paths still need in-kernel
concurrency, suspend and unload tests, not just host-model tests.

Calibration compatibility now checks the current program and configuration's
program, not equality with the calibration configuration metadata. Stock
metadata config 0 can therefore accompany program-0 playback config 4 without
rewriting the blob. The loaded flag is set only after successful coefficient
application; deferred selection is not a successful application. This does not
establish acoustic safety for every configuration sharing program 0.

The block executor now accepts DevB coefficient blocks and validates command
layout before I/O. Mocked I/O/CRC tests are not hardware P/Y CRC validation.
GPIO/rail assumptions, power sequencing, actual hardware CRC and QUAT MI2S
integration still need hardware validation. The new guarded probes use the DTS
`reset-gpios` descriptor; only the blocked legacy parser still expects
`ti,tas2559-reset-gpio`. Do not enable legacy startup/hw_params paths.

`container-test.c` is an offline structure test, not a hardware safety test.
The container header checksum has not been authenticated; the vendor hardware
P/Y CRC programming checks are a separate step. Firmware structure validation
does not establish that registers or calibration are appropriate for a speaker.

`tas2559-pgid.c` is a separate reset-and-read diagnostic, not part of the module
Makefile below. It reads IDs without power-control or unmute writes, then holds
RESET_N physically low. It must be compiled for the exact running test kernel.

## One-shot programming diagnostic (passed on the phone)

`diagnostic_program` defaults to false and is read-only after module insertion.
Do not enable it without a separately confirmed hardware-write scope. The
separate probe uses `reset-gpios` through a descriptor with explicit raw physical
levels; it publishes no ASoC component, IRQ, timer, or recovery work. It requires
both local firmware files before releasing reset, checks the stock layout and
all selected commands, and calls only the block executor, not legacy startup,
set_program, failsafe or unmute.

The sequence targets program 0, PLL 0, config 4 and calibration 0. It skips the
DevB post-power-up block. It explicitly writes output mute/power-down controls,
then programs the seven selected blocks with available P/Y checks. Blob writes
are constrained by `tas2559-program-policy.h`; this is a device-specific write
policy, not firmware authentication or proof of electrical safety. In particular,
the stock image writes gain, device-mode, boost configuration and special pages.
Calibration has no stored P/Y checksum. Its eleven exact destinations (44 bytes)
are written and immediately compared byte-for-byte, without logging private
values. B140/P42/R84 lies outside vendor YCRC; do not claim calibration CRC.

The sequence checks power/mute registers afterwards and attempts physical reset
low on every post-reset exit, including success; reset failures are reported.
It deliberately discards the programmed state. Host fault tests mock hardware,
so neither a green host test nor a build proves that hardware stays off. The
legacy playback path still needs separate review and validation.

Transport selector errors now invalidate signed book/page caches and stop the
transaction. DevBoth write/bulk/update preserve the first failure instead of
masking it with the second device's success.

## Controlled playback (verified on the phone)

`controlled_playback=1` selects a separate minimal ASoC component in
`tas2559-playback.c`. It defaults off and is mutually exclusive with
`diagnostic_program=1`. Neither option enters the legacy probe. Probe parses
firmware while reset is held low; PCM prepare validates/programs the selected
stock configuration and verifies calibration readback while outputs remain off.
The DevB post-power block is preflighted before reset is released.

Load-time parameters (defaults keep the original laboratory limits):

| Parameter | Default | Meaning |
|---|---|---|
| `gain` | 0 | DAC gain code for both amplifiers, 0..15. Stock uses 15 (6 below 0 °C). |
| `play_limit_ms` | 5000 | Stop the amplifier this long after power-up; 0 = no limit. |
| `reopen` | off | Allow further opens/prepares after a stop; each re-programs and re-checks. |

- Only ASI1 playback, 48 kHz, stereo, S16_LE; no capture or mixer controls.
- The gain code is written and read back before power-up. It is not a
  calibrated SPL limit; use software attenuation (`/etc/asound.conf` softvol).
- Without `reopen`, one open attempt per binding: stop, error, timeout or
  suspend after prepare consumes it. Suspend before prepare preserves it. With
  `reopen`, a later open, `hw_params` or prepare (e.g. underrun recovery)
  stops a running amplifier first and then programs it from scratch.
- Power-up remains muted, post-power coefficients and fault/power status are
  checked before unmute. I/O errors immediately take the stop/reset path, with
  no legacy retries, recovery work or automatic power restoration.
- A 20 ms status polling work item stops on faults or, with a nonzero
  `play_limit_ms`, at the deadline measured from the start of power-up, and
  logs why. This is a **software** check, not a hard electrical cutoff:
  scheduling stalls or blocked I2C can delay it. No protection IRQs are used.
- PCM shutdown/hw_free, suspend, driver cleanup and system shutdown request mute,
  power-down and physical reset low; reset failure is explicitly reported.
  Cleanup drains work without holding its mutex and releases parsed firmware
  before devres releases the bus/reset resources.

On the phone (2026-10-27): the programming diagnostic passed (P/Y checks,
44-byte calibration readback, reset held low afterwards). After machine patch
0028 the speaker played audibly; a full 3:45 song played with `gain=15
play_limit_ms=0 reopen=1` without any fault stop. Loudness and level stability
at gain 15 are still being evaluated.

Offline tests extract the actual callbacks and programmer and mock register
I/O (startup I/O failures, both programmer modes, corrupted calibration
readback, gain/status/post-block failures, format rejection, one-shot state,
concurrent opens, timeout, suspend, shutdown, reset failure). They predate the
`gain`/`play_limit_ms`/`reopen` parameters and have not been extended to them.

`chiron-quat-experimental.dtsi` is not included by the repository's base Chiron
DTS: with it, the whole card defers until this component is loaded. The
playback mode must not be selected with an unreviewed firmware file merely
because its layout matches.

## Build

Copy sources to a new build directory and use the matching configured kernel:

```sh
make -C /path/to/matching/kernel M=/path/to/module-copy \
  ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- LOCALVERSION=-audio10 modules
modinfo -F vermagic /path/to/module-copy/snd-soc-tas2559.ko
```

A successful build is not a successful probe, playback or recording test.
Never force-load a mismatched module. Firmware and per-phone calibration are
private, Git-ignored artifacts and must not be committed with this source.
