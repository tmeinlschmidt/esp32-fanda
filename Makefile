# Thin wrapper around idf.py. Usage: `make flash PORT=/dev/cu.usbmodem1101`

PORT ?=
PORT_FLAG = $(if $(PORT),-p $(PORT),)

.PHONY: build flash monitor flashmonitor fm clean fullclean menuconfig set-target erase

build:
	idf.py build

flash:
	idf.py $(PORT_FLAG) flash

monitor:
	idf.py $(PORT_FLAG) monitor

flashmonitor fm:
	idf.py $(PORT_FLAG) flash monitor

clean:
	idf.py clean

fullclean:
	idf.py fullclean

menuconfig:
	idf.py menuconfig

set-target:
	idf.py set-target esp32c3

erase:
	idf.py $(PORT_FLAG) erase-flash
