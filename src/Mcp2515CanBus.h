// Mcp2515CanBus : the CAN-bus controller (an MCP2515 chip on the end of SPI2). The STM32 controls it
// entirely by reading/writing its registers over SPI. Step 1 only brings up SPI + reads one register.
#ifndef MCP2515_CAN_BUS_H
#define MCP2515_CAN_BUS_H
#include "stm32f4xx_hal.h"

class Mcp2515CanBus
{
  public:
    void init()
    {
        __HAL_RCC_GPIOB_CLK_ENABLE();      // SCK/MISO/MOSI/CS all live on port B
        __HAL_RCC_SPI2_CLK_ENABLE();       // turn on power to SPI peripheral 2

        // PB12 = chip-select. A plain output WE drive by hand (not the SPI block). Idle HIGH = "not talking".
        GPIO_InitTypeDef cs = {0};
        cs.Pin = GPIO_PIN_12; cs.Mode = GPIO_MODE_OUTPUT_PP; cs.Pull = GPIO_NOPULL;
        cs.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        HAL_GPIO_Init(GPIOB, &cs);
        deselect();                        // start HIGH so the chip isn't selected until we want it

        // PB13/14/15 = SCK/MISO/MOSI, handed to the SPI2 peripheral ("alternate function 5").
        GPIO_InitTypeDef spi = {0};
        spi.Pin = GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
        spi.Mode = GPIO_MODE_AF_PP; spi.Pull = GPIO_NOPULL;
        spi.Speed = GPIO_SPEED_FREQ_VERY_HIGH; spi.Alternate = GPIO_AF5_SPI2;
        HAL_GPIO_Init(GPIOB, &spi);

        hspi.Instance               = SPI2;
        hspi.Init.Mode              = SPI_MODE_MASTER;          // the STM32 drives the clock
        hspi.Init.Direction         = SPI_DIRECTION_2LINES;    // separate MOSI + MISO (full duplex)
        hspi.Init.DataSize          = SPI_DATASIZE_8BIT;       // one byte at a time
        hspi.Init.CLKPolarity       = SPI_POLARITY_LOW;        // SPI mode 0: clock rests low...
        hspi.Init.CLKPhase          = SPI_PHASE_1EDGE;         // ...and data is read on the first (rising) edge
        hspi.Init.NSS               = SPI_NSS_SOFT;            // we do chip-select ourselves (PB12), not the block
        hspi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8; // 16 MHz / 8 = 2 MHz: gentle + easy for the analyzer
        hspi.Init.FirstBit          = SPI_FIRSTBIT_MSB;        // most-significant bit first (what the MCP2515 expects)
        hspi.Init.TIMode            = SPI_TIMODE_DISABLE;
        hspi.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
        HAL_SPI_Init(&hspi);
    }

    // Send the one-byte RESET command, then wait for the chip to finish its internal reset. After this
    // the MCP2515 is in Configuration mode, so CANSTAT reads 0x80.
    void reset()
    {
        uint8_t command = CMD_RESET;
        select();
        HAL_SPI_Transmit(&hspi, &command, 1, 100);   // 100 = give-up timeout (ms)
        deselect();
        HAL_Delay(10);                               // let the reset settle before any more SPI
    }

    // Write one register: clock out [WRITE, address, value] in one CS-low...CS-high. The chip has no
    // reply for a write, so this is a plain transmit (no dummy byte, nothing to read back here).
    void writeRegister(uint8_t address, uint8_t value)
    {
        uint8_t toSend[3] = { CMD_WRITE, address, value };
        select();
        HAL_SPI_Transmit(&hspi, toSend, 3, 100);
        deselect();
    }

    // Read one register: clock out [READ, address, dummy]; the chip clocks the value back during the 3rd byte.
    uint8_t readRegister(uint8_t address)
    {
        uint8_t toSend[3]    = { CMD_READ, address, 0x00 };   // 3rd byte is dummy: it just clocks the answer out
        uint8_t received[3]  = { 0, 0, 0 };
        select();
        HAL_SPI_TransmitReceive(&hspi, toSend, received, 3, 100);
        deselect();
        return received[2];                          // the register value rode back in on the 3rd byte
    }

    uint8_t readCanstat() { return readRegister(REG_CANSTAT); }   // 0x80 right after reset = "Configuration mode"

    // Set the wire speed (8 MHz crystal @ 500 kbps). Every node on a CAN bus MUST agree on this exactly,
    // or they can't decode each other. Writable only while in Configuration mode (i.e. right after reset).
    void setBitTiming8MHz500k()
    {
        writeRegister(REG_CNF1, CNF1_8MHZ_500K);
        writeRegister(REG_CNF2, CNF2_8MHZ_500K);
        writeRegister(REG_CNF3, CNF3_8MHZ_500K);
    }

    // Leave Configuration mode and go live: REQOP = 000 in CANCTRL = Normal mode. After this the chip
    // participates on the bus, and CANSTAT's mode bits read 000 (so CANSTAT = 0x00).
    void enterNormalMode()
    {
        writeRegister(REG_CANCTRL, 0x00);   // 0x00: Normal mode, one-shot off, CLKOUT off
    }

    // Loopback mode (REQOP = 010): the chip routes its own transmit straight into its own receiver,
    // internally. No bus wires, no second node, no ACK needed. Perfect for testing frame code alone.
    void enterLoopbackMode() { writeRegister(REG_CANCTRL, 0b01000000); }

