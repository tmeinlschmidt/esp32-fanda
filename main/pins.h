// pins.h — D-label silkscreen → GPIO mapping for Seeed Studio XIAO ESP32-C3.
// The XIAO C3 uses Arduino-style D0..D10 labels that do NOT match GPIO numbers:
//   D0=GPIO2,  D1=GPIO3,  D2=GPIO4,  D3=GPIO5,  D4=GPIO6,  D5=GPIO7,
//   D6=GPIO21, D7=GPIO20, D8=GPIO8,  D9=GPIO9,  D10=GPIO10.
// D6 (GPIO21) and D7 (GPIO20) are UART0 TX/RX by default; we route the
// console to USB-Serial-JTAG via sdkconfig.defaults so these pads are free.
#pragma once

#include "driver/gpio.h"

#define PIN_LED      GPIO_NUM_21  // D6  — LED, active HIGH (GPIO -> resistor -> LED anode, cathode to GND)
#define PIN_BUTTON   GPIO_NUM_10  // D10 — push button to GND, internal pull-up
#define PIN_REED     GPIO_NUM_3   // D1  — reed switch to GND, internal pull-up
#define PIN_I2S_DIN  GPIO_NUM_20  // D7  — I2S data to MAX98357A
#define PIN_I2S_BCLK GPIO_NUM_8   // D8  — I2S bit clock
#define PIN_I2S_LRC  GPIO_NUM_9   // D9  — I2S word select / LRC
