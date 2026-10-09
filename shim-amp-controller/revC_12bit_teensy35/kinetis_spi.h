/******************************************************************************
 * kinetis_spi.h  -  minimal SPI driver for the 12-bit (Teensy 3.5) version
 *
 * Replaces the modified T3SPI library the firmware used to depend on, so the
 * sketch builds without installing any library. Written for this project and
 * covered by the repository's MIT license.
 *
 * It sets the SPI registers to the same final values the firmware obtained
 * through T3SPI. This was checked register by register in a simulation of
 * the Teensy 3.5 SPI registers (Oct 2026):
 *
 *   SPI0  master  16-bit frames, SPI mode 0, MSB first, PCS0 (pin 10) active
 *                 low, pins SCK 13 / MOSI 11 / MISO 12
 *   SPI1  slave   16-bit frames, SPI mode 1, receive-FIFO interrupt enabled,
 *                 pins SCK1 32 / MOSI1 0 / MISO1 1 / CS1 31
 *
 * Note on bit order: the firmware asked T3SPI for "LSB_FIRST", but T3SPI
 * compared that value against Arduino's MSBFIRST, so the bus actually ran
 * MSB first, which is what the LTC2656 DAC and LTC1863 ADC require. This
 * driver sets MSB first explicitly.
 ******************************************************************************/
#pragma once
#include <Arduino.h>     // brings in kinetis.h and core_pins.h on Teensy 3.x

// Clock divider codes (value of the CTAR BR and CSSCK fields, with DBR set),
// same numbering as T3SPI's SPI_CLOCK_DIVn constants.
const uint8_t KSPI_CLOCK_DIV8  = 0x3;
const uint8_t KSPI_CLOCK_DIV16 = 0x4;
const uint8_t KSPI_CLOCK_DIV32 = 0x5;

const uint8_t KSPI_PCS0 = 0x01;      // peripheral chip select 0 (pin 10)
const uint32_t KSPI_SR_TXCTR = 0x0000F000;   // SR: transmit FIFO counter field

class KinetisSpi {
public:
  explicit KinetisSpi(KINETISK_SPI_t *port) : spi(port) {}

  volatile int dataPointer = 0;      // receive position within a packet
  volatile int packetCT    = 0;      // packets completed (sent or received)

  // SPI0 as master: 16-bit frames, mode 0, MSB first, PCS0 active low.
  void beginMaster16(uint8_t clockDiv) {
    SIM_SCGC6 |= SIM_SCGC6_SPI0 | SIM_SCGC6_SPI1;   // clock both SPI modules
    halt();
    spi->MCR = SPI_MCR_MSTR | SPI_MCR_HALT | SPI_MCR_MDIS
             | SPI_MCR_PCSIS(KSPI_PCS0);          // PCS0 idles high (active low)
    spi->CTAR0 = SPI_CTAR_FMSZ(15)                // 16-bit frames
               | SPI_CTAR_DBR | SPI_CTAR_CSSCK(clockDiv) | SPI_CTAR_BR(clockDiv);
               // CPOL = CPHA = 0 (mode 0), LSBFE = 0 (MSB first)
    CORE_PIN13_CONFIG = PORT_PCR_DSE | PORT_PCR_MUX(2);   // SCK
    CORE_PIN11_CONFIG = PORT_PCR_DSE | PORT_PCR_MUX(2);   // MOSI
    CORE_PIN12_CONFIG = PORT_PCR_MUX(2);                  // MISO
    CORE_PIN10_CONFIG = PORT_PCR_DSE | PORT_PCR_MUX(2);   // PCS0
    run();
  }

  // SPI1 as slave: 16-bit frames, mode 1, interrupt on each received word.
  void beginSlave16Mode1() {
    SIM_SCGC6 |= SIM_SCGC6_SPI0 | SIM_SCGC6_SPI1;
    halt();
    spi->MCR = SPI_MCR_HALT | SPI_MCR_MDIS;       // slave mode
    spi->CTAR0 = SPI_CTAR_FMSZ(15) | SPI_CTAR_CPHA; // 16-bit, mode 1
    run();
    // Enable the interrupt only once the module is running, in the same
    // order T3SPI used (begin_SLAVE wrote RSER after restarting the port).
    spi->RSER = SPI_RSER_RFDF_RE;                 // interrupt: receive FIFO not empty
    CORE_PIN32_CONFIG = PORT_PCR_DSE | PORT_PCR_MUX(2);   // SCK1
    CORE_PIN0_CONFIG  = PORT_PCR_DSE | PORT_PCR_MUX(2);   // MOSI1
    CORE_PIN1_CONFIG  = PORT_PCR_MUX(2);                  // MISO1
    CORE_PIN31_CONFIG = PORT_PCR_MUX(2);                  // CS1
  }

  // Master: send n 16-bit words, waiting for each transfer to complete.
  void tx16(volatile uint16_t *words, int n, uint8_t pcs) {
    for (int i = 0; i < n; i++) {
      while ((spi->SR & KSPI_SR_TXCTR) >= 0x00004000) {}   // room in TX FIFO
      spi->PUSHR = (words[i] & 0xFFFF) | SPI_PUSHR_CTAS(0) | SPI_PUSHR_PCS(pcs & 0x1F);
      while ((spi->SR & KSPI_SR_TXCTR) != 0) {}            // FIFO drained
      while (!(spi->SR & SPI_SR_TCF)) {}                  // transfer complete
      spi->SR |= SPI_SR_TCF;                              // clear flag(s)
    }
    packetCT++;
  }

  // Slave, called from the SPI interrupt: store one received word; after
  // n words, count a packet and start over.
  void rx16(volatile uint16_t *buf, int n) {
    buf[dataPointer] = spi->POPR;
    dataPointer++;
    if (dataPointer == n) {
      dataPointer = 0;
      packetCT++;
    }
    spi->SR |= SPI_SR_RFDF;                               // clear flag(s)
  }

private:
  KINETISK_SPI_t *spi;
  void halt() { spi->MCR |= SPI_MCR_HALT | SPI_MCR_MDIS; }
  void run()  { spi->MCR &= ~SPI_MCR_HALT & ~SPI_MCR_MDIS; }
};
