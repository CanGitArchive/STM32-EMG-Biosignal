// sender.cpp : Arduino UNO + MCP2515 (8 MHz) that sends one CAN frame every 500 ms (coryjfowler MCP_CAN library).
#include <Arduino.h>
#include <SPI.h>
#include <mcp_can.h>

const int SPI_CS_PIN = 10;          // MCP2515 CS -> Arduino D10
MCP_CAN CAN(SPI_CS_PIN);

void setup() {
  Serial.begin(115200);
  // 500 kbps bus, 8 MHz module crystal. BOTH nodes must match speed + crystal.
  while (CAN.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ) != CAN_OK) {
    Serial.println(F("MCP2515 init FAILED (check wiring/crystal), retrying..."));
    delay(500);
  }
  CAN.setMode(MCP_NORMAL);          // actually drive the bus (not loopback / listen-only)
  Serial.println(F("Sender ready."));
}

byte counter = 0;
void loop() {
  byte data[8] = { 'E', 'M', 'G', counter, 0, 0, 0, 0 };   // 'E''M''G' + a rolling counter
  byte result = CAN.sendMsgBuf(0x100, 0, 8, data);         // id=0x100, standard frame, 8 bytes
  if (result == CAN_OK) { Serial.print(F("Sent, counter=")); Serial.println(counter); }
  else                  { Serial.print(F("Send FAILED, err=")); Serial.println(result); }
  counter++;
  delay(500);
}
