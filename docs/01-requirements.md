# Joy-Con Bridge — Requirements

**Project:** ESP32 firmware that pairs one left + one right Nintendo Switch Joy-Con,
merges them into a single virtual controller, and re-presents that controller to an
Android or Windows host as an Xbox-style gamepad. Configuration is done through a
web page served by the ESP32.

**Status:** Draft v2 — 2026-08-30 (scope narrowed to 2-in / 1-out)
**Owner:** Katelyn Smith

---

## 1. Goal

Let a phone or PC use a loose pair of Joy-Cons as if they were a normal Xbox-style
gamepad, with no host-side software. The device:

1. Pairs **exactly two Joy-Cons** — one left, one right (Switch 1 generation).
2. Merges them into **one** virtual controller.
3. Presents that controller to the host as an **Xbox-style Bluetooth controller**.
4. Serves a **local web UI** for pairing and button mapping.

A future version may support 4 Joy-Cons → 2 controllers; that is explicitly **not**
this project (see §6, §7).

## 2. Definitions

| Term | Meaning |
|---|---|
| **Host** | The downstream Android/Windows device that consumes the emulated controller. |
| **Joy-Con** | Nintendo Switch (2017 generation) left or right Joy-Con. Bluetooth Classic (BR/EDR) HID. |
| **Virtual controller** | The single merged Xbox-style gamepad built from the L + R Joy-Con. |
| **Config Mode** | ESP32 runs a Wi-Fi interface + web server; BT traffic is deprioritized. |
| **Play Mode** | Wi-Fi off; all radio budget goes to Bluetooth. Default state. |

## 3. Hardware

**Target board:** 38-pin ESP32 DevKitC-style module, **4 MB flash, no PSRAM**,
micro-USB serial only (no native USB-OTG). Presumed **ESP32-WROOM-32 / -32E**
(original ESP32). **Must verify** before Phase 0 that the chip is a classic ESP32
(`esptool.py chip_id` → `ESP32-D0WD…`), not an S3/C3/C6 — those lack Bluetooth
Classic and cannot connect to Switch 1 Joy-Cons.

| Item | Requirement |
|---|---|
| MCU | Original ESP32, dual-core, **BR/EDR + BLE**. Confirmed board is a 38-pin DevKitC clone. |
| Flash | **4 MB** confirmed. No PSRAM — heap budget is tight (see NFR-6). With only 3 total BT links (see §7) this is expected to fit on one chip. |
| USB | Micro-USB → USB-serial bridge (CP2102 / CH340) for flashing + logs only. **No** USB device role to the host. |
| Inputs | 1 user button — reuse **BOOT / GPIO0**: long-press toggles Config/Play Mode; the pairing-reset chord is held at boot. |
| Status output | 1 addressable **WS2812 RGB LED** on a free GPIO for mode / connection state (assumed default; a single plain LED is an acceptable downgrade). |
| Power | USB 5 V. No battery requirement in v1. |

## 4. Functional requirements

### 4.1 Joy-Con pairing (upstream)

- **FR-1** Discover and pair Joy-Cons put into sync mode (sync button on the rail).
- **FR-2** Bond **exactly one left and one right** Joy-Con. A second Joy-Con of a
  side already filled replaces the previous bond for that side (with UI confirmation).
- **FR-3** Remember the two bonded Joy-Cons by Bluetooth address in NVS and
  auto-reconnect on boot / on wake.
- **FR-4** Show each Joy-Con's state in the web UI: address, side (L/R), battery if
  available, link status.
- **FR-5** Allow "forget" of either Joy-Con from the web UI or via the boot chord.
- **FR-6** Set the Joy-Con **player LEDs** to a fixed "player 1" pattern once bonded,
  as a visible pairing confirmation (subcommand `0x30`). *(Stretch — not release-blocking.)*

### 4.2 Merge engine

- **FR-7** Roles are fixed: the left Joy-Con drives the left half of the virtual
  controller, the right Joy-Con the right half. No assignment table.
- **FR-8** Combine the L and R Joy-Con input state into **one** Xbox-style report
  (mapping in §4.4).
- **FR-9** If one Joy-Con drops, hold its last state for ~200 ms, then report neutral
  for that half and mark the controller "degraded" in the UI / LED. The virtual
  controller stays connected to the host throughout.
- **FR-10** When the dropped Joy-Con reconnects, resume immediately — no host re-pair.

### 4.3 Host presentation (downstream)

- **FR-11** Emulate an **Xbox-style wireless controller over Bluetooth LE HID**
  (HOGP), using Microsoft VID `0x045E` and an Xbox One S / Series-compatible PID and
  HID report descriptor, so Windows binds it via XInput/GIP and Android treats it as
  a standard gamepad.
