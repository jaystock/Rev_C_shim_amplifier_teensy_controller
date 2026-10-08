/******************************************************************************
 * util.h  -  shim amplifier controller: calibration, conversions, sequencing
 *
 * This file is IDENTICAL in the 12-bit (Teensy 3.2) and 16-bit (Teensy 4.1)
 * versions. Site settings come from config.h, pins and drivers from
 * hardware.h, and the stored shim-current sequence from coefs.h.
 * Include order in the .ino: hardware.h (which includes config.h), coefs.h,
 * util.h.
 *
 * Signal chain (per channel):
 *   DAC volts (0..5 V, 16-bit LTC2656)  -->  amplifier  -->  coil current
 *   current = gain * (Vdac - 2.5 V - zeroPoint)    gain is about -1.62 A/V
 *   coil current --> sense resistor --> sense amp --> ADC (LTC1863 / LTC1867)
 ******************************************************************************/
#pragma once

/*============================================================================*
 *  Calibration settings
 *============================================================================*/
const bool  CAL_VERBOSE       = true;   // one diagnostic line per channel during calibration
const int   CAL_SETTLE_MS     = 40;     // wait after every DAC step before reading the ADC
const int   CAL_N_AVG         = 16;     // ADC samples averaged per measurement
const int   CAL_MAX_ITER      = 10;     // max offset-correction iterations per channel
// CAL_GAIN_NOM, CAL_GAIN_TOL and CAL_TOL_A (pass/fail limits) are in config.h
const int   CAL_FAIL_PAUSE_MS = 0;      // optional pause after a failed channel
                                        // (an older 16-bit version used 2000 ms here)

const float DAC_FULL_SCALE_V  = 5.0;    // LTC2656 output range
const float DAC_MID_V         = 2.5;    // DAC voltage that gives zero current (before calibration)

/*============================================================================*
 *  Calibration state and channel bookkeeping
 *============================================================================*/
float zeroPoint[NUM_B][NUM_C];          // DAC offset correction (volts) found by calibration
float gain[NUM_B][NUM_C];               // measured gain (A per DAC volt)
bool  calibrationStatus[NUM_B][NUM_C];  // true = channel passed calibration
float Ichannel[NUM_B][NUM_C];           // most recent current reading (A)

// Order in which channels map onto columns of the coefficient table.
// Built from channels_used[][] in the .ino; -1 marks the end of the list.
int8_t channel_order[NUM_B * NUM_C];
int8_t board_order[NUM_B * NUM_C];

// Sequence bookkeeping (see compute_transitions_base)
int block_transitions[maxBlocks];       // trigger count at which each block ends
int block_base[maxBlocks];              // row in coefStore where each block ends

/*============================================================================*
 *  Unit conversions
 *============================================================================*/

// Converting a float outside 0..65535 to uint16_t is undefined behaviour and
// can send a random code to the DAC, so every DAC code goes through this.
uint16_t clampDac(float code) {
  if (!(code > 0.0)) return 0;          // also catches NaN
  if (code > 65535.0) return 65535;
  return uint16_t(code);
}

// DAC code for a raw output voltage, ignoring calibration.
uint16_t dacCodeRaw(float voltage) {
  return clampDac(65535.0 * voltage / DAC_FULL_SCALE_V);
}

// DAC code for an output voltage, corrected by this channel's zero point.
uint16_t computeDacVal_V(float voltage, int b, int c) {
  return clampDac(65535.0 * (voltage - zeroPoint[b][c]) / DAC_FULL_SCALE_V);
}

// DAC code for a desired coil current (A), using this channel's calibration.
uint16_t computeDacVal_I(float current, int b, int c) {
  return clampDac(65535.0 * (current / gain[b][c] + DAC_MID_V - zeroPoint[b][c])
                  / DAC_FULL_SCALE_V);
}

// ADC code -> voltage at the ADC input.
float computeOutV(uint16_t adcCode) {
  return float(adcCode) * ADC_VREF / ADC_COUNTS;
}

// ADC code -> coil current (A).  Sense amp output is 1.25 V at zero current.
float computeOutI(uint16_t adcCode) {
  return (computeOutV(adcCode) - SENSE_OFFSET_V) / SENSE_AMP_GAIN / SENSE_R_OHM;
}

/*============================================================================*
 *  Channel labels and command-argument parsing
 *============================================================================*/

// Channels are numbered 1..48, board-major:
// board 0 ch 0 = #1, board 0 ch 7 = #8, board 1 ch 0 = #9, ... board 5 ch 7 = #48
int chanNumber(int b, int c) { return b * NUM_C + c + 1; }

