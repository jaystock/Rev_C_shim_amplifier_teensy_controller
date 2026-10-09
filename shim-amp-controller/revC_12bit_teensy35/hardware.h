/******************************************************************************
 * hardware.h  -  Rev C shim amplifier, 12-bit readback version
 *
 *   Microcontroller : Teensy 3.5 (Kinetis K64), SPI via kinetis_spi.h (included)
 *   DAC             : LTC2656 (16-bit, 8 channels per board)
 *   ADC (readback)  : LTC1863 (12-bit, 8 channels per board)
 *   Trigger         : fiber (pin 6) or BNC (pin 4), selected in config.h
 *
 * Site settings (trigger source, board map, readback scaling, ...) are in
 * config.h. This file holds what is fixed by the electronics: pins, SPI,
 * and the DAC/ADC drivers. util.h and the .ino are shared by both versions.
 ******************************************************************************/
#pragma once
#include <Arduino.h>
#include "config.h"
#include "kinetis_spi.h"

/*============================================================================*
 *  ADC (sense-amp scaling is in config.h)
 *============================================================================*/
const float ADC_VREF   = 4.096;       // ADC reference (V)
const float ADC_COUNTS = 4096.0;      // 12-bit

/*============================================================================*
 *  Pins
 *============================================================================*/
const int selectPin0   = 14;          // function select (DAC / ADC / none)
const int selectPin1   = 15;
const int boardSelect2 = 16;          // 3-bit board (slot) select
const int boardSelect1 = 17;
const int boardSelect0 = 18;
const int CS_BB        = 30;          // bit-banged chip select used during ADC reads

/*============================================================================*
 *  Trigger wiring (choose the source in config.h)
 *============================================================================*/
const int fiberPin = 6;               // fiber-optic receiver output
const int bncPin   = 4;               // BNC connector

#if TRIGGER_SOURCE == TRIGGER_FIBER
  const int  TRIGGER_PIN = fiberPin;
  const bool bncOut      = (BNC_ECHO != 0);   // BNC can echo triggers
  const char TRIGGER_DESC[] = "fiber optic (pin 6)";
#elif TRIGGER_SOURCE == TRIGGER_BNC
  #if BNC_ECHO
    #error "config.h: BNC_ECHO must be 0 when TRIGGER_SOURCE is TRIGGER_BNC"
  #endif
  const int  TRIGGER_PIN = bncPin;
  const bool bncOut      = false;
  const char TRIGGER_DESC[] = "BNC (pin 4)";
#else
  #error "config.h: TRIGGER_SOURCE must be TRIGGER_FIBER or TRIGGER_BNC"
#endif

/*============================================================================*
 *  Chip definitions
 *============================================================================*/
typedef enum LTC26456_COMMAND {
  WRITE_TO_INPUT             = 0x00,
  UPDATE_DAC                 = 0x10,
  WRITE_TO_INPUT_UPDATE_ALL  = 0x20,
  WRITE_AND_UPDATE           = 0x30,
  POWER_DOWN                 = 0x40,
  POWER_DOWN_CHIP            = 0x50,
  SELECT_INTERNAL_REF        = 0x60,
  SELECT_EXTERNAL_REF        = 0x70,
  NOP                        = 0xF0
} LTC26456_COMMAND;

typedef enum LTC2656_ADDRESS {
  DAC_A = 0x00, DAC_B = 0x01, DAC_C = 0x02, DAC_D = 0x03,
  DAC_E = 0x04, DAC_F = 0x05, DAC_G = 0x06, DAC_H = 0x07,
  DAC_ALL = 0x0F
} LTC2656_ADDRESS;

// Logical channel c -> DAC output / ADC input on the board
LTC2656_ADDRESS channelMap[NUM_C] = {DAC_E, DAC_F, DAC_G, DAC_H, DAC_A, DAC_B, DAC_C, DAC_D};
uint8_t channelMap_ADC[NUM_C]     = {0, 4, 1, 5, 2, 6, 3, 7};

/*============================================================================*
 *  SPI state
 *  SPI0 = master (DAC writes, ADC commands); SPI1 = slave that receives the
 *  ADC result through the readback buffer, collected in spi1_isr().
 *============================================================================*/
KinetisSpi spiMaster(&KINETISK_SPI0);
KinetisSpi spiSlave(&KINETISK_SPI1);
volatile uint16_t data_tx[20] = {};
volatile uint16_t data_rx[20] = {};
volatile bool read_in_flight = false;

// ADC reads that got no reply on SPI1 (counted per command; see the .ino).
// A read gives up after ADC_TIMEOUT_US instead of waiting forever.
volatile unsigned long adcTimeouts = 0;
volatile bool adcLastTimedOut = false;      // true if the most recent read got no reply
const unsigned long ADC_TIMEOUT_US = 5000;

/*============================================================================*
 *  Init
 *============================================================================*/