- **FR-12** Present a **single, fixed** HID device identity. It advertises whenever
  no host is connected and a bonded host is remembered or pairing is allowed.
- **FR-13** Forward at minimum: all face buttons, D-pad, both analog sticks with
  clicks, both shoulder buttons, both triggers (digital Joy-Con `ZL/ZR` mapped to
  full-scale trigger values), and the system buttons (Minus→View, Plus→Menu,
  Home→Guide, Capture→configurable).
- **FR-14** Report a battery level to the host — the lower of the two Joy-Con
  batteries. *(Stretch in v1.)*
- **FR-15** Remember one bonded host; "forget host" available in the web UI.

### 4.4 Default button map (Joy-Con pair → Xbox)

| Xbox control | Source |
|---|---|
| Left stick + L3 | Left Joy-Con stick + stick click |
| Right stick + R3 | Right Joy-Con stick + stick click |
| D-pad | Left Joy-Con ▲▼◀▶ buttons |
| A / B / X / Y | Right Joy-Con — **Nintendo-position by default**, i.e. Xbox A = Nintendo B (bottom). Toggle in web UI for "Xbox-position (match label)". |
| LB / RB | L / R |
| LT / RT | ZL / ZR (digital → 0 or full) |
| View (Back) | Minus |
| Menu (Start) | Plus |
| Guide | Home |
| *(configurable)* | Capture — default unmapped |
| SL / SR (rail buttons) | Default unmapped; assignable in web UI |

- **FR-16** All mappings above are overridable in the web UI and persisted to NVS.
- **FR-17** Apply **stick calibration** read from each Joy-Con's factory calibration
  (SPI flash `0x603D` left / `0x6046` right + `0x6080` params), with user calibration
  override as *stretch*. Apply a configurable radial deadzone.

### 4.5 Web configuration UI

- **FR-18** In Config Mode the ESP32 serves a single-page app over HTTP. Two ways to
  reach it:
  - **Default — SoftAP:** ESP32 broadcasts its own Wi-Fi network (`SSID: joycon-bridge`,
    WPA2, documented default password), UI at `http://192.168.4.1/`.
  - **Optional — Station (compile-time):** if Wi-Fi SSID + password are provided at
    **build time** (Kconfig / `secrets.h`), Config Mode instead joins that existing
    network and the UI is reachable at `http://joycon-bridge.local/` (mDNS) and the
    DHCP-assigned IP (printed to serial). SoftAP is the fallback if the join fails.
  - Runtime credential entry (captive portal) is **not** in v1.
- **FR-19** No internet dependency: all HTML/CSS/JS assets are embedded in firmware.
  No external CDNs.
- **FR-20** UI capabilities:
  - Live status of both Joy-Cons and the host connection.
  - Enter/exit pairing (discovery) mode; forget a Joy-Con; forget the host.
  - Edit the button map, A/B position, deadzone, trigger behavior.
  - Firmware version; reboot; factory reset.
- **FR-21** UI gets live updates (WebSocket or SSE) at ≥ 5 Hz for status, with an
  input-test view showing the live merged report.
- **FR-22** Entering Config Mode must not permanently disturb an active host session;
  on returning to Play Mode the host reconnects automatically. *(If coexistence
  proves disruptive, Config Mode may suspend the host link for its duration — an
  acceptable degradation.)*

### 4.6 Modes & persistence

- **FR-23** Boot into **Play Mode**. Long-press the user button to enter **Config
  Mode**; another long-press or a UI action returns to Play Mode. Mode is shown on
  the status LED.
- **FR-24** Persist to NVS: the two bonded Joy-Con addresses, button map and tuning,
  bonded host, A/B position preference.
- **FR-25** Factory reset (button chord at boot + UI action) clears all NVS config
  and BT bonds.

## 5. Non-functional requirements

| ID | Requirement |
|---|---|
| NFR-1 | **Latency:** added input latency (Joy-Con report received → host report sent) < 10 ms typical, < 20 ms p99, in Play Mode. |
| NFR-2 | **Report rate:** emit the host report at ≥ 100 Hz; never slower than the Joy-Con's ~60 Hz native rate. |
| NFR-3 | **Reliability:** survive Joy-Con disconnect/reconnect and host disconnect/reconnect without a reboot. Task watchdog + auto-recover on stack fault. |
| NFR-4 | **Startup:** both Joy-Cons + host reconnect within 10 s of power-on when present. |
| NFR-5 | **Coexistence:** Wi-Fi runs **only** in Config Mode. Play Mode keeps the Wi-Fi radio off to protect BT timing. |
| NFR-6 | **RAM budget:** fit in the ~320 KB usable DRAM of a **no-PSRAM** ESP32 with BTstack (Classic HID host ×2 + LE peripheral ×1) + `esp_http_server` (Config Mode only). Log free-heap at each milestone; < 30 KB free heap under full load triggers the two-chip fallback. |
| NFR-7 | **Flash budget:** firmware + embedded web assets + NVS fit in **4 MB**. No dual-slot OTA in v1; recovery is USB reflash. |
| NFR-8 | **No host-side software** required for core function. |
| NFR-9 | **Security:** SoftAP is WPA2 with a non-blank password; web UI has no auth beyond network access in v1 (documented limitation). BT uses standard SSP. |
| NFR-10 | **Buildability:** single `idf.py build`; pinned ESP-IDF and component versions; reproducible. |

