# joycon

ESP32 firmware bridging a pair of Nintendo Switch (gen 1) Joy-Cons to an
Android/Windows host as a single Xbox-style Bluetooth controller.

- **What / why / constraints:** [`docs/01-requirements.md`](docs/01-requirements.md)
- **Architecture & phased plan:** [`docs/02-implementation-plan.md`](docs/02-implementation-plan.md)
- **Build status:** [`docs/03-implementation-status.md`](docs/03-implementation-status.md)
- **Firmware:** [`firmware/`](firmware/) — see [`firmware/README.md`](firmware/README.md)

## Quick start

Dependencies and install commands (host compiler, ESP-IDF, flashing tools):
[`firmware/README.md` → "Dependencies & how to install them"](firmware/README.md#dependencies--how-to-install-them).

```sh
# Host-side logic tests (no hardware, no ESP-IDF):
make -C firmware/test test

# Firmware (needs ESP-IDF v5.3.x + the board):
cd firmware && idf.py set-target esp32 && idf.py build
```
