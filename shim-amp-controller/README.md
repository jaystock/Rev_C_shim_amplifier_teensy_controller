# Shim amplifier controller

Teensy firmware for a 48-channel shim-coil current amplifier. It:

- plays a stored sequence of shim currents, advancing one row per trigger pulse,
- calibrates each channel's gain and zero-current (leakage) offset,
- reports channel currents over USB serial for people and for host software.

## Versions

| Folder | Board | Readback ADC | Default trigger | Notes |
|---|---|---|---|---|
| [`revC_12bit_teensy32/`](revC_12bit_teensy32) | Teensy 3.2 | LTC1863, 12-bit | BNC | Rev C amplifier. Needs the T3SPI library. |
| [`revC_16bit_ltc1867_teensy41/`](revC_16bit_ltc1867_teensy41) | Teensy 4.1 | LTC1867, 16-bit | Fiber, echoed on BNC | Uses the standard SPI library. |

Both versions accept the same serial commands, and either can use the fiber or BNC trigger.

```
<version>/
├── config.h        SITE SETTINGS: trigger, board map, readback scaling  <- edit this
├── <version>.ino   setup, serial commands, trigger handling   (same in both)
├── util.h          calibration, conversions, readback, sequencing  (same in both)
├── hardware.h      pins, SPI, DAC/ADC drivers                 (version-specific)
└── coefs.h         stored current sequence                    (version-specific)
```

The `.ino` and `util.h` are identical in the two folders. If you change one, copy it to the other folder as well.

## Building