## 6. Out of scope (v1)

- **More than two Joy-Cons / more than one output controller.** (The original
  4-Joy-Con → 2-controller idea — deferred to a possible v2 that needs a two-chip
  design or a larger MCU regardless.)
- Motion / IMU (gyro + accelerometer) forwarding.
- Rumble / HD-rumble forwarding from host to Joy-Cons.
- NFC / IR camera / amiibo.
- Joy-Con 2 (Switch 2, 2025) support — different, largely-BLE, undocumented protocol.
- Nintendo Switch as a host target (we emulate Xbox, not a Pro Controller).
- Cloud connectivity, accounts, mobile app.
- Single-Joy-Con (sideways) mode — stretch only.
- In-field OTA updates.

## 7. Key constraints & rationale

1. **Joy-Cons are Bluetooth Classic.** Confirmed — Switch 1 Joy-Cons speak BR/EDR
   HID only, so the original ESP32 is mandatory and the Classic HID **host** role
   is unavoidable.
2. **One radio, one identity.** The ESP32 has a single 2.4 GHz radio and, as an HID
   peripheral, one practical device identity. v1 therefore does 2 Joy-Cons in → 1
   controller out. This keeps total simultaneous BT links at **3** (2 Classic HID
   host + 1 BLE peripheral), well within the ESP32's normal envelope, and removes
   all controller-switching logic.
3. **Stack unification.** The mature host-side library (Bluepad32) is built on
   **BTstack**. The output side must therefore also be BTstack (LE HID peripheral)
   so a single host stack owns the radio. Running Bluepad32 alongside a
   NimBLE/Bluedroid gamepad library in one image is not viable. Wiring a BTstack LE
   HID peripheral next to Bluepad32 is the main integration task (see plan §1, M4).
4. **Wi-Fi + BT coexistence in AP mode is poor.** Hence the hard Config/Play split.

## 8. Open questions

| # | Question | Default assumption if unanswered |
|---|---|---|
| Q1 | Is a single-Joy-Con (sideways) mode wanted even as stretch? | Yes, stretch only. |
| Q2 | Player-LED confirmation — v1 or later? | Attempt in v1, not release-blocking. |
| Q3 | Target host OS versions for test (Android 13/14/15? Windows 11 build)? | Latest Android + Windows 11. |
| Q4 | Confirm chip variant once board is in hand (`esptool.py chip_id`). | Classic ESP32-WROOM-32 (project is a non-starter otherwise). |
| Q5 | Is it acceptable for Config Mode to suspend the host link while open (FR-22 fallback)? | Yes. |

**Resolved:** Scope = 2 Joy-Cons in (1 L + 1 R) → 1 merged Xbox-style controller out
(BLE HID). Joy-Cons = Switch 1 only. v1 features = buttons + sticks + calibration.
Board = 38-pin ESP32-WROOM-32 devkit, 4 MB, no PSRAM, no USB device role. Config Mode
= SoftAP by default, optional compile-time Wi-Fi STA credentials. Mode button = reuse
BOOT/GPIO0. Status LED = one WS2812. Architecture = single chip (two-chip is fallback only).

## 9. Acceptance criteria (v1)

- A-1: Put one L and one R Joy-Con in sync mode; both bond; web UI shows both with
  correct side and live status.
- A-2: The merged controller connects to a Windows 11 PC and to an Android phone as
  an Xbox-compatible controller; every control in §4.4 verified in a gamepad tester.
- A-3: Power-cycle; both Joy-Cons and the last host reconnect automatically within 10 s.
- A-4: Disconnect and reconnect one Joy-Con; the virtual controller stays connected
  to the host and resumes full function without a reboot or re-pair.
- A-5: Measured added latency meets NFR-1 on a representative test.
- A-6: All config in §4.5 survives reboot; factory reset clears it.
- A-7: Free heap under full load (2 Joy-Cons + host connected) stays above the
  NFR-6 floor.
