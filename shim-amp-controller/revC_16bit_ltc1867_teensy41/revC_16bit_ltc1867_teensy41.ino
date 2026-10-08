/******************************************************************************
 * Shim amplifier controller - main sketch (same file in both versions)
 *
 * Multi-channel shim amplifier controller. Plays a stored sequence of shim
 * currents (one row per trigger pulse), calibrates each channel's gain and
 * zero-current offset, and reports channel currents over USB serial.
 *
 * Files:
 *   config.h    SITE SETTINGS: trigger source, board map, scaling  <- edit this
 *   hardware.h  pins, SPI, DAC/ADC drivers                 (version-specific)
 *   coefs.h     stored current sequence (coefStore, lengths, reps)
 *   util.h      calibration, conversions, sequencing       (shared)
 *
 * Serial: 115200 baud. Commands end with a newline; type H for the list.
 ******************************************************************************/
#include "hardware.h"
#include "coefs.h"
#include "util.h"

/*============================================================================*
 *  Sequence playback (trigger interrupt)
 *============================================================================*/
volatile int  counter = 0;      // number of triggers since the sequence started
volatile long tIrupt  = 0;      // BNC echo state
volatile bool outHigh = false;

// BNC echo output (only with fiber trigger and BNC_ECHO 1 in config.h). Kept exactly as in the original:
// the pin goes high on one trigger and low on the next, so it toggles rather
// than producing a fixed-width pulse.
void bncEcho() {
  if (!bncOut) return;
  long tnow = millis();
  if (tnow > tIrupt && !outHigh) {
    digitalWrite(bncPin, HIGH);
    outHigh = true;
    tIrupt = tnow;
  }
  tnow = millis();
  if (tnow > tIrupt + .013 && outHigh) {
    digitalWrite(bncPin, LOW);
    outHigh = false;
    tIrupt = tnow;
  }
}

// Advance one row in the sequence. Runs on each trigger edge, and on "T".
void setDACVal() {
  int repIdx;
  int blkIdx = computeBlockIdx(counter);
  if (blkIdx == -1) {             // past the last block: start over
    blkIdx = 0;
    repIdx = 0;
    counter = 0;
  } else {
    repIdx = computeRepIdx(counter, blkIdx);
  }
  if (TRIGGER_VERBOSE) {
    Serial.print("counter: "); Serial.println(counter);
    Serial.print("blkIdx: ");  Serial.println(blkIdx);
    Serial.print("repIdx: ");  Serial.println(repIdx);
  }
  update_outputs(blkIdx, repIdx, TRIGGER_VERBOSE);
  counter++;
  bncEcho();
}

void triggerOn()  { attachInterrupt(TRIGGER_PIN, setDACVal, FALLING); }
void triggerOff() { detachInterrupt(TRIGGER_PIN); }

/*============================================================================*
 *  Serial input
 *============================================================================*/
enum UploadState { UPLOAD_NONE, UPLOAD_WAIT_HEADER, UPLOAD_WAIT_BODY };
UploadState uploadState = UPLOAD_NONE;

const int ctrlBuffer_length = 100;
char ctrlBuffer[ctrlBuffer_length];

String cmdLine;                   // command being received
unsigned long lastCharMs = 0;

void printHelp() {
  Serial.println("Commands (end each with a newline):");
  Serial.println("  C            calibrate all channels #1..#48");
  Serial.println("  C(b,c)       calibrate one channel, e.g. C(2,5)");
  Serial.println("  S(b,c)       step-response sweep of one channel (diagnostic)");
  Serial.println("  I            list channel currents #1..#48");
  Serial.println("  A            raw readback: ADC code, current, ADC volts");
  Serial.println("  Z            set all channels to 0 A");
  Serial.println("  M            restart sequence and load row 1");
  Serial.println("  T            advance sequence one row (same as a trigger)");
  Serial.println("  E            reset: forget calibration, restart sequence, 0 A");
  Serial.println("  H or ?       this list");
  Serial.println("Host-program commands:");
  Serial.println("  Icurrentall              all currents, machine-readable");
  Serial.println("  Ichannel,b,c             one current, machine-readable");
  Serial.println("  Track,b,c,amps           set one channel's current (-4..4 A)");
  Serial.println("  uploadcoef               then send raw float32 data");
  Serial.println("  byte 0x01                then header + raw float32 data");
}

