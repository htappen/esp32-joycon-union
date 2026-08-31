# Joy-Con Bridge — firmware

ESP32 (original / WROOM-32) firmware that bonds one left + one right Switch 1
Joy-Con, merges them into a single virtual controller, and re-presents it to an
Android/Windows host as an Xbox-style BLE HID gamepad. Config via a web page
served in Config Mode.

See [`../docs/01-requirements.md`](../docs/01-requirements.md) and
[`../docs/02-implementation-plan.md`](../docs/02-implementation-plan.md) for the
full spec, and [`../docs/03-implementation-status.md`](../docs/03-implementation-status.md)
for what is built vs. what still needs hardware.

## Layout

```
firmware/
  CMakeLists.txt          top-level ESP-IDF project
  sdkconfig.defaults      pinned build config (BTstack dual-mode, no PSRAM, WDT)
  partitions.csv          4 MB single-app (no dual-slot OTA)
  main/                   app_main wiring, mode_manager (BOOT button), status LED
  components/
    merge/                pad_state.h, merge_engine, xbox_report   (pure C, tested)
    joycon/               joycon_decode (pure C, tested), joycon_host (Bluepad32)
    ble_out/              xbox_descriptor.h, ble_xbox_hid (BTstack HOGP), xbox_hid.gatt
    webcfg/               web_server (Wi-Fi + esp_http_server), www/ SPA
    store/                config_store (NVS)
  test/                   host build of the pure-C code + golden vectors
```

## Dependencies & how to install them

### 1. Host unit tests only

Needs just a C99 compiler and `make` — nothing ESP-specific.

```sh
# Debian / Ubuntu / WSL
sudo apt-get update && sudo apt-get install -y build-essential

# Fedora
sudo dnf install -y gcc make

# macOS (Command Line Tools)
xcode-select --install

# Arch
sudo pacman -S --needed base-devel
```

Verify: `make -C firmware/test test` → `99 checks, 0 failed`.

### 2. Firmware build (ESP-IDF v5.3.x)

**System packages** (ESP-IDF's own prerequisites):

```sh
# Debian / Ubuntu / WSL
sudo apt-get install -y git wget flex bison gperf python3 python3-pip python3-venv \
  cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0

# Fedora
sudo dnf install -y git wget flex bison gperf python3 python3-pip python3-virtualenv \
  cmake ninja-build ccache dfu-util libusbx

# macOS (Homebrew)
brew install cmake ninja dfu-util python3
```

**ESP-IDF toolchain** (pin to the version CI uses, `v5.3.1`):

```sh
mkdir -p ~/esp && cd ~/esp
git clone -b v5.3.1 --recursive https://github.com/espressif/esp-idf.git
cd ~/esp/esp-idf
./install.sh esp32          # Windows: install.bat esp32
. ./export.sh               # run once per shell — puts idf.py on PATH
```

Add `. ~/esp/esp-idf/export.sh` (or the `get_idf` alias from the IDF docs) to
your shell rc so `idf.py` is available in new terminals.

**Managed components** — `bluepad32`, `btstack`, `led_strip`, `mdns` (see
`main/idf_component.yml`). These are pulled automatically from the ESP-IDF
Component Registry on the first `idf.py build`; no manual install, but the first
build needs network access. To pre-fetch:

```sh
cd firmware && idf.py set-target esp32 && idf.py reconfigure
```

### 3. Flashing hardware

`esptool.py` and the serial monitor ship inside ESP-IDF (installed by
`install.sh` above). Extra per-OS setup:

```sh
# Linux: allow non-root access to the USB serial port, then re-login
sudo usermod -aG dialout $USER

# WSL: attach the USB device from Windows (run in an admin PowerShell)
#   winget install usbipd    &&    usbipd list    &&    usbipd attach --busid <b-p> --wsl
```

On Windows/macOS also install the USB-UART bridge driver for your board —
CP210x (Silicon Labs) or CH340 (WCH), depending on the module.

## Host unit tests (no hardware, no ESP-IDF)

```
make -C firmware/test test
```

Covers `merge_engine`, `xbox_report`, and `joycon_decode` with golden vectors:
neutral, all-buttons, A/B position toggle, stick calibration + deadzone,
missing-L / missing-R degraded handling + hold-then-neutralize + reconnect,
trigger config, assignable SL/SR, HID report packing, hat encoding, axis
inversion. CI runs this on every push (`.github/workflows/ci.yml`).

## Building the firmware (needs ESP-IDF + the board)

```
cd firmware
idf.py set-target esp32
idf.py menuconfig        # optional: Joy-Con Bridge menu (Wi-Fi STA creds, GPIOs)
idf.py build
idf.py -p <PORT> flash monitor
```

Before the first build, **lock the managed-component versions** in
`main/idf_component.yml` against Bluepad32's current `CHANGELOG.md` (plan §2),
and confirm the chip with `esptool.py -p <PORT> chip_id` — it must be a classic
ESP32 (`ESP32-D0WD*`), not an S3/C3/C6.

## Config Mode

Long-press BOOT/GPIO0 (~1.5 s) to toggle Play ↔ Config. In Config Mode:

- default: SoftAP `joycon-bridge` (WPA2, password `joyconbridge`), UI at
  `http://192.168.4.1/`
- if `JCB_WIFI_STA_SSID`/`JCB_WIFI_STA_PASS` are set at build time: joins that
  network, UI at `http://joycon-bridge.local/` (mDNS) and the serial-printed IP

Hold BOOT from power-on for ~3 s to factory-reset (clears NVS + BT bonds).
