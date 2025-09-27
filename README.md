# resQNet
a LoRa-based alternate communication channel for mobile phones to use in disaster zones where traditional GSM isn't feasible.

## Components used:
* ESP32 (to host the AP and run other logic)
* LoRa-02 SX1278 (433 Mhz)
* 18650 battery (2200mah)
* Type-C 5V 2A battery charging circuit (to charge the battery)
* power switch (for turning the device on/off)
* WS2812B x3 strip (for indicating different status of the device)
* 5V passive piezo buzzer (for SOS beeps and incoming message alert)
* small tactile switch (for SOS button)
* antennae
* jumper wires