void handleCommand(String command) {
  command.trim();
  if (command.length() == 0) return;

  if (command == "C") {
    triggerOff();                 // keep trigger pulses from touching DACs mid-calibration
    calibrate_all();
    triggerOn();

  } else if (command.startsWith("C(")) {
    int b, c;
    if (parseBC(command, b, c) && validBC(b, c)) {
      triggerOff();
      calibrate_channel(b, c);
      triggerOn();
    }

  } else if (command.startsWith("S(")) {
    int b, c;
    if (parseBC(command, b, c) && validBC(b, c)) {
      triggerOff();
      sweep_channel(b, c);
      triggerOn();
    }

  } else if (command == "I") {
    print_channels_numbered();

  } else if (command == "A") {
    print_all_boards();

  } else if (command == "Z") {
    zero_all();

  } else if (command == "M") {
    counter = 0;
    Serial.println(counter);
    update_outputs(0, 0, TRIGGER_VERBOSE);
    counter = 1;

  } else if (command == "T") {
    setDACVal();

  } else if (command == "E") {
    resetCalibration();
    counter = 0;
    zero_all();
    Serial.println("reset: calibration cleared, sequence at row 1, outputs 0 A");

  } else if (command == "H" || command == "?") {
    printHelp();

  } else if (command == "Icurrentall") {
    give_all_channel_current();

  } else if (command.startsWith("Ichannel")) {
    int c1 = command.indexOf(',');
    int c2 = command.indexOf(',', c1 + 1);
    if (c1 > 0 && c2 > c1) {
      give_one_channel_current(command.substring(c1 + 1, c2).toInt(),
                               command.substring(c2 + 1).toInt());
    }

  } else if (command.startsWith("Track")) {
    int c1 = command.indexOf(',');
    int c2 = command.indexOf(',', c1 + 1);
    int c3 = command.indexOf(',', c2 + 1);
    if (c1 > 0 && c2 > c1 && c3 > c2) {
      int   b    = command.substring(c1 + 1, c2).toInt();
      int   c    = command.substring(c2 + 1, c3).toInt();
      float amps = command.substring(c3 + 1).toFloat();
      if (!validBC(b, c)) {
        // message already printed
      } else if (amps >= -4 && amps <= 4) {
        selectBoard(b);
        LTC2656Write(WRITE_AND_UPDATE, channelMap[c], computeDacVal_I(amps, b, c));
        give_one_channel_current(b, c);
      } else {
        Serial.println("Current out of bounds (check code)");
      }
    }

  } else if (command.startsWith("uploadcoef")) {
    // Fixed-size upload: uses the current channels/blocks/lengths settings.
    Serial.println("Ready to receive coefStore...");
    uploadState = UPLOAD_WAIT_BODY;
    delay(20);

  } else {
    Serial.print("Unknown command: ");
    Serial.println(command);
  }
}

// Collect characters into a command line; run it on newline (or on timeout
// when LEGACY_BARE_COMMANDS is set). Stops reading while an upload is
// pending so binary data isn't mistaken for commands.
void pollSerial() {
  while (uploadState == UPLOAD_NONE && Serial.available()) {
    char ch = Serial.read();

    if (ch == 0x01) {             // start of header + data upload
      cmdLine = "";
      counter = 0;
      zero_all();                 // outputs to 0 A while the new sequence loads
      uploadState = UPLOAD_WAIT_HEADER;
      return;
    }
    if (ch == '\n' || ch == '\r') {
      if (cmdLine.length() > 0) {
        String cmd = cmdLine;
        cmdLine = "";
        handleCommand(cmd);
      }
    } else {
      cmdLine += ch;
      lastCharMs = millis();
      if (cmdLine.length() > 100) cmdLine = "";   // runaway input
    }
  }

  if (LEGACY_BARE_COMMANDS && cmdLine.length() > 0 &&
      millis() - lastCharMs >= CMD_TIMEOUT_MS) {
    String cmd = cmdLine;
    cmdLine = "";
    handleCommand(cmd);
  }
}

/*============================================================================*
 *  Setup and main loop
 *============================================================================*/
void setup() {
  Serial.begin(115200);

  initIO();
  selectNone();
  spiInit();

  resetCalibration();
  initChannelOrder(channels_used);

  delay(500);
  triggerOn();
  Serial.println("I'm up");

  adcInit();
  selectNone();
  compute_transitions_base();
  Serial.print("Trigger: "); Serial.print(TRIGGER_DESC);
  Serial.println(bncOut ? ", echoed on BNC (pin 4)" : "");
  Serial.print("Boards: "); Serial.print(NUM_B);
  Serial.print("   channels per board: "); Serial.println(NUM_C);
  Serial.println("Type H for commands.");
}

void loop() {
  switch (uploadState) {
    case UPLOAD_NONE:
      pollSerial();
      break;

    case UPLOAD_WAIT_HEADER:
      if (Serial.available()) {
        int n = Serial.readBytesUntil(0, ctrlBuffer, ctrlBuffer_length - 1);
        ctrlBuffer[n] = 0;        // readBytesUntil does not terminate the string
        uploadState = read_ctrl_string(ctrlBuffer) ? UPLOAD_WAIT_BODY : UPLOAD_NONE;
      }
      break;

    case UPLOAD_WAIT_BODY:
      if (read_float_dump()) {
        uploadState = UPLOAD_NONE;
        Serial.println("✅ coefStore updated.");
      }
      break;
  }
}
