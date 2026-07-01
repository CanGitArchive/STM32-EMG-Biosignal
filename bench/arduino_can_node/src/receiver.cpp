// receiver.cpp : Arduino UNO + MCP2515 (8 MHz) that prints every CAN frame received (serial monitor at 115200).
#include <Arduino.h>
#include <SPI.h>
#include <mcp_can.h>

const int SPI_CS_PIN = 10;          // MCP2515 CS -> Arduino D10
MCP_CAN CAN(SPI_CS_PIN);

void setup() {
  Serial.begin(115200);
  while (CAN.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ) != CAN_OK) {   // same 500 kbps + 8 MHz as the sender
    Serial.println(F("MCP2515 init FAILED (check wiring/crystal), retrying..."));
    delay(500);
  }
  CAN.setMode(MCP_NORMAL);
  Serial.println(F("Receiver ready, waiting for frames..."));
}

void loop() {
  if (CAN.checkReceive() == CAN_MSGAVAIL) {     // a frame is waiting (polled; no INT wire needed)
    unsigned long id = 0;
    byte len = 0;
    byte data[8];
    CAN.readMsgBuf(&id, &len, data);

    // Gripper frames (2 bytes): 0x100 = gesture event, 0x101 = status heartbeat. Decode for the demo.
    if ((id == 0x100 || id == 0x101) && len == 2) {
      Serial.print(id == 0x100 ? F("GESTURE  servo=") : F("Status   servo="));
      Serial.print(data[0] ? F("CLOSED") : F("OPEN"));
      Serial.print(F("   electrode="));
      Serial.println(data[1] ? F("ATTACHED") : F("DETACHED"));
      return;
    }

    // Anything else: raw byte dump (the generic bench view).
    Serial.print(F("Got frame  id=0x")); Serial.print(id, HEX);
    Serial.print(F("  len="));           Serial.print(len);
    Serial.print(F("  data="));
    for (byte i = 0; i < len; i++) {
      if (data[i] < 0x10) Serial.print('0');
      Serial.print(data[i], HEX); Serial.print(' ');
    }
    Serial.println();
  }
}
