# Chiron audio bring-up — current status

Updated 2026-11-01.

## Evidence ladder

| Stage | Status |
|---|---|
| ADSP / APR / AFE / ASM / ADM, SLIMbus NGD, WCD9335, ALSA card | Verified on the phone |
| TAS2559/TAS2560 identity after GPIO41 reset | Verified (0xa0 / 0x00) |
| TAS programming: program 0, PLL 0, config 4, P/Y checks, 44-byte calibration readback | Verified on the phone |
| DSP → QUAT MI2S data flow (`hw_ptr` advances, buffers complete) | Verified after patch 0028 |
| Audible bottom speaker playback, full 3:45 song, no amplifier fault stops | Verified on the phone (listened) |
| Volume keys → software volume, level remembered | Verified on the phone |
| Loudness/stability at stock gain 15 | Under evaluation |
| Earpiece/headphones (WCD9335 SLIMbus), recording | Not working: DSP rejects starting SLIMBUS_6_RX (`error[9]`) |
| Automatic ADSP start / module load at boot (mix2-audio.service) | Done |
| Seeking in a player (VLC) | Works through dmix (see Userspace) |
| Desktop sound server | Not done (plain ALSA + dmix) |
| Permanent boot deployment | Done 2026-11-01 (kernel 6.18.58-chiron-disp2 in the boot partition) |

## Root causes fixed

- **No sound, DSP never consumed data** (`hw_ptr` stuck at 0, `aplay` EIO
  after ~0.5 s, every APR command returned success). q6afe reads the CPU DAI
  format from the DAI's own side; the machine driver passed `CBC_CFC`
  (== `BC_FC`), which configured QUAT MI2S RX for an *external* word select.
  The TAS2559 is a clock consumer as well, so nothing clocked the port.
  Patch 0028 sets `BP_FP` on the CPU DAI and `BC_FC` on the codec DAI, as
  sdm845 does. Found with an APR packet trace: the first `ASM_DATA_CMD_WRITE_V2`
  only completed after the port was stopped.
- **Very quiet output.** The controlled playback component forced DAC gain code
  0. The stock driver uses 15 (6 only below 0 °C). The gain is now a module
  parameter, written and read back before unmute.

## Kernel pieces

- `kernel/chiron.config`: the audio stack is built in (`=y`), as on the tested
  kernels; `CONFIG_QCOM_PD_MAPPER` stays off so userspace pd-mapper serves the
  phone's `adspua.jsn`. Keep the APR protection-domain properties.
- Patches 0023–0028: APR audio services and SLIMbus DT, MSM8998 machine driver
  and its fixes, QUAT clock constraint/unwind, QUAT clock direction.
- `kernel/msm8998-xiaomi-chiron.dts`: sound card, WCD9335 on SLIMbus, TAS2559
  node on `blsp2_i2c4`.
- `kernel/tas2559/chiron-quat-experimental.dtsi`: QUAT MI2S link (GPIO58 BCLK,
  59 WS, 60 SD0; `qcom,sd-lines = <0>`). `scripts/build_kernel.sh` appends it
  to the DTS; the whole card defers until the TAS playback component is loaded.
- `kernel/tas2559/`: the amplifier module (out of tree), see its README.

`scripts/build_kernel.sh` appends the QUAT fragment and `kernel/local.dtsi` to
the DTS, builds `kernel/tas2559` (and `kernel/mix2-pon`) against the same tree
(`make M=...`) and puts them in `/lib/modules/<release>/updates/` of the
module tarball. Each test kernel gets its own release name and its own
complete module directory: never reuse a release name for a different build
(mismatched modules broke the display once).

## Running it (manual for now)

1. Boot the audio kernel; start ADSP after rootfs is up (find the remoteproc
   whose `name` is `adsp`, write `start` to its `state`).
2. `modprobe snd-soc-tas2559 controlled_playback=1 gain=15 play_limit_ms=0 reopen=1`
   (auto-loading is blacklisted on the command line).
3. `amixer -c 0 cset name='QUAT_MI2S_RX Audio Mixer MultiMedia1' on`
4. Play through the ALSA default device, e.g. `mpg123 -o alsa -a default song.mp3`.

## Userspace (overlay/)

- `/etc/asound.conf`: `default` is plug → softvol `Master` → dmix →
  `hw:chironwcd9335,0`; plug converts to the only format the speaker path
  takes (48 kHz, stereo, S16_LE). dmix (period 1920, buffer 7680 frames)
  keeps the DSP stream running across a player's flush/refill: without it,
  VLC restarted the stream after a seek with less than one period queued,
  hit an underrun at once and stayed silent. It also lets several programs
  play at once.
- `mix2-volume up|down|mute|init`, bound to the volume keys
  (`XF86AudioRaiseVolume`/`LowerVolume`/`Mute` in skel). udev runs `init` when
  the card appears and creates `Master` at the saved or a quiet level before
  softvol could create it at full volume. The level is kept in
  `/var/lib/mix2/volume`; `alsa-restore` also restores it. One popup is updated
  in place instead of stacking.
- No PulseAudio/PipeWire yet: they hold the device open, which the first
  amplifier component versions could not tolerate.

## Known issues

- After the sound card is torn down (e.g. unbinding the TAS), it cannot be
  re-instantiated without a reboot: WCD9335 does not release IRQ 145.
- SLIMbus (WCD9335) playback: the DSP rejects starting AFE port 0x400c.
- A SLIM QMI timeout appears after enumeration.
- The fault/status polling is a 20 ms software check, not a hardware cutoff;
  no protection IRQs are used.

## Next

Confirm loudness and level stability at gain 15; start ADSP and load the
amplifier at boot; non-debug release kernel with 0028; desktop player and
sound server; longer stability run; then permanent boot deployment with a
rollback image. Do not commit firmware or calibration; do not publish boot
images or DTBs built with `kernel/local.dtsi`.