    // Tell receive buffer 0 to accept ANY frame (RXM = 11 switches the ID filters/masks off). Without it,
    // the freshly-reset filters could drop our frame.
    void acceptAllOnRxBuffer0() { writeRegister(REG_RXB0CTRL, 0b01100000); }

    // Build a standard (11-bit ID) data frame in TX buffer 0 and fire it. length = 0..8 data bytes.
    void sendFrame(uint16_t id, const uint8_t *data, uint8_t length)
    {
        writeRegister(REG_TXB0SIDH, (uint8_t)(id >> 3));            // the ID's top 8 bits
        writeRegister(REG_TXB0SIDL, (uint8_t)((id & 0x07) << 5));   // the ID's bottom 3 bits, parked in bits 7..5
        writeRegister(REG_TXB0DLC, length);                        // how many data bytes follow
        for (uint8_t i = 0; i < length; i++)
            writeRegister(REG_TXB0D0 + i, data[i]);                // the payload, byte by byte
        requestToSendTxb0();                                       // "send it now"
    }

    // Did a frame land in receive buffer 0? RX0IF is bit 0 of the interrupt-flag register.
    bool messageWaiting() { return (readRegister(REG_CANINTF) & 0x01) != 0; }

    // Read the frame out of receive buffer 0 into id + data[]; returns the data length. Clears the flags
    // afterwards so the next frame can be detected.
    uint8_t readFrame(uint16_t *id, uint8_t *data)
    {
        uint8_t sidh = readRegister(REG_RXB0SIDH);
        uint8_t sidl = readRegister(REG_RXB0SIDL);
        *id = (uint16_t)(sidh << 3) | (sidl >> 5);     // rebuild the 11-bit ID from its two halves
        uint8_t length = readRegister(REG_RXB0DLC) & 0x0F;
        for (uint8_t i = 0; i < length; i++)
            data[i] = readRegister(REG_RXB0D0 + i);
        writeRegister(REG_CANINTF, 0x00);              // clear all flags (incl. RX0IF) so the next frame shows
        return length;
    }

  private:
    void select()   { HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET); }   // CS LOW  = start talking
    void deselect() { HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);   }   // CS HIGH = done talking

    // The "request to send" instruction is a single command byte (no address), unlike read/write.
    void requestToSendTxb0()
    {
        uint8_t command = CMD_RTS_TXB0;
        select();
        HAL_SPI_Transmit(&hspi, &command, 1, 100);
        deselect();
    }

    // MCP2515 SPI command bytes + the one register we read in step 1 (from the chip's datasheet).
    static const uint8_t CMD_RESET  = 0xC0;   // reset the whole chip to a known state
    static const uint8_t CMD_READ   = 0x03;   // "read register at the address that follows"
    static const uint8_t CMD_WRITE  = 0x02;   // "write the value that follows to the address that follows"
    static const uint8_t REG_CANSTAT = 0x0E;  // status register; top 3 bits = current operating mode
    static const uint8_t REG_CANCTRL = 0x0F;  // control register; top 3 bits = REQUESTED operating mode
    static const uint8_t REG_CNF3    = 0x28;  // bit-timing config 3
    static const uint8_t REG_CNF2    = 0x29;  // bit-timing config 2
    static const uint8_t REG_CNF1    = 0x2A;  // bit-timing config 1

    // Bit timing for an 8 MHz crystal @ 500 kbps (bench-proven values). Written in BINARY so each byte
    // lines up 1:1 with the datasheet's CNF register diagrams (C++ 0b literals are the same number as hex,
    // just a different notation the compiler parses). 8 TQ per bit, sample point ~62.5%.
    static const uint8_t CNF1_8MHZ_500K = 0b00000000;   // SJW[7:6]=00 (1 TQ) | BRP[5:0]=000000 (TQ = 2/8MHz = 250 ns)
    static const uint8_t CNF2_8MHZ_500K = 0b10010000;   // BTLMODE=1 | SAM=0 | PHSEG1[5:3]=010 (3 TQ) | PRSEG[2:0]=000 (1 TQ)
    static const uint8_t CNF3_8MHZ_500K = 0b10000010;   // SOF=1 | WAKFIL=0 | --- | PHSEG2[2:0]=010 (3 TQ)

    // TX buffer 0, RX buffer 0, interrupt flags + the request-to-send command (step 3: sending frames).
    static const uint8_t REG_TXB0SIDH = 0x31;  // transmit: standard ID, high bits
    static const uint8_t REG_TXB0SIDL = 0x32;  // transmit: standard ID, low bits
    static const uint8_t REG_TXB0DLC  = 0x35;  // transmit: data length code (how many data bytes)
    static const uint8_t REG_TXB0D0   = 0x36;  // transmit: first data byte (D0..D7 = 0x36..0x3D)
    static const uint8_t REG_RXB0CTRL = 0x60;  // receive buffer 0: control (filter mode)
    static const uint8_t REG_RXB0SIDH = 0x61;  // receive: standard ID, high bits
    static const uint8_t REG_RXB0SIDL = 0x62;  // receive: standard ID, low bits
    static const uint8_t REG_RXB0DLC  = 0x65;  // receive: data length code
    static const uint8_t REG_RXB0D0   = 0x66;  // receive: first data byte (0x66..0x6D)
    static const uint8_t REG_CANINTF  = 0x2C;  // interrupt flags; bit 0 = RX0IF (a frame arrived in RXB0)
    static const uint8_t CMD_RTS_TXB0 = 0x81;  // one-byte "request to send, TX buffer 0" instruction

    SPI_HandleTypeDef hspi = {};
};
#endif