void initIO() {
  pinMode(selectPin0, OUTPUT);
  pinMode(selectPin1, OUTPUT);
  pinMode(boardSelect0, OUTPUT);
  pinMode(boardSelect1, OUTPUT);
  pinMode(boardSelect2, OUTPUT);
  pinMode(fiberPin, INPUT);
  pinMode(CS_BB, OUTPUT);
  digitalWrite(CS_BB, 1);
  pinMode(bncPin, bncOut ? OUTPUT : INPUT);
}

void spiInit() {
  spiMaster.beginMaster16(KSPI_CLOCK_DIV32);   // DIV32 (not DIV16): slower bus for signal integrity
  spiSlave.beginSlave16Mode1();
  delay(2000);   // start-up pause the T3SPI constructors used to add (2 x 1 s)
}

// Called once at the end of setup(): enable the SPI1 receive interrupt and
// clear anything the slave picked up during start-up.
void adcInit() {
  NVIC_ENABLE_IRQ(IRQ_SPI1);
  delay(500);
  spiSlave.packetCT = 0;
  spiSlave.dataPointer = 0;
}

/*============================================================================*
 *  Function and board select
 *============================================================================*/
void selectNone()  { digitalWrite(selectPin0, HIGH); digitalWrite(selectPin1, HIGH); }
void selectDAC()   { digitalWrite(selectPin0, LOW);  digitalWrite(selectPin1, LOW);  }
void selectADC()   { digitalWrite(selectPin0, HIGH); digitalWrite(selectPin1, LOW);  }
void selectError() { digitalWrite(selectPin0, LOW);  digitalWrite(selectPin1, HIGH); }

// Route the bus to logical board `board` (physical slot boardMap[board]).
void selectBoard(int board) {
  int b = boardMap[board];
  digitalWrite(boardSelect0, b & 0x01);
  digitalWrite(boardSelect1, b & 0x02);
  digitalWrite(boardSelect2, b & 0x04);
}

/*============================================================================*
 *  DAC write (LTC2656, 32-bit frame: 8 don't-care, 8 command/address, 16 data)
 *============================================================================*/
void LTC2656Write(LTC26456_COMMAND action, LTC2656_ADDRESS address, uint16_t value) {
  cli();
  selectNone();
  selectDAC();
  data_tx[0] = ((action | address) & 0xFF);
  data_tx[1] = value;
  spiMaster.tx16(data_tx, 2, KSPI_PCS0);
  selectNone();
  sei();
  delayMicroseconds(100);
}

/*============================================================================*
 *  ADC read (LTC1863)
 *  Frame 1 selects the channel; raising CS starts the conversion. Frame 2
 *  clocks out the result, which arrives on SPI1 via spi1_isr().
 *  Returns the 12-bit code (0..4095).
 *============================================================================*/
uint16_t readAdcCode(uint8_t c) {
  read_in_flight = true;
  NVIC_ENABLE_IRQ(IRQ_SPI1);
  selectNone();
  selectADC();
  digitalWrite(CS_BB, 0);
  // command word: single-ended, channel, unipolar
  data_tx[0] = ((0x80 | (channelMap_ADC[c] << 4) | 0x04) << 8);
  spiMaster.tx16(data_tx, 1, KSPI_PCS0);
  selectNone();                     // CS high -> start conversion
  delayMicroseconds(20);            // conversion time

  selectNone();
  selectADC();
  // this frame's command only sets up the NEXT conversion; its result is ignored
  data_tx[0] = ((0x80 | (c << 4) | 0x04) << 8);
  spiMaster.tx16(data_tx, 1, KSPI_PCS0);
  unsigned long t0 = micros();
  bool timedOut = false;
  while (spiSlave.packetCT == 0) {
    // wait for spi1_isr to receive the result
    if (micros() - t0 > ADC_TIMEOUT_US) {   // nothing arrived on SPI1
      timedOut = true;
      break;
    }
  }
  digitalWrite(CS_BB, 1);
  read_in_flight = false;
  NVIC_DISABLE_IRQ(IRQ_SPI1);
  spiSlave.packetCT = 0;
  spiSlave.dataPointer = 0;
  selectNone();

  adcLastTimedOut = timedOut;
  if (timedOut) {
    adcTimeouts++;
    return 0;
  }

  // With a 74HCT240 (inverting) readback buffer the data arrives as-is.
  // If your board uses a 74HCT244 (non-inverting), use:  (~data_rx[1] & 0xFFFF) >> 4
  return (data_rx[1] & 0xFFFF) >> 4;
}

// SPI1 receive interrupt: keep the data during a read, discard it otherwise.
// The core's vector table looks this up by its C name.
extern "C" void spi1_isr(void);
void spi1_isr(void) {
  cli();
  if (read_in_flight) {
    spiSlave.rx16(data_rx, 2);
  } else {
    volatile uint16_t dump;
    spiSlave.rx16(&dump, 1);
  }
  sei();
}
