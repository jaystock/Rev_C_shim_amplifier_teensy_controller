/******************************************************************************
 * config.h  -  SITE SETTINGS FOR THIS INSTALLATION
 *
 * 16-bit readback (Teensy 4.1). Defaults: Berkeley setup.
 *
 * Everything you are likely to change for a new setup is in this file.
 * Pins, SPI and the DAC/ADC drivers are in hardware.h; calibration
 * algorithm settings (settle time, averaging, tolerance) are in util.h.
 ******************************************************************************/
#pragma once
#include <Arduino.h>

/*============================================================================*
 *  1. Trigger source
 *
 *  TRIGGER_FIBER : trigger pulses arrive on the fiber-optic receiver (pin 6).
 *                  The BNC connector (pin 4) can then optionally echo each
 *                  trigger as an output (BNC_ECHO 1).
 *  TRIGGER_BNC   : trigger pulses arrive on the BNC connector (pin 4).
 *                  BNC_ECHO must be 0, since the BNC is then an input.
 *
 *  The sequence advances one row on each FALLING edge.
 *============================================================================*/
#define TRIGGER_FIBER 1
#define TRIGGER_BNC   2

#define TRIGGER_SOURCE TRIGGER_FIBER
#define BNC_ECHO       1     // 1 = BNC pin echoes triggers (fiber trigger only)

/*============================================================================*
 *  2. Boards and channels
 *============================================================================*/
#define NUM_B 6                     // number of amplifier boards
#define NUM_C 8                     // channels per board

// boardMap[logical board] = physical backplane slot (0..7).
// Logical board 0 holds channels #1-#8, board 1 holds #9-#16, and so on.
// Feb 2020 remap: 6->0, 5->1, 4->2, 3->3
const int boardMap[NUM_B] = {6, 5, 3, 2, 1, 0};

// Channels used on each board, in the order they map onto the columns of
// coefStore. Put -1 after the last used channel of a board.
const int8_t channels_used[NUM_B][NUM_C] = {
  {0, 1, 2, 3, 4, 5, 6, 7},
  {0, 1, 2, 3, 4, 5, 6, 7},
  {0, 1, 2, 3, 4, 5, 6, 7},
  {0, 1, 2, 3, 4, 5, 6, 7},
  {0, 1, 2, 3, 4, 5, 6, 7},
  {0, 1, 2, 3, 4, 5, 6, 7},
};

/*============================================================================*
 *  3. Current readback scaling (depends on the amplifier board revision)
 *
 *     I = (Vadc - SENSE_OFFSET_V) / SENSE_AMP_GAIN / SENSE_R_OHM
 *
 *  Verify with S(b,c) and a clamp meter: a wrong value scales every
 *  measured current and gain by the same factor.
 *============================================================================*/
const float SENSE_OFFSET_V = 1.25;  // sense-amp output at 0 A (V)
const float SENSE_AMP_GAIN = 1.8;   // sense-amp gain
const float SENSE_R_OHM    = 0.2;   // current-sense resistor (ohm)

/*============================================================================*
 *  4. Calibration acceptance
 *============================================================================*/
const float CAL_GAIN_NOM = -1.62;   // expected gain (A per DAC volt)
const float CAL_GAIN_TOL = 0.5;    // allowed |measured - nominal| before "failed (gain)"
const float CAL_TOL_A    = 0.002;   // offset must end within this (A) to pass

/*============================================================================*
 *  5. Serial behaviour
 *============================================================================*/
// Print block/row info on every trigger. Printing inside the interrupt takes
// time; set false if triggers come fast.
const bool TRIGGER_VERBOSE = true;

// true:  a command with no newline still runs after CMD_TIMEOUT_MS of silence,
//        for host programs that send bare characters such as "T".
//        Drawback: a terminal that sends each keystroke as you type (e.g.
//        PuTTY) may run "C" before you finish typing "C(0,3)".
// false: every command must end with a newline (Arduino Serial Monitor:
//        set the line-ending menu to "Newline").
const bool LEGACY_BARE_COMMANDS = false;
const unsigned long CMD_TIMEOUT_MS = 100;