// Prints e.g. "Ch  9/48  (1,0)  slot 1"  (slot = physical slot from boardMap)
void printChanLabel(int b, int c) {
  int n = chanNumber(b, c);
  Serial.print("Ch ");
  if (n < 10) Serial.print(" ");
  Serial.print(n); Serial.print("/"); Serial.print(NUM_B * NUM_C);
  Serial.print("  ("); Serial.print(b); Serial.print(","); Serial.print(c);
  Serial.print(")  slot "); Serial.print(boardMap[b]);
}

bool validBC(int b, int c) {
  if (b < 0 || b >= NUM_B || c < 0 || c >= NUM_C) {
    Serial.println("board/channel out of range");
    return false;
  }
  return true;
}

// Parses "X(b,c)" into b and c. Spaces are allowed: "C( 2 , 5 )".
bool parseBC(String cmd, int &b, int &c) {
  int open  = cmd.indexOf('(');
  int comma = cmd.indexOf(',', open + 1);
  int close = cmd.indexOf(')', comma + 1);
  if (open < 0 || comma < 0 || close < 0) {
    Serial.println("format: X(b,c), e.g. C(0,3)");
    return false;
  }
  String sb = cmd.substring(open + 1, comma);  sb.trim();
  String sc = cmd.substring(comma + 1, close); sc.trim();
  if (sb.length() == 0 || sc.length() == 0) {
    Serial.println("format: X(b,c), e.g. C(0,3)");
    return false;
  }
  b = sb.toInt();
  c = sc.toInt();
  return true;
}

/*============================================================================*
 *  Setup helpers
 *============================================================================*/

// Forget all calibration (gain = nominal, no offset correction).
void resetCalibration() {
  for (int b = 0; b < NUM_B; b++) {
    for (int c = 0; c < NUM_C; c++) {
      zeroPoint[b][c] = 0;
      gain[b][c] = CAL_GAIN_NOM;
      calibrationStatus[b][c] = false;
      Ichannel[b][c] = 0;
    }
  }
}

// Build channel_order/board_order from the channels_used table in the .ino.
// A -1 entry ends that board's list.
void initChannelOrder(const int8_t used[NUM_B][NUM_C]) {
  for (int j = 0; j < NUM_B * NUM_C; j++) {
    channel_order[j] = -1;
    board_order[j] = -1;
  }
  int i = 0;
  for (int b = 0; b < NUM_B; b++) {
    for (int c = 0; c < NUM_C; c++) {
      if (used[b][c] == -1) break;
      channel_order[i] = used[b][c];
      board_order[i] = b;
      i++;
    }
  }
}

/*============================================================================*
 *  Output control
 *============================================================================*/

// Set every channel to 0 A (using its current calibration).
void zero_all() {
  for (int b = 0; b < NUM_B; b++) {
    selectBoard(b);
    for (int c = 0; c < NUM_C; c++) {
      LTC2656Write(WRITE_AND_UPDATE, channelMap[c], computeDacVal_I(0, b, c));
    }
  }
}

float coefStoreAsMat(int chanIdx, int blkIdx, int repIdx);

// Write one row of the coefficient table (block blkIdx, row repIdx) to all channels.
void update_outputs(int blkIdx, int repIdx, bool verbose) {
  int8_t b = 0;
  selectBoard(b);
  if (verbose) Serial.println("-------------------------");
  for (int i = 0; i < NUM_C * NUM_B; i++) {
    int c = channel_order[i];
    if (c == -1) break;
    if (b != board_order[i]) {
      b = board_order[i];
      selectBoard(b);
    }
    LTC2656Write(WRITE_AND_UPDATE, channelMap[c],
                 computeDacVal_I(coefStoreAsMat(i, blkIdx, repIdx), b, c));
  }
}

/*============================================================================*
 *  Calibration
 *============================================================================*/

// Average of n current readings on channel c of the currently selected board.
float readAvgI(uint8_t c, int n) {
  float sum = 0;
  for (int i = 0; i < n; i++) sum += computeOutI(readAdcCode(c));
  return sum / n;
}

