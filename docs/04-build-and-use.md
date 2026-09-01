# Joy-Con Bridge: build and use

Joy-Con Bridge turns one left and one right Nintendo Switch (generation 1)
Joy-Con into one Xbox-style Bluetooth controller for an Android device or
Windows PC. The firmware targets a classic ESP32-WROOM-32 development board
with 4 MB flash and no PSRAM.

This document separates instructions for building the firmware from
instructions for using a board that already has firmware installed.

## Build instructions

### Requirements

- A classic ESP32-WROOM-32 board. ESP32-S3, C3, and C6 boards are not the
  target.
- A USB data cable and the board's USB-UART driver, if required by the board
  (CP210x or CH340).
- A C99 compiler and `make` for host tests.
- ESP-IDF v5.3.1 for a firmware build and flash.
- Two Switch 1 Joy-Cons for hardware testing.

On Debian/Ubuntu, install the host and ESP-IDF prerequisites with:

```sh
sudo apt-get update
sudo apt-get install -y git wget flex bison gperf python3 python3-pip \
  python3-venv cmake ninja-build ccache libffi-dev libssl-dev \
  dfu-util libusb-1.0-0
```

Fedora and macOS package lists are documented in
[`firmware/README.md`](../firmware/README.md#dependencies--how-to-install-them).
Linux users normally also need USB-serial access:

```sh
sudo usermod -aG dialout "$USER"
```

Log in again after changing the group. The repository includes a setup script
that installs the supported Linux/macOS prerequisites, ESP-IDF, and the
Bluepad32 BTstack integration:

```sh
./scripts/install-dependencies.sh
```

The script installs ESP-IDF at `~/esp/esp-idf` by default. To use a different
location, set `IDF_PATH` before running it.

### Run host tests

These tests do not need ESP-IDF or hardware:

```sh
make -C firmware/test test
```

The expected result is `99 checks, 0 failed`.

### Configure and build

In every new shell, activate ESP-IDF, then build from the firmware directory:

```sh
. ~/esp/esp-idf/export.sh
cd firmware
idf.py set-target esp32
idf.py build
```

Before the first build, use `idf.py menuconfig` if the board needs settings
other than the defaults. The **Joy-Con Bridge** menu contains:

- Optional Config Mode Wi-Fi station SSID and password.
- SoftAP SSID and password (defaults: `joycon-bridge` and `joyconbridge`).
- Optional WS2812 status LED and its GPIO (default GPIO 8 when enabled).
- The default A/B button position and output report rate.

Leaving the station SSID empty uses the built-in SoftAP and is the simplest
first-time setup.

### Flash the board

Find the board's serial port (`/dev/ttyUSB0`, `/dev/ttyACM0`, or the relevant
Windows/macOS port), connect it by USB, and run:

```sh
cd firmware
idf.py -p <PORT> flash monitor
```

For example:

```sh
idf.py -p /dev/ttyACM0 flash monitor
```

The convenience script builds, flashes, and captures serial output for 15
seconds by default:

```sh
./scripts/build-flash-monitor.sh /dev/ttyACM0 15
```

### Flashing from loose files

