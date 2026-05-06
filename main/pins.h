// pins.h — D-label silkscreen → GPIO mapping for ESP32-C3 SuperMini.
// On this board the "D" labels match GPIO numbers directly.
#pragma once

#include "driver/gpio.h"

#define PIN_LED      GPIO_NUM_6   // D6  — LED, active HIGH
#define PIN_BUTTON   GPIO_NUM_10  // D10 — push button to GND, internal pull-up
#define PIN_REED     GPIO_NUM_1   // D1  — reed switch to GND, internal pull-up
#define PIN_I2S_DIN  GPIO_NUM_9   // D9  — I2S data to MAX98357A
#define PIN_I2S_BCLK GPIO_NUM_8   // D8  — I2S bit clock
#define PIN_I2S_LRC  GPIO_NUM_7   // D7  — I2S word select / LRC
