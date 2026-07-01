// Mcp2515Registers : the MCP2515 datasheet as code, every command/address/value with its datasheet name.
#ifndef MCP2515_REGISTERS_H
#define MCP2515_REGISTERS_H
#include <stdint.h>

// === SPI command bytes (CMD = an instruction the chip runs; datasheet Table 12-1, shown in binary) ===
static const uint8_t CMD_RESET_CHIP     = 0b11000000;  // RESET: wipe ALL registers to power-on defaults (lands in Config mode)
static const uint8_t CMD_READ_REGISTER  = 0b00000011;  // READ:  read the register at the address that follows
static const uint8_t CMD_WRITE_REGISTER = 0b00000010;  // WRITE: write the value that follows, to that address
static const uint8_t CMD_TRANSMIT_NOW   = 0b10000001;  // RTS, TX buffer 0: "send what's loaded, now"

// === Register addresses (the PLACE; the read/write verb happens in the driver; Register Map, hex) ===
static const uint8_t GET_CURRENT_MODE        = 0x0E;  // CANSTAT:  read the mode the chip is in right now
static const uint8_t SET_MODE                = 0x0F;  // CANCTRL:  write to request a mode (normal/loopback/config)
static const uint8_t GET_SET_INTERRUPT_STATE = 0x2C;  // CANINTF:  read flags / write to clear; bit 0 = a frame arrived

static const uint8_t BIT_TIMING_1 = 0x2A;  // CNF1: baud prescaler + sync jump width
static const uint8_t BIT_TIMING_2 = 0x29;  // CNF2: propagation + phase-1 segment
static const uint8_t BIT_TIMING_3 = 0x28;  // CNF3: phase-2 segment

static const uint8_t SET_ID_LEFT8_BITS  = 0x31; // TXB0SIDH: outgoing ID, left/top 8 bits
static const uint8_t SET_ID_RIGHT3_BITS = 0x32; // TXB0SIDL: outgoing ID, right/bottom 3 bits (in bits 7..5)
static const uint8_t SET_DATA_LENGTH    = 0x35; // TXB0DLC:  how many DATA bytes this frame carries (0..8)
static const uint8_t SET_DATA0          = 0x36; // TXB0D0:   first outgoing DATA byte (DATA0..DATA7 = 0x36..0x3D)

static const uint8_t SET_FILTER_MODE    = 0x60; // RXB0CTRL: which incoming IDs receive buffer 0 keeps
static const uint8_t GET_ID_LEFT8_BITS  = 0x61; // RXB0SIDH: received ID, left/top 8 bits
static const uint8_t GET_ID_RIGHT3_BITS = 0x62; // RXB0SIDL: received ID, right/bottom 3 bits
static const uint8_t GET_DATA_LENGTH    = 0x65; // RXB0DLC:  how many DATA bytes arrived
static const uint8_t GET_DATA0          = 0x66; // RXB0D0:   first received DATA byte (0x66..0x6D)

// === Values written INTO registers (datasheet bit-diagrams, shown in binary) ===
static const uint8_t SET_NORMAL_MODE       = 0b00000000;  // into SET_MODE: REQOP=000, live on the bus
static const uint8_t SET_LOOPBACK_MODE     = 0b01000000;  // into SET_MODE: REQOP=010, TX loops back to RX internally
static const uint8_t RX_ACCEPT_ANY         = 0b01100000;  // into SET_FILTER_MODE: RXM=11, keep every frame (filters off)
static const uint8_t CLEAR_INTERRUPT_STATE = 0b00000000;  // into GET_SET_INTERRUPT_STATE: all flags back to 0

// Bit timing for an 8 MHz crystal @ 500 kbps (8 TQ per bit, sample point ~62.5%); every node must match it.
static const uint8_t BIT_TIMING_1_VALUE_8MHZ_500K = 0b00000000; // CNF1: SJW=00 (1 TQ) | BRP=000000 (TQ = 2/8MHz)
static const uint8_t BIT_TIMING_2_VALUE_8MHZ_500K = 0b10010000; // CNF2: BTLMODE=1 | SAM=0 | PHSEG1=010 | PRSEG=000
static const uint8_t BIT_TIMING_3_VALUE_8MHZ_500K = 0b10000010; // CNF3: SOF=1 | WAKFIL=0 | PHSEG2=010

#endif
