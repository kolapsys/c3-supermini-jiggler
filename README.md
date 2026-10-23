# ESP32-C3 Super Mini BLE Mouse Jiggler

This project makes the ESP32-C3 Super Mini act as a standalone BLE mouse. USB
provides power and serial logging; the computer pairs with **BT Mouse** over
Bluetooth. No physical mouse is connected to the C3. When enabled and connected,
the C3 waits 10 seconds, then moves the cursor around a 10-by-10-pixel square
and sends a brief left-click. It repeats every 60 seconds.

Because the C3 does not receive reports from the computer's physical mouse, it
cannot detect movement or pause when that mouse is moved. The jiggle schedule is
time-based.

Jiggling starts disabled. Press the BOOT button to toggle it on or off. The
onboard blue LED blinks once per second while jiggling is enabled and is off
while disabled. Enabling the function starts the 10-second initial delay.

## Hardware and software

- ESP32-C3 Super Mini
- A computer with Bluetooth Low Energy HID support
- PlatformIO Core and a USB data cable

The button and LED pins assume the common Super Mini layout: BOOT on GPIO9 and
the active-low blue status LED on GPIO8.

The firmware uses PlatformIO with Arduino and NimBLE-Arduino. The C3 runs as a
BLE HID peripheral and supports one computer connection. USB CDC is enabled at
boot so startup and BLE status messages are available through the USB serial
port.

## Build and flash

From this directory:

```sh
pio run
pio run --target upload
pio device monitor
```

If PlatformIO does not select the correct USB serial port, specify it with
`--upload-port` and `--port`.
Open the serial monitor at 115200 baud before resetting the board to capture
startup messages.

## Pairing

1. Flash and restart the C3.
2. In the computer's Bluetooth settings, pair **BT Mouse** as a mouse.
3. Keep the computer paired and connected. While enabled, the C3 waits 10
   seconds, sends the square movement and left-click, then repeats every 60
   seconds.
4. Press BOOT to enable jiggling; it starts disabled. Press BOOT again to
   disable it. A blinking blue LED means enabled; the LED is off when disabled.
5. Check the serial monitor for BLE connection and jiggle-cycle messages.

## Test the idle timer

The hardware-independent idle timer tests can be run on a host with a C++17
compiler:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -Iinclude tests/mouse_activity_tests.cpp -o /tmp/mouse_activity_tests
/tmp/mouse_activity_tests
```

The 10-second initial delay, 60-second repeat interval, LED blink rate, and
10-pixel square are defined near the top of `src/main.cpp`.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE).