// Step the DAC from 2.0 V to 2.5 V and measure the current change.
// Expected result is about -1.62 A/V (+0.81 A at 2.0 V, 0 A at 2.5 V).
float measure_gain(uint8_t b, uint8_t c) {
  selectBoard(b);
  delay(1);
  LTC2656Write(WRITE_AND_UPDATE, channelMap[c], computeDacVal_V(2.0, b, c));
  delay(CAL_SETTLE_MS);
  float i_2v0 = readAvgI(c, CAL_N_AVG);
  LTC2656Write(WRITE_AND_UPDATE, channelMap[c], computeDacVal_V(2.5, b, c));
  delay(CAL_SETTLE_MS);
  float i_2v5 = readAvgI(c, CAL_N_AVG);
  if (CAL_VERBOSE) {
    Serial.print("  I@2.0V="); Serial.print(i_2v0, 4);
    Serial.print("  I@2.5V="); Serial.print(i_2v5, 4);
  }
  return (i_2v5 - i_2v0) / 0.5;
}

// Measure gain, then iteratively adjust zeroPoint until the channel reads 0 A.
// Prints one line per channel. Returns true if the channel passed.
bool calibrate_channel(uint8_t b, uint8_t c) {
  zeroPoint[b][c] = 0;
  selectBoard(b);
  delay(10);
  printChanLabel(b, c);

  // --- 1. gain ---
  gain[b][c] = measure_gain(b, c);
  if (CAL_VERBOSE) { Serial.print("  gain="); Serial.print(gain[b][c], 4); }

  if (fabsf(gain[b][c] - CAL_GAIN_NOM) > CAL_GAIN_TOL) {
    Serial.println("  -> failed (gain)");
    calibrationStatus[b][c] = false;
    // Don't keep a bad gain: later current commands divide by it.
    gain[b][c] = CAL_GAIN_NOM;
    LTC2656Write(WRITE_AND_UPDATE, channelMap[c], computeDacVal_I(0, b, c));
    if (CAL_FAIL_PAUSE_MS > 0) delay(CAL_FAIL_PAUSE_MS);
    return false;
  }

  // --- 2. offset: measure, correct, settle, re-measure ---
  float offset = 0;
  for (int i = 0; i < CAL_MAX_ITER; i++) {
    offset = readAvgI(c, CAL_N_AVG);
    if (CAL_VERBOSE) {
      Serial.print("  it"); Serial.print(i);
      Serial.print(":"); Serial.print(offset, 4);
    }
    if (fabsf(offset) <= CAL_TOL_A) {
      calibrationStatus[b][c] = true;
      if (CAL_VERBOSE) { Serial.print("  zp="); Serial.print(zeroPoint[b][c], 5); }
      Serial.println("  -> ok");
      return true;
    }
    zeroPoint[b][c] += offset / gain[b][c];
    LTC2656Write(WRITE_AND_UPDATE, channelMap[c], computeDacVal_I(0, b, c));
    delay(CAL_SETTLE_MS);
  }

  // --- 3. did not converge: undo the correction ---
  Serial.print("  -> failed (cal), residual=");
  Serial.println(offset, 4);
  calibrationStatus[b][c] = false;
  zeroPoint[b][c] = 0;
  LTC2656Write(WRITE_AND_UPDATE, channelMap[c], computeDacVal_I(0, b, c));
  if (CAL_FAIL_PAUSE_MS > 0) delay(CAL_FAIL_PAUSE_MS);
  return false;
}

// Calibrate channels #1..#48 in order; returns the number that passed.
int calibrate_all() {
  int nOk = 0;
  for (int b = 0; b < NUM_B; b++) {
    for (int c = 0; c < NUM_C; c++) {
      if (calibrate_channel(b, c)) nOk++;
    }
  }
  Serial.print("Calibrated "); Serial.print(nOk);
  Serial.print(" / "); Serial.println(NUM_B * NUM_C);
  return nOk;
}

/*============================================================================*
 *  Diagnostics
 *============================================================================*/

// Step one channel through DAC voltages 2.0..3.0 V (about +0.8 .. -0.8 A) and
// log the readback 2..500 ms after each step. Compare against a clamp meter
// to separate settling, gain, and readback-scaling problems.
void sweep_channel(int b, int c) {
  const float vs[] = {2.0, 2.25, 2.5, 2.75, 3.0};
  const int   ts[] = {2, 10, 40, 100, 250, 500};     // ms after each step
  selectBoard(b);
  delay(1);
  Serial.print("Sweep ");
  printChanLabel(b, c);
  Serial.println();
  Serial.println("Vdac  expI    | t(ms):code/I ...");
  for (int k = 0; k < 5; k++) {
    LTC2656Write(WRITE_AND_UPDATE, channelMap[c], dacCodeRaw(vs[k]));
    unsigned long t0 = millis();
    Serial.print(vs[k], 2); Serial.print("  ");
    Serial.print(CAL_GAIN_NOM * (vs[k] - DAC_MID_V), 3); Serial.print("  |");
    for (int j = 0; j < 6; j++) {
      while (millis() - t0 < (unsigned long)ts[j]) {}
      uint16_t code = readAdcCode(c);
      Serial.print(" "); Serial.print(ts[j]); Serial.print(":");
      Serial.print(code); Serial.print("/");
      Serial.print(computeOutI(code), 3);
    }
    Serial.println();
    delay(500);                                       // hold so a meter can be read
  }
  LTC2656Write(WRITE_AND_UPDATE, channelMap[c], dacCodeRaw(DAC_MID_V));
  Serial.println("done (left at 2.5 V, uncalibrated zero)");
}

