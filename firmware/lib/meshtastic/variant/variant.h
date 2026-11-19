// twatch: headless T-Watch S3 variant for the vendored Meshtastic core.
// Screen, touch, audio, haptics, accelerometer, RTC and power stay with the watch firmware;
// the core only owns the SX1262. Pins from Meshtastic's variants/esp32s3/t-watch-s3/variant.h.
#pragma once

#define USE_SX1262

#define LORA_SCK 3
#define LORA_MISO 4
#define LORA_MOSI 1
#define LORA_CS 5
#define LORA_DIO0 -1
#define LORA_RESET 8
#define LORA_DIO1 9 // SX1262 IRQ
#define LORA_DIO2 7 // SX1262 BUSY

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

#define HAS_GPS 0
#define HAS_SCREEN 0
