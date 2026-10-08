/******************************************************************************
 * hardware.h  -  shim amplifier, 16-bit readback version
 *
 *   Microcontroller : Teensy 4.1, Arduino SPI library
 *   DAC             : LTC2656 (16-bit, 8 channels per board)
 *   ADC (readback)  : LTC1867 (16-bit, 8 channels per board)
 *   Trigger         : fiber (pin 6) or BNC (pin 4), selected in config.h
 *
 * Site settings (trigger source, board map, readback scaling, ...) are in
 * config.h. This file holds what is fixed by the electronics: pins, SPI,
 * and the DAC/ADC drivers. util.h and the .ino are shared by both versions.
 ******************************************************************************/
#pragma once
#include <Arduino.h>
#include "config.h"
#include <SPI.h>

/*============================================================================*
 *  ADC (sense-amp scaling is in config.h)
 *============================================================================*/
const float ADC_VREF   = 4.096;       // ADC reference (V)
const float ADC_COUNTS = 65535.0;     // 16-bit

/*============================================================================*
 *  Pins
 *============================================================================*/
const int selectPin0   = 14;          // function select (DAC / ADC / none)
const int selectPin1   = 15;
const int boardSelect2 = 16;          // 3-bit board (slot) select
const int boardSelect1 = 17;
const int boardSelect0 = 18;
const int CS_BB        = 30;          // chip select driven low during ADC reads

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
 *============================================================================*/
volatile uint16_t data_tx[2] = {};
volatile uint16_t data_rx[2] = {};
const uint32_t SPI_CLOCK_HZ = 500000;   // slow clock for signal integrity

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
  SPI.begin();
  SPI1.begin();     // not used for reads in this version; kept as in the original
}

// Called once at the end of setup(). Nothing to do for this ADC.
void adcInit() {
  delay(500);
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
  SPI.beginTransaction(SPISettings(SPI_CLOCK_HZ, MSBFIRST, SPI_MODE0));
  SPI.transfer16(data_tx[0]);
  SPI.transfer16(data_tx[1]);
  SPI.endTransaction();
  selectNone();
  sei();
  delayMicroseconds(100);
}

/*============================================================================*
 *  ADC read (LTC1867)
 *  Frame 1 selects the channel; raising CS starts the conversion. Frame 2
 *  clocks out that result (and sets up a conversion we ignore).
 *  Returns the 16-bit code (0..65535).
 *============================================================================*/
uint16_t readAdcCode(uint8_t c) {
  selectNone();
  selectADC();
  digitalWrite(CS_BB, 0);
  // command word: single-ended, channel, unipolar
  data_tx[0] = ((0x80 | (channelMap_ADC[c] << 4) | 0x04) << 8);
  SPI.beginTransaction(SPISettings(SPI_CLOCK_HZ, MSBFIRST, SPI_MODE0));
  data_rx[0] = SPI.transfer16(data_tx[0]);
  SPI.endTransaction();
  selectNone();                     // CS high -> start conversion
  delayMicroseconds(20);            // conversion time

  selectNone();
  selectADC();
  digitalWrite(CS_BB, 0);
  data_tx[1] = ((0x80 | (c << 4) | 0x04) << 8);
  SPI.beginTransaction(SPISettings(SPI_CLOCK_HZ, MSBFIRST, SPI_MODE0));
  data_rx[1] = SPI.transfer16(data_tx[1]);
  SPI.endTransaction();
  selectNone();
  delayMicroseconds(20);

  // NOTE: CS_BB is left LOW here, as in the original code. The 12-bit version
  // sets it back HIGH after each read. Check whether your readback board
  // expects it to be released.
  return data_rx[1];
}