/*============================================================================*
 *  Readback listings
 *============================================================================*/

// "I": human-readable currents for channels #1..#48.
void print_channels_numbered() {
  Serial.println("---------------- channel currents ----------------");
  int nOk = 0;
  for (int b = 0; b < NUM_B; b++) {
    selectBoard(b);
    for (int c = 0; c < NUM_C; c++) {
      Ichannel[b][c] = computeOutI(readAdcCode(c));
      printChanLabel(b, c);
      Serial.print("   I=");
      if (Ichannel[b][c] >= 0) Serial.print(" ");
      Serial.print(Ichannel[b][c], 4);
      Serial.print(" A   gain="); Serial.print(gain[b][c], 3);
      if (calibrationStatus[b][c]) { Serial.println("   cal OK"); nOk++; }
      else                         { Serial.println("   cal X"); }
    }
  }
  Serial.print("calibrated: "); Serial.print(nOk);
  Serial.print(" / ");          Serial.println(NUM_B * NUM_C);
}

// "A": raw ADC code, current, and ADC voltage for every channel (averaged).
void print_all_boards() {
  Serial.println("------- raw readback: code   I(A)   V(adc)   gain -------");
  for (int b = 0; b < NUM_B; b++) {
    selectBoard(b);
    for (int c = 0; c < NUM_C; c++) {
      uint32_t sum = 0;
      for (int i = 0; i < CAL_N_AVG; i++) sum += readAdcCode(c);
      uint16_t code = uint16_t(sum / CAL_N_AVG);
      printChanLabel(b, c);
      Serial.print("   "); Serial.print(code);
      Serial.print("   "); Serial.print(computeOutI(code), 4);
      Serial.print("   "); Serial.print(computeOutV(code), 4);
      Serial.print("   "); Serial.print(gain[b][c], 3);
      Serial.println(calibrationStatus[b][c] ? "" : "  X");
    }
  }
}

// Machine-readable output for host software. Keep these formats unchanged:
//   "Icurrent b c I gain T|F"
void give_one_channel_current(int board, int channel) {
  if (!validBC(board, channel)) return;
  selectBoard(board);
  Serial.print("Icurrent ");
  Serial.print(board);
  Serial.print(" ");
  Serial.print(channel);
  Serial.print(" ");
  Ichannel[board][channel] = computeOutI(readAdcCode(channel));
  Serial.print(Ichannel[board][channel], 4);
  Serial.print(" ");
  Serial.print(gain[board][channel]);
  Serial.println(calibrationStatus[board][channel] ? " T" : " F");
}

//   "Icurrentall" / one "b c I gain T|F" line per channel / "IcurrentallEnd"
void give_all_channel_current() {
  Serial.print("Icurrentall\n");
  int8_t b = 0;
  selectBoard(b);
  for (int i = 0; i < NUM_C * NUM_B; i++) {
    int c = channel_order[i];
    if (c == -1) break;
    if (b != board_order[i]) {
      b = board_order[i];
      selectBoard(b);
    }
    Ichannel[b][c] = computeOutI(readAdcCode(c));
    Serial.print(b);
    Serial.print(" ");
    Serial.print(c);
    Serial.print(" ");
    Serial.print(Ichannel[b][c], 4);
    Serial.print(" ");
    Serial.print(gain[b][c]);
    Serial.println(calibrationStatus[b][c] ? " T" : " F");
  }
  Serial.print("IcurrentallEnd\n");
}

/*============================================================================*
 *  Shim-current sequence
 *
 *  coefStore holds rows of `channels` currents (A). The rows are grouped into
 *  `blocks`; block k has lengths[k] rows and is played reps[k] times. Each
 *  trigger advances `counter` by one row. After the last block it wraps to 0.
 *============================================================================*/

