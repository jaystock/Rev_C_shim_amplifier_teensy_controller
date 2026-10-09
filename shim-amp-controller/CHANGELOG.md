# Changelog

## 2026-10 — "no reply" marks for unanswered ADC reads

- `I` and `A` print `no reply` for channels whose ADC read got no answer
  (12-bit version), instead of a misleading -0.625 A. A board whose fiber
  cable is on the wrong port of the fiber-optic interface board shows up as
  eight `no reply` lines with the same slot.
- Calibration reports `failed (no reply from ADC: check this board's fiber
  port)` for those channels instead of `failed (gain)`; `S(b,c)` prints
  `no-reply` for unanswered readings.

## 2026-10 — 12-bit: no more freeze when the ADC doesn't answer

- `readAdcCode()` waited forever for the readback word on SPI1. If nothing
  arrived (Teensy tested on its own, boards unpowered, readback cable
  disconnected), `I`, `A`, `C` and `S(b,c)` froze the controller. Reads now
  give up after 5 ms, and the command ends with a warning that its readings
  are invalid.
- `kinetis_spi.h` now enables the SPI1 receive interrupt after restarting
  the port, the same order T3SPI used.
- `spi1_isr` is declared `extern "C"` so the interrupt vector always finds it.

## 2026-10 — 12-bit: built-in SPI driver replaces T3SPI

- New `kinetis_spi.h` in the 12-bit folder replaces the modified T3SPI
  library, which had no license and so couldn't be published here. Nothing
  needs installing to build either version.
- The driver sets the same final SPI register values, pin settings and
  transfers as the modified T3SPI; checked in a register-level simulation,
  not yet on hardware.
- Bit order: the old code requested `LSB_FIRST`, but T3SPI compared that
  value against Arduino's `MSBFIRST`, so the bus ran MSB first (as the DAC
  and ADC need). The new driver sets MSB first explicitly; behaviour is
  unchanged.

## 2026-10 — 12-bit board is Teensy 3.5

- 12-bit folder renamed `revC_12bit_teensy35`; README and comments said
  Teensy 3.2, which has too little RAM for the stored sequence (160 KB).
- README notes that the 12-bit version needs a modified T3SPI library.

## 2026-10 — site settings in config.h, trigger selection

- New `config.h` in each version holds every site setting: trigger source,
  board map, channels used, readback scaling, calibration limits, serial
  options. `hardware.h` now holds only pins and drivers.
- Trigger source is one line, `TRIGGER_SOURCE TRIGGER_FIBER` or
  `TRIGGER_BNC`, with `BNC_ECHO` for the fiber case. Both versions support
  both. An invalid combination stops the build; the active trigger is
  printed at start-up.
- The `.ino` is now identical in both versions, like `util.h`.

## 2026-10 — cleanup and calibration fixes (both versions)

The first commit in this repository holds the code exactly as it was before
these changes, so `git diff` against it shows everything below.

### Calibration fixes

- **12-bit: the offset correction was never verified.** The correction loop
  had been cut to one pass, so it computed a correction, wrote it, and then
  reported "failed (cal)" and discarded it. Any channel that needed correcting
  failed. Now it iterates (measure, correct, settle, re-measure) up to
  `CAL_MAX_ITER` times.
- **Settling time.** The gain measurement waited only 1-2 ms after each DAC
  step. All waits now use `CAL_SETTLE_MS` (40 ms by default).
- **Averaging never happened.** `LTC1863ReadSlow(addr, n)` overwrote `n` with 1.
  Measurements now average `CAL_N_AVG` readings.
- **Bad gains were kept.** After "failed (gain)" the measured gain (e.g. -0.3
  or about 0) stayed in use, so later current commands on that channel were
  far off. Failed channels now keep the nominal gain.
- **Gain measurement used channel (0,0)'s zero point** for every channel
  (12-bit). It now uses the channel's own.
- `abs()` on floats replaced by `fabsf()`.
- Trigger interrupts are switched off during calibration and the sweep, so a
  trigger can't rewrite DACs or change the board select mid-measurement.

### Other fixes

- **DAC codes are clamped** to 0-65535. Converting an out-of-range float to
  `uint16_t` is undefined behaviour and could send a random value to the DAC
  (possible with a bad gain, or `Track` near +/-4 A).
- **Raw ADC codes printed in base 4** (16-bit): `Serial.print(data, 4)` on an
  integer means base 4, not 4 decimals.
- **Uploads via `uploadcoef` could lose data** (16-bit): `serialEvent()` could
  read the binary data as command text. Serial input now pauses during uploads.
- **Header upload string not terminated** (12-bit): `readBytesUntil` doesn't
  add a terminating zero, so a shorter header could pick up leftovers from a
  previous one. Malformed headers are now rejected instead of crashing.
- **Uploads larger than `coefStore`** are truncated with a warning instead of
  overwriting memory.
- **48 debug prints per trigger removed** (16-bit): `coefStoreAsMat()` printed
  every coefficient from inside the trigger interrupt.
- **`E` no longer calls `setup()`** (which on the 12-bit board would create a
  second set of SPI objects). It now clears calibration, restarts the sequence
  and sets outputs to 0 A.
- Board/channel indices from serial commands are range-checked.
- 12-bit: byte `0x01` fell through into the `Z` case because of a missing
  `break`. The zeroing is kept on purpose and is now explicit.

### New features

- Same command set on both versions, ending in a newline:
  `C`, `C(b,c)`, `S(b,c)`, `I`, `A`, `Z`, `M`, `T`, `E`, `H`, plus the host
  commands `Icurrentall`, `Ichannel,b,c`, `Track,b,c,amps`, `uploadcoef` and
  the `0x01` header upload. (The 16-bit `D` command is now `C`.)
- Channels are numbered #1-#48 in board order in all listings, with board,
  channel and physical slot on each line.
- `S(b,c)` step-response sweep for diagnosing gain, settling and readback.
- Calibration prints one diagnostic line per channel and a pass count.
- `H` prints the command list.

### Code organisation

- `util.h` is now identical in both versions; hardware differences are in
  `hardware.h`, and readback scaling is set by named constants there.
- The stored sequence moved to `coefs.h`. In this repository it is a single
  row of zeros (all outputs 0 A); paste real data into `coefs.h` or upload
  it over serial. The original tables were also blanked in the first commit.
- `LTC1863ReadSlow()` renamed `readAdcCode()` (the 16-bit board has an LTC1867).
- Unused variables and dead code removed; comments added throughout.
