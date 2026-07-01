# Bill of materials

Parts to reproduce this build. Most were bought from Robotistan (Istanbul); prices are in Turkish Lira as paid
and drift over time. The build is two CAN nodes, so it includes a second MCP2515 and an Arduino.

| Part | Qty | Role | Price (TL) |
|---|---:|---|---:|
| ST Nucleo-F446RE | 1 | the MCU board (Cortex-M4F, onboard ST-LINK) | 1380 |
| MCP2515 CAN module (controller + transceiver, SPI) | 2 | one per CAN node (STM32 side and Arduino bench node) | 206 |
| TowerPro SG90 servo, 180° | 2 | gripper actuation (the 2nd is a spare) | 157 |
| USB logic analyzer, 24 MHz 8-channel | 1 | SPI/CAN protocol decode | 365 |
| Mini-USB cable, 150 cm | 1 | powers and flashes the Nucleo | 44 |
| Jumper wires F-F, M-M, M-F, 200 mm | 3 | breadboard wiring | ~155 |
| Grove EMG Detector (Seeed) | 1 | analog EMG envelope into the ADC | ~30 USD |

Already on hand: a breadboard, EMG electrodes, a 6 V (4xAAA) pack for the servo, an Arduino UNO for the second
CAN node, a multimeter, and about 120 Ω of bus termination. No oscilloscope is needed: the logic analyzer covers
the digital buses, and the analog EMG is read through the ADC and plotted from serial.