Use this procedure when sending firmware to someone who will flash it with
[Espressif ESP LaunchPad](https://espressif.github.io/esp-launchpad/). LaunchPad
supports selecting multiple local binaries and assigning each one a flash
address.

After a successful `idf.py build`, send these three files from `firmware/build`
to the recipient:

| File | Flash address |
| --- | ---: |
| `bootloader/bootloader.bin` | `0x1000` |
| `partition_table/partition-table.bin` | `0x8000` |
| `joycon_bridge.bin` | `0x10000` |

The project uses a custom partition table with the application at `0x10000`.
Do not substitute `boot_app0.bin`, `ota_data_initial.bin`, or a `storage`
image: this is a single-application image and the web files are embedded in
`joycon_bridge.bin`. The `firmware/build/flash_args` file records the same
mapping for reference.

Send the files without renaming them, or include a note matching each renamed
file to the table above. The build directory is generated output; do not send
the ELF, MAP, object files, or the entire build directory.

#### Recipient flashing steps

1. Use a classic ESP32-WROOM-32 board with 4 MB flash, connect it with a USB
   data cable, and close other programs using the serial port.
2. Open [ESP LaunchPad](https://espressif.github.io/esp-launchpad/) in a
   Web Serial-capable browser such as Chrome or Edge. The site must be opened
   over HTTPS, as required by Web Serial.
3. Click **Connect** and select the board's USB serial port.
4. In the DIY/local firmware area, add the three files and enter the addresses
   exactly as shown in the table. Use **Program** to flash them.
5. Watch the LaunchPad console until it reports that flashing completed, then
   click **Reset Device**. Flashing overwrites the earlier firmware on the
   board.

#### Verify the flash

The recipient should provide the following evidence of success:

1. LaunchPad reports a completed flash with no error.
2. After **Reset Device**, the serial console is set to `115200` baud and
   shows startup messages including `Joy-Con Bridge`, `startup: nvs ready`,
   `startup: joycon host ready`, and `startup: BLE host ready`.
3. If the board has no saved pair of Joy-Cons, the log says it is entering
   Config Mode. The recipient can then connect to the `joycon-bridge` Wi-Fi
   network and open `http://192.168.4.1/` as described in
   [User instructions](#user-instructions-after-the-firmware-is-built).

If it does not work, do not paraphrase the failure. Send back:

- The complete LaunchPad console output from **Connect** through the failure
  or reset.
- Any browser error or LaunchPad error text, including the step at which it
  appeared.
- The board model, the detected chip/flash size if shown, operating system,
  browser, and serial port selected.
- Whether the board reset and what appears at `115200` baud afterward.

Common causes are selecting an ESP32-S3/C3/C6 instead of a classic ESP32,
using a charge-only USB cable, choosing the wrong serial port, or entering an
address incorrectly. If the board is not detected, check the USB-UART driver
and try holding BOOT while connecting or resetting the board.

The firmware currently contains the complete host-side logic, configuration
portal, and BLE HID implementation, but on-target Windows XInput and Android
compatibility testing remains part of the project validation work. See
[`03-implementation-status.md`](03-implementation-status.md) for the current
status.

## User instructions (after the firmware is built)

### First start and Config Mode

1. Connect the ESP32 to power.
2. A board with no two remembered Joy-Cons enters Config Mode automatically
   after boot.
3. If the board already has both Joy-Cons remembered, enter Config Mode from
   the serial console with `mode config` (or run
   `python3 scripts/serial-mode.py config`; that helper currently uses
   `/dev/ttyACM0`).
4. In the default setup, connect your phone or computer to Wi-Fi network
   `joycon-bridge` using password `joyconbridge`.
5. Open [`http://192.168.4.1/`](http://192.168.4.1/) in a browser.

If station Wi-Fi was configured at build time, the bridge first joins that
network. Open `http://joycon-bridge.local/` or the IP address printed by the
serial monitor. If the station connection fails, the bridge falls back to the
default SoftAP.

Config Mode suspends the downstream Xbox controller link while the portal is
open. Returning to Play Mode allows it to reconnect.

### Pair the Joy-Cons

1. In the web portal, click **Pair a Joy-Con**.
2. On a Joy-Con, hold the small sync button on its rail until it is available
   for Bluetooth pairing.
3. Repeat until one left and one right Joy-Con are shown as connected.
4. Click **Stop pairing** when finished. The two Joy-Con addresses are saved
   and will be used for automatic reconnection after reboot.

The portal shows connection state and battery level for each Joy-Con. A
missing half is reported as **degraded**; the available half remains usable,
and the full pair resumes when the missing Joy-Con reconnects.

### Connect the merged controller to a host

1. Click **Exit Config Mode**. If the serial console is available, `mode play`
   has the same effect.
2. On Windows or Android, open Bluetooth settings and pair with
   **Xbox Wireless Controller**.
3. Start a gamepad tester or a game and verify the sticks, D-pad, buttons,
   bumpers, triggers, and system buttons.

The downstream host bond is remembered. If the host does not reconnect, use
the portal's **Host → forget** control, return to Play Mode, and pair again.

### Adjust the controls

In Config Mode, the **Mapping** section provides:

- **A/B match printed label (Xbox position):** changes the default Nintendo
  face-button arrangement to physical label matching.
- **ZL/ZR map to full-scale triggers:** controls whether the digital Joy-Con
  triggers are reported as full trigger values.
- **Deadzone:** adjusts the stick deadzone.
- **Capture, L SL, L SR, R SL, R SR:** assigns those otherwise-special buttons
  to an Xbox control or leaves them unmapped.

Click **Save mapping** after changes. Use **Input test** to inspect live axes,
trigger values, and button bits before returning to Play Mode.

### Reset and status

- **Reboot** restarts the firmware without clearing settings.
- **Factory reset** clears saved mappings, Joy-Con bonds, and the host bond,
  then restarts the board.
- The planned BOOT/GPIO0 long-press and boot-time factory-reset controls are
  not wired into the current firmware. Use the web portal's **Factory reset**
  action instead.

If a WS2812 status LED is enabled, its patterns are:

| Pattern | Meaning |
| --- | --- |
| Blue pulse | Play Mode, searching or no complete connection |
| Green solid | Both Joy-Cons and the host are connected |
| Green pulse | A partial Play Mode connection |
| Amber blink | Degraded input: one Joy-Con is missing |
| Cyan pulse | Config Mode |
| Magenta blink | Joy-Con pairing is enabled |
| Red blink | Error |

### Useful recovery checks

- If the portal is unavailable and both Joy-Cons are already remembered, enter
  Config Mode with the serial `mode config` command and watch the monitor for
  the SoftAP or station IP address.
- If a Joy-Con is not found, click **Pair a Joy-Con** before pressing its sync
  button, and confirm it is a Switch 1 Joy-Con.
- If an old controller is interfering, use the relevant **forget** button and
  pair it again.
- If flashing fails, confirm the selected port, use a data-capable USB cable,
  install the board's USB-UART driver, and verify the chip with
  `esptool.py -p <PORT> chip_id`.
