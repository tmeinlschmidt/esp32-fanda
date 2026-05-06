# fanda

A small ESP-IDF application for an ESP32-C3 SuperMini that drives an LED, reads a push button and a reed switch, and plays short tones through a MAX98357A I2S amplifier. Verify that your board's silkscreen D-labels match the GPIO mapping in the table below before flashing.

## Behaviour

- When the push button is pressed: play a short tone and blink the LED twice.
- When the reed switch is disconnected (magnet removed): play a different, lower and longer tone once.

## Hardware

| Silkscreen | GPIO    | Wired to                                                            |
|-----------:|--------:|---------------------------------------------------------------------|
| D6         | GPIO6   | LED anode (via 220-330 Ohm resistor to GND on cathode)              |
| D10        | GPIO10  | Push button, other leg to GND (firmware enables internal pull-up)   |
| D1         | GPIO1   | Reed switch, other leg to GND (firmware enables internal pull-up)   |
| D9         | GPIO9   | I2S DIN  -> MAX98357A DIN                                           |
| D8         | GPIO8   | I2S BCLK -> MAX98357A BCLK                                          |
| D7         | GPIO7   | I2S LRC  -> MAX98357A LRC                                           |

Power the MAX98357A from VIN (3.3 V or 5 V) and GND. The SD pin can be tied to VIN for always-on / both-channels-mixed behaviour. Leave the GAIN pin floating for the default 9 dB.

### Wiring notes

- **Button (D10):** active LOW. One leg to GPIO10, the other to GND. The firmware enables the internal pull-up, so no external resistor is required.
- **Reed switch (D1):** closed = magnet present = GPIO reads LOW; disconnect (magnet removed) = open = GPIO reads HIGH. The firmware enables the internal pull-up.
- **LED (D6):** drive through a 220-330 Ohm series resistor on the cathode side to GND.
- **MAX98357A:** SD pin tied to VIN keeps the amp on and mixes both stereo channels. GAIN pin floating gives 9 dB; tie it to GND, VIN, or via a resistor for other gain settings (see datasheet).

Caution: GPIO8 and GPIO9 are strapping pins on the ESP32-C3. If the board fails to boot with the amp connected, briefly disconnect DIN/BCLK while resetting, or move the I2S signals to non-strapping GPIOs and update `main/pins.h`.

## Project layout

```
fanda/
├── CMakeLists.txt
├── Makefile
├── README.md
├── sdkconfig.defaults
└── main/
    ├── CMakeLists.txt
    ├── main.c
    ├── audio.c
    ├── audio.h
    └── pins.h
```

## Prerequisites

Install ESP-IDF v5.x. Follow the official guide for macOS or Linux:
<https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/get-started/index.html>

Quick steps:

```sh
mkdir -p ~/esp && cd ~/esp
git clone -b release/v5.3 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32c3
```

Activate the environment in every new shell:

```sh
. ~/esp/esp-idf/export.sh
```

Verify:

```sh
idf.py --version
```

## Build & flash

One-time, set the chip target:

```sh
make set-target
# or, equivalently:
idf.py set-target esp32c3
```

Build:

```sh
make build
```

Flash. `idf.py` auto-detects the serial port; if it cannot, override with `PORT=`:

```sh
make flash
# macOS override:
make flash PORT=/dev/cu.usbmodem1101
# Linux override:
make flash PORT=/dev/ttyACM0
```

Find the port:

```sh
# macOS
ls /dev/cu.*
# Linux
ls /dev/ttyACM* /dev/ttyUSB*
```

Watch the serial logs (exit with Ctrl-]):

```sh
make monitor
```

Common dev loop (build + flash + monitor):

```sh
make flashmonitor
# or
make fm
```

Other useful targets:

- `make clean` — remove build artefacts for the current target.
- `make fullclean` — wipe the entire `build/` directory.
- `make erase` — erase the chip's flash.
- `make menuconfig` — open the ESP-IDF configuration UI.

## Troubleshooting

- **Serial port not found:** macOS may need CP210x or CH9102 USB-UART drivers depending on the board's USB-serial chip. Compare `ls /dev/cu.*` before and after plugging the board in to identify the device.
- **Permission denied on Linux:** add your user to the `dialout` group, then log out and back in:
  ```sh
  sudo usermod -aG dialout $USER
  ```
- **Board doesn't boot after flashing:** GPIO8 and GPIO9 are ESP32-C3 strapping pins. Briefly unplug the I2S amp during reset, or move the I2S signals to non-strapping GPIOs and update `main/pins.h`.
- **No sound:** make sure the MAX98357A SD pin is not pulled to GND (that mutes the amp), and that GAIN is floating or tied to a sensible level.
- **Button does nothing:** verify wiring (one leg to GPIO10, the other to GND). The internal pull-up is enabled in firmware, so a multimeter on the GPIO should read ~3.3 V with the button open and 0 V when pressed.

## License

License: TBD.