void compute_transitions_base() {
  Serial.println("transitions:");
  block_transitions[0] = reps[0] * lengths[0];
  Serial.println(block_transitions[0]);
  for (int i = 1; i < blocks; i++) {
    block_transitions[i] = block_transitions[i - 1] + reps[i] * lengths[i];
    Serial.println(block_transitions[i]);
  }
  Serial.println("base:");
  block_base[0] = lengths[0];
  Serial.println(block_base[0]);
  for (int i = 1; i < blocks; i++) {
    block_base[i] = block_base[i - 1] + lengths[i];
    Serial.println(block_base[i]);
  }
}

// Which block trigger number `iter` falls in; -1 once past the last block.
int computeBlockIdx(int iter) {
  int blkIdx = 0;
  for (int i = 0; i < blocks; i++) {
    if (iter >= block_transitions[i]) blkIdx = i + 1;
  }
  if (blkIdx >= blocks) blkIdx = -1;
  return blkIdx;
}

// Row within the block for trigger number `iter`.
int computeRepIdx(int iter, int blkIdx) {
  int base = 0;
  if (blkIdx >= 1) base = block_transitions[blkIdx - 1];
  if (lengths[blkIdx] <= 0) return 0;
  return (iter - base) % lengths[blkIdx];
}

// Current for column chanIdx of row repIdx in block blkIdx.
float coefStoreAsMat(int chanIdx, int blkIdx, int repIdx) {
  int base = 0;
  if (blkIdx >= 1) base = block_base[blkIdx - 1];
  long idx = long(channels) * (base + repIdx) + chanIdx;
  if (idx < 0 || idx >= COEF_CAPACITY) return 0;   // out of range: output 0 A
  return coefStore[idx];
}

/*============================================================================*
 *  Sequence upload over serial
 *============================================================================*/

// Parses a header like "c48|b2|l96|2|r10000|3|" into channels, blocks,
// lengths[] and reps[]. Returns false (and changes nothing) if malformed.
bool read_ctrl_string(char *buf) {
  int newChannels, newBlocks;
  int newLengths[maxBlocks], newReps[maxBlocks];

  char *p = strchr(buf, 'c');            if (!p) goto bad;
  newChannels = atoi(p + 1);
  p = strchr(p, 'b');                    if (!p) goto bad;
  newBlocks = atoi(p + 1);
  if (newBlocks < 1 || newBlocks > maxBlocks) goto bad;

  p = strchr(p, 'l');                    if (!p) goto bad;
  for (int i = 0; i < newBlocks; i++) {
    newLengths[i] = atoi(p + 1);
    p = strchr(p + 1, '|');              if (!p) goto bad;
  }
  p = strchr(p, 'r');                    if (!p) goto bad;
  for (int i = 0; i < newBlocks; i++) {
    newReps[i] = atoi(p + 1);
    p = strchr(p + 1, '|');              if (!p) goto bad;
  }

  channels = newChannels;
  blocks = newBlocks;
  Serial.print("blocks:"); Serial.println(blocks);
  Serial.println("lengths:");
  for (int i = 0; i < blocks; i++) { lengths[i] = newLengths[i]; Serial.println(lengths[i]); }
  Serial.println("repeats:");
  for (int i = 0; i < blocks; i++) { reps[i] = newReps[i]; Serial.println(reps[i]); }
  compute_transitions_base();
  return true;

bad:
  Serial.println("header parse error; upload cancelled");
  return false;
}

// Reads channels * sum(lengths) raw little-endian floats into coefStore.
// BLOCKING; times out per Serial.setTimeout (default 1 s between bytes).
// Returns false while waiting for data to arrive, true when finished.
bool read_float_dump() {
  if (!Serial.available()) return false;
  long totalLength = 0;
  for (int i = 0; i < blocks; i++) totalLength += long(channels) * lengths[i];

  long toStore = totalLength;
  if (toStore > COEF_CAPACITY) {
    Serial.print("upload too large for coefStore; keeping first ");
    Serial.print(COEF_CAPACITY); Serial.println(" values");
    toStore = COEF_CAPACITY;
  }
  Serial.println("starting");
  size_t got = Serial.readBytes((char *)coefStore, 4 * toStore);

  // discard anything that didn't fit so it isn't read as commands
  long extra = 4 * (totalLength - toStore);
  char scratch[64];
  while (extra > 0) {
    size_t n = Serial.readBytes(scratch, extra > 64 ? 64 : extra);
    if (n == 0) break;
    extra -= n;
  }

  if (got != size_t(4 * toStore)) {
    Serial.print("warning: expected "); Serial.print(4 * toStore);
    Serial.print(" bytes, got ");       Serial.println(got);
  }
  Serial.println("done");
  return true;
}