1. Install the [Arduino IDE](https://www.arduino.cc/en/software) and [Teensyduino](https://www.pjrc.com/teensy/td_download.html).
2. 12-bit version only: install the T3SPI library for Teensy 3.x.
3. Edit `<version>/config.h` for your setup (see below).
4. Open `<version>/<version>.ino`. The folder name must match the `.ino` name, so keep them together.
5. Select the board (Teensy 3.2 or Teensy 4.1) and upload.
6. Open the Serial Monitor at **115200 baud** with line ending set to **Newline**, and type `H`.

## Site configuration (`config.h`)

All installation-specific settings are at the top of each version's `config.h`:

**1. Trigger source.** Set one line:

```cpp
#define TRIGGER_SOURCE TRIGGER_FIBER   // fiber-optic receiver, pin 6
// or
#define TRIGGER_SOURCE TRIGGER_BNC     // BNC connector, pin 4
#define BNC_ECHO       1               // fiber only: BNC pin changes state on each trigger
```

The sequence advances one row on each falling edge. `BNC_ECHO 1` with `TRIGGER_BNC` stops the build with an error, since the BNC can't be an input and an output at once. The active trigger is printed at start-up.

**2. Boards and channels.** `NUM_B`, `boardMap` (logical board -> physical backplane slot), and `channels_used` (which channels map to the columns of the stored sequence).

**3. Readback scaling.** `SENSE_AMP_GAIN`, `SENSE_R_OHM`, `SENSE_OFFSET_V`, from `I = (Vadc - SENSE_OFFSET_V) / SENSE_AMP_GAIN / SENSE_R_OHM`. These depend on the amplifier board revision (10 in the 12-bit defaults, 1.8 in the 16-bit defaults). Verify with `S(b,c)` and a clamp meter.

**4. Calibration limits.** Nominal gain (-1.62 A/V), allowed gain deviation, and offset tolerance (2 mA).

**5. Serial behaviour.** `TRIGGER_VERBOSE` and `LEGACY_BARE_COMMANDS` (see below).

Algorithm settings such as settle time and averaging (`CAL_SETTLE_MS`, `CAL_N_AVG`, `CAL_MAX_ITER`) are at the top of `util.h`.

## Serial commands

Commands end with a newline. Board `b` is 0-5, channel `c` is 0-7.

| Command | Action |
|---|---|
| `C` | Calibrate all channels, #1 to #48, then print "Calibrated N / 48" |
| `C(b,c)` | Calibrate one channel, e.g. `C(2,5)` |
| `S(b,c)` | Step-response sweep of one channel (diagnostic, see below) |
| `I` | List channel currents #1 to #48 with gain and calibration status |
| `A` | Raw readback for every channel: ADC code, current, ADC voltage, gain |
| `Z` | Set all channels to 0 A |
| `M` | Restart the sequence and load row 1 |
| `T` | Advance the sequence one row (same as a trigger pulse) |
| `E` | Reset: forget calibration, restart sequence, all outputs to 0 A |
| `H` or `?` | Command list |

Commands for host programs (output formats are stable, so scripts can parse them):

| Command | Response |
|---|---|
| `Icurrentall` | `Icurrentall`, then one `b c I gain T/F` line per channel, then `IcurrentallEnd` |
| `Ichannel,b,c` | `Icurrent b c I gain T/F` |
| `Track,b,c,amps` | Sets one channel (-4 to 4 A), then replies like `Ichannel` |
| `uploadcoef` | Replies `Ready to receive coefStore...`; then send `channels x sum(lengths)` little-endian float32 values |
| byte `0x01` | Sets outputs to 0 A, then send a header such as `c48\|b2\|l96\|2\|r10000\|3\|` followed by a zero byte, then the float32 data |

Uploaded sequences are kept in RAM only and are lost on power-down. The `coefs.h` in this repository holds a single row of zeros, so until you paste in or upload a sequence, every row outputs 0 A.

**Commands without a newline.** The 12-bit defaults set `LEGACY_BARE_COMMANDS = true`, so a bare `T` with no newline still runs after 100 ms, for older host programs. A terminal that sends each keystroke as you type (e.g. PuTTY) may then run `C` before you finish typing `C(0,3)`; set it to `false` if that's a problem.

## Calibration

For each channel the firmware:

1. sets the DAC to 2.0 V and 2.5 V, waiting `CAL_SETTLE_MS` and averaging `CAL_N_AVG` readings at each, and computes the gain (expected about -1.62 A/V);
2. if the gain is within `CAL_GAIN_TOL`, repeatedly measures the current at the 0 A setting and shifts that channel's zero point until it's within `CAL_TOL_A` (2 mA by default).

Trigger interrupts are switched off during calibration. Example output:

```
Ch  9/48  (1,0)  slot 1  I@2.0V=0.8102  I@2.5V=0.0031  gain=-1.6142  it0:0.0031  it1:0.0004  zp=-0.00192  -> ok
Ch 10/48  (1,1)  slot 1  I@2.0V=-0.3442  I@2.5V=-0.5289  gain=-0.3694  -> failed (gain)
```

Reading failures:

- **failed (gain)**: the channel didn't respond as expected to the DAC step. Expect about +0.8 A at 2.0 V and about 0 A at 2.5 V. If every channel on one board looks the same, suspect that board or slot (power, enable, coil connections, select lines). If every board looks the same, suspect the readback scaling or ADC reference. A failed channel keeps the nominal gain so later current commands stay sane.
- **failed (cal)**: the gain was fine but the offset didn't converge. Look at the `it` values: if they shrink slowly, raise `CAL_SETTLE_MS`; if they jump around, the readback is noisy.

### Step-response sweep, `S(b,c)`

Steps one channel through DAC outputs of 2.0, 2.25, 2.5, 2.75 and 3.0 V (about +0.8 to -0.8 A) and prints the ADC code and current 2, 10, 40, 100, 250 and 500 ms after each step. Watch the coil current with a clamp meter at the same time:

- meter correct, readback wrong: readback problem (ADC, reference, buffer, scaling constants);
- meter shows little or no change: output problem (amplifier power or enable, coil connection, DAC bus to that slot);
- values still moving at 250-500 ms: settling problem; set `CAL_SETTLE_MS` above the time where they stop.

## Things to verify on your hardware

- **Readback scaling**: `SENSE_AMP_GAIN` is 10 in the 12-bit version and 1.8 in the 16-bit version. A wrong value scales every measured gain by the same factor.
- **Readback buffer polarity** (12-bit): the code assumes a 74HCT240. For a 74HCT244, see the comment at the end of `readAdcCode()`.
- **`CS_BB`** (16-bit): left low after each ADC read, as in the original code; the 12-bit version releases it. Check what your readback board expects.
- **BNC echo**: the pin changes state on each trigger rather than giving a fixed-width pulse.

See [CHANGELOG.md](CHANGELOG.md) for the history of fixes.

## License

MIT. See [LICENSE](LICENSE).
