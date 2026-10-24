# ESP32-C3 Super Mini BLE Mouse Jiggler

This project makes the ESP32-C3 Super Mini act as a standalone BLE mouse. USB
provides power and serial logging; the computer pairs with **BT Mouse** over
Bluetooth. No physical mouse is connected to the C3. When enabled and connected,
the C3 waits 10 seconds, then moves the cursor around a 10-by-10-pixel square
and sends a brief left-click. It repeats every 60 seconds.

![ESP32-S3 Super Mini pinout illustration supplied by the project owner](assets/pinout_esp32c3_super_mini.png)

*This supplied illustration is for an ESP32-S3 Super Mini, not the ESP32-C3
used by this project. Its pin labels and mapping do not apply to the C3.*

Because the C3 does not receive reports from the computer's physical mouse, it
cannot detect movement or pause when that mouse is moved. The jiggle schedule is
time-based.

Jiggling starts disabled. Press the BOOT button to toggle it on or off. The
onboard blue LED stays on while jiggling is enabled and is off while disabled.
At startup, if no computer is connected, the C3 advertises in pairing mode with
the LED blinking rapidly; saved bonds are preserved. Hold BOOT for 2 seconds to
restart pairing and erase all saved BLE bonds. The LED keeps blinking until a
BLE computer connects and remains connected for 2 seconds. Brief connections
that drop resume the pairing blink. After a stable connection, the LED flashes
three times over three seconds, then returns to the normal enabled/disabled
state. Enabling the function starts the 10-second initial delay.

## Hardware and software

- ESP32-C3 Super Mini
- A computer with Bluetooth Low Energy HID support
- PlatformIO Core and a USB data cable

This project uses BOOT on GPIO9 and the active-low blue status LED on GPIO8.
RESET restarts the board. Check the specific C3 board's silkscreen or
documentation for its pinout; the illustration above is for the S3 variant.

The firmware uses PlatformIO with Arduino and NimBLE-Arduino. The C3 runs as a
BLE HID peripheral and supports one computer connection. USB CDC is enabled at
boot so startup and BLE status messages are available through the USB serial
port.

## Easy install (recommended)

Use the browser installer on a desktop computer with the latest Chrome or Edge:

1. Connect the C3 with a USB data cable.
2. Open the [C3 Mouse Jiggler installer](https://kolapsys.github.io/c3-supermini-jiggler/).
3. Click **Install**, select the ESP32-C3 serial port, and approve installation.
4. Pair **BT Mouse** in your computer's Bluetooth settings. Installing clears
   saved BLE bonds, so pair the mouse again after each flash.

If the installer says no device is available, try a USB data cable that supports
data. You may also hold BOOT, press and release RESET, then release BOOT and
retry. The first time this repository is set up, enable GitHub Pages using
**Settings → Pages → Build and deployment → GitHub Actions**; updates to `main`
publish the latest installer automatically.

## Build and flash manually

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
   disable it. The blue LED stays on while enabled and is off when disabled.
   Hold BOOT for 2 seconds to disconnect the current computer and advertise for
   pairing. At startup the C3 enters pairing mode automatically if no computer
   is connected, preserving saved bonds. The LED blinks four times per second
   until a computer connects and remains connected for 2 seconds. It then
   flashes three times over three seconds before returning to the
   enabled/disabled state. Brief connections that drop resume the pairing blink.
   A 2-second BOOT hold disconnects the current host and erases all saved BLE
   bonds, so computers must pair again.
5. Check the serial monitor for BLE connection and jiggle-cycle messages.
   Disconnect messages include the reason text and how long the link was up.

## Test the idle timer

The hardware-independent idle timer tests can be run on a host with a C++17
compiler:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -Iinclude \
    tests/mouse_activity_tests.cpp -o /tmp/mouse_activity_tests
/tmp/mouse_activity_tests
```

The 10-second initial delay, 60-second repeat interval, LED blink rate, and
10-pixel square are defined near the top of `src/main.cpp`.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE).
