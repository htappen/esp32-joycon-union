# Joy-Con Bridge — Implementation Plan

Derived from `01-requirements.md` (Draft v2, 2026-08-30 — scope: 2 Joy-Cons in / 1 controller out).

**Board:** 38-pin ESP32 DevKitC-style, presumed ESP32-WROOM-32, **4 MB flash, no PSRAM**,
micro-USB serial only. Verify classic-ESP32 silicon (`esptool.py chip_id`) before Phase 0.

**Scope impact on this plan:** exactly one L + one R Joy-Con → one merged Xbox-style
controller. Total simultaneous Bluetooth links = **3** (2 Classic HID host + 1 BLE
peripheral). No assignment table, no slot management, no active-controller switching.
This keeps the single-chip architecture within the ESP32's normal envelope; the
two-chip design is a fallback only.

---

## 1. Architecture

### 1.1 Chosen: single chip, single BTstack, dual role

```
                       ESP32-WROOM-32  (one BTstack instance)
  ┌─────────────────────────────────────────────────────────────────────┐
  │  Classic HID HOST  (Bluepad32 core)                                  │
  │    ├─ Joy-Con L ─┐  raw HID input reports (0x30)                     │
  │    └─ Joy-Con R ─┘                                                   │
  │            │                                                         │
  │            ▼   joycon_decode  (parse buttons + stick, apply calib)   │
  │            ▼   merge_engine   (L half + R half → unified_pad_state)  │
  │            ▼   xbox_report    (unified_pad_state → HID report bytes) │
  │            ▼                                                         │
  │  BLE HID PERIPHERAL (HOGP)  ── Xbox One S/Series descriptor ──▶ Host │
  │    (BTstack hids_device + GATT: HID / Battery / Device Info)         │
  └─────────────────────────────────────────────────────────────────────┘
        Config Mode only: Wi-Fi (SoftAP or STA) + esp_http_server + SPA
```

**Why:** Joy-Cons force the Classic HID host role; the only mature Classic-HID-host
code for ESP32 (Bluepad32) is BTstack-based; BTstack can run a Classic host and an
LE peripheral concurrently. One stack owns the radio — no Bluedroid/NimBLE conflict.
With only 3 links and no PSRAM, this is expected to fit; M4 confirms it.

### 1.2 Fallback: two chips (only if M4 fails)

If a single BTstack instance can't hold 2 Classic HID links + 1 LE peripheral within
the heap floor (NFR-6), split:

- **ESP32-A** — Bluepad32 + `joycon_decode` + `merge_engine`, streams a ~16-byte
  `unified_pad_state` struct over **UART** (TX/RX/GND, 3 wires; ESP-NOW is the
  cable-free alternative but adds 2.4 GHz traffic).
- **ESP32-B** — `ESP32-BLE-CompositeHID` (Mystfit, NimBLE) presenting the Xbox-style
  controller from the received struct; also hosts the Wi-Fi config UI.

Cost: a second board + 3 wires. Benefit: both halves are stock, proven libraries.
`merge_engine` + `xbox_report` + `pad_state.h` are written as a topology-independent
library so the fallback costs integration work, not a rewrite.

**Decision gate:** attempt §1.1 through M4. Fall back only if M4's soak fails after
reasonable tuning.

## 2. Toolchain & repo layout

- **Framework:** ESP-IDF, pinned to the version Bluepad32's current release targets
  (verify against Bluepad32 `CHANGELOG.md` at pin time; ~v5.3.x at time of writing).
  Not Arduino — need BTstack access for the LE peripheral.
- **Language:** C (C++ allowed for the app layer / merge engine).
- **Key components:**
  - `bluepad32` (ESP-IDF component) + its bundled BTstack — Classic HID host.
  - BTstack `hids_device`, `battery_service_server`, `device_information_service_server` — LE HID peripheral.
  - `esp_http_server`, `esp_wifi`, `mdns` — Config Mode.
  - `nvs_flash` — persistence.

```
/firmware
  /main
    app_main.c              # init, task wiring
    mode_manager.c/.h       # Play/Config state machine, BOOT button, WS2812 status LED
  /components
    /joycon
      joycon_decode.c/.h    # input report 0x30 parse, stick calibration, subcommands
      joycon_host.c/.h      # Bluepad32 glue: bond exactly 1 L + 1 R, player LED, battery
    /merge
      pad_state.h           # joycon_state, unified_pad_state (topology-independent)
      merge_engine.c/.h     # L half + R half -> unified_pad_state, degraded-half handling
      xbox_report.c/.h      # unified_pad_state -> Xbox HID report bytes + button map
    /ble_out
      ble_xbox_hid.c/.h     # BTstack LE peripheral: advertising, GATT, report notify
      xbox_descriptor.h     # HID report descriptor + VID/PID/strings
    /webcfg
      web_server.c/.h       # http + ws routes
      /www                  # index.html, app.js, style.css (embedded via target_add_binary_data)
    /store
      config_store.c/.h     # NVS schema, load/save, factory reset
  /test                     # host-built unit tests for merge_engine + xbox_report
  sdkconfig.defaults
  partitions.csv            # 4 MB single-app: nvs, phy_init, factory (~2 MB), storage (~256 KB)
```

**4 MB / no-PSRAM partitioning:** single `factory` app — no dual-slot OTA. Web assets
embedded in the binary; a small `storage` partition only if assets outgrow ~150 KB.
In-field OTA is post-v1 (needs a compressed single-slot scheme).

## 3. Configuration highlights (`sdkconfig.defaults`)

- `CONFIG_BT_ENABLED=y`; BTstack host via Bluepad32; controller in **dual mode** (BR/EDR + BLE).
- `CONFIG_BTDM_CTRL_BR_EDR_MAX_ACL_CONN` = **2** (the two Joy-Cons). `+1` headroom only if heap allows.
- BLE max connections = **1** (the host link).
- `CONFIG_BLUEPAD32_MAX_DEVICES` = **2**.
- `CONFIG_SW_COEXIST_ENABLE` — off in Play Mode (Wi-Fi radio down); on for Config Mode. Hardware coex if SoftAP + BT must overlap.
- Bump BTstack task stack/priority; pin BT to core 0, app/merge to core 1.
- Task Watchdog on for merge + output tasks.
- **No PSRAM:** minimal BTstack buffer pools; disable unused features (BLE 5 ext-adv,
  mesh, A2DP/HFP/SPP, verbose logging). Log `esp_get_free_heap_size()` every few seconds.
- Build-time Kconfig `JCB_WIFI_STA_SSID` / `JCB_WIFI_STA_PASS`: if set, Config Mode
  uses STA + mDNS (`joycon-bridge.local`); if empty, SoftAP.

## 4. Phased delivery

### Phase 0 — Skeleton & host bring-up  *(M0)*
- **Verify silicon:** `esptool.py --port <PORT> chip_id` → must be classic ESP32
  (`ESP32-D0WD` / `D0WDQ6`). Stop and re-hardware if S3/C3/C6. Record `flash_id`.
- Repo, ESP-IDF pin, `sdkconfig.defaults`, 4 MB single-app `partitions.csv`, CI build.
- Vendor `bluepad32` as a component; build its stock example unchanged on the board.
- **Exit:** silicon confirmed; stock Bluepad32 example runs; one Joy-Con connects and
  logs buttons; baseline free-heap recorded.

### Phase 1 — Joy-Con input layer  *(M1)*
- `joycon_host`: on Bluepad32 connect, identify L vs R (Bluepad32 model / product ID);
  **accept only one of each side**, reject/replace extras (FR-2); keep the two bonds
  in a fixed 2-slot table.
- `joycon_decode`: confirm/extend Bluepad32's parsing for input report `0x30`
  (buttons + stick; IMU bytes ignored). If Bluepad32 doesn't already request `0x30`,
  send subcommand `0x03 0x30`.
- Read **factory stick calibration** (SPI reads: `0x603D` left / `0x6046` right,
  `0x6080` params); apply center/min/max scaling + configurable radial deadzone.
- Re-send report-mode subcommand on every (re)connect.
- **Exit:** one L + one R Joy-Con connected; normalized button + stick state for each
  at ≥ 60 Hz; calibration visibly correct; extras rejected cleanly.

### Phase 2 — Merge engine + Xbox report  *(M2)*  — host-buildable, no hardware needed
- `pad_state.h`: `joycon_state` (per physical), `unified_pad_state` (the one output).
- `merge_engine`: left Joy-Con → left half, right Joy-Con → right half (FR-7);
  degraded-half handling (FR-9): hold last state ~200 ms, then neutralize that half,
  set a `degraded` flag; recover on reconnect (FR-10).
- `xbox_report`: `unified_pad_state` → Xbox HID report bytes; default button map
  (requirements §4.4) incl. A/B position toggle, digital `ZL/ZR` → full-scale triggers.
- Compile `merge` + `xbox_report` for the dev PC; `/test` golden vectors: neutral,
  all-buttons, stick extremes, missing L, missing R, A/B toggle.
- **Exit:** unit tests pass; recorded real Joy-Con samples (from M1 logs) produce
  correct report bytes.

### Phase 3 — BLE Xbox HID peripheral (standalone)  *(M3)*
- `xbox_descriptor.h`: Xbox One S/Series-compatible HID report descriptor, VID
  `0x045E`, matching PID (`0x0B13` Series or `0x02FD` One S), device name + DIS
  strings. Cross-check against `ESP32-BLE-CompositeHID` and `BlueRetro`.
- `ble_xbox_hid` on **BTstack** (`hids_device` + Battery + Device Information
  services); connectable advertising, appearance `0x03C4` (Gamepad); single fixed
  identity; bond storage.
- Drive from a **synthetic** `unified_pad_state` (sweeping sticks, walking buttons).
- Test on **Windows 11** (expect XInput binding) and **Android** (standard gamepad):
  every control in §4.4 via a gamepad tester + `joy.cpl`.
- **Exit:** OS-recognized Xbox-style pad, all controls exercised from synthetic data
  on both OSes; pairing + reconnect to a remembered host works.

### Phase 4 — Concurrency integration  *(M4 — the risk gate)*
- Single firmware: Bluepad32 Classic HID host **and** `ble_xbox_hid` LE peripheral
  under one BTstack. Wire real `merge_engine` output → BLE report at ≥ 100 Hz.
- Stress: both Joy-Cons + host connected. Measure added latency (NFR-1), dropped-report
  rate, and **free heap** (NFR-6 floor: 30 KB) over a **1-hour soak**.
- Inject Joy-Con and host disconnect/reconnect during the soak.
- **Exit (M4):** stable 2-in / 1-out for ≥ 1 hour, latency in budget, heap above floor.
  **If not, after tuning → invoke §1.2 two-chip fallback** (Phases 2 & 5 carry over intact).

### Phase 5 — Web configuration portal  *(M5)*
- `mode_manager`: Play ↔ Config state machine; BOOT/GPIO0 long-press; WS2812 patterns
  (Play-searching / Play-connected / degraded / Config / error).
- Config Mode Wi-Fi: **SoftAP** (`joycon-bridge`, WPA2) by default; **STA + mDNS**
  if `JCB_WIFI_STA_*` set, SoftAP fallback on join failure. Serial prints the URL/IP.
- `esp_http_server` + embedded SPA (vanilla JS, no CDN; total < ~150 KB).
- REST: `GET /api/state`, `POST /api/pairing/{start,stop}`,
  `POST /api/joycon/{L|R}/forget`, `GET/PUT /api/mapping`, `POST /api/host/forget`,
  `POST /api/reboot`, `POST /api/factory-reset`, `GET /api/version`.
- WebSocket `/ws`: status + live merged report ≥ 5 Hz for the input-test view (FR-21).
- Entering Config Mode must not lose BT bonds; if coexistence is disruptive it may
  suspend the host link for the session (FR-22 fallback, allowed).
- **Exit:** every capability in requirements §4.5 works from a phone browser.

### Phase 6 — Persistence & lifecycle  *(M6)*
- `config_store`: versioned NVS blob for FR-24 (two Joy-Con addresses, button map +
  tuning, bonded host, A/B pref) with migration.
- Boot restore + auto-reconnect logic and timeouts (NFR-4).
- Factory reset clears NVS + BT bonds, via UI and boot chord (FR-25).
- **Exit:** acceptance A-3, A-6 pass.

### Phase 7 — Hardening & polish  *(M7)*
- Watchdog coverage for all tasks; recover on BTstack error events.
- Player-LED "player 1" confirmation + host battery report (both stretch, land if cheap).
- Edge cases: host bond full, Joy-Con battery critical, RF congestion, Joy-Con 2
  plugged in (detect unknown PID → clear UI message).
- Optional: single-Joy-Con sideways mode.
- User README: SoftAP password, pairing steps, LED legend, USB reflash recovery.
- **Exit:** all requirements §9 acceptance criteria pass.

## 5. Milestone summary

| ID | Milestone | Gate |
|---|---|---|
| M0 | Silicon confirmed; Bluepad32 stock example, 1 Joy-Con | toolchain OK |
| M1 | 1 L + 1 R Joy-Con, normalized state, calibration | input layer done |
| M2 | Merge engine + Xbox report, host unit-tested | core logic done |
| M3 | BLE Xbox pad from synthetic data, Win + Android | output proven |
| **M4** | **2-in / 1-out concurrent, 1-hr soak, heap above floor** | **single-chip confirmed / fallback decided** |
| M5 | Web portal full feature set | config done |
| M6 | Persistence + auto-reconnect | lifecycle done |
| M7 | Hardening, docs, acceptance | release |

## 6. Risks & mitigations

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Board is S3/C3/C6 (no BR/EDR) | Low | Fatal | `esptool chip_id` in Phase 0 before any real work; swap board. |
| Single BTstack can't hold 2 Classic HID + 1 LE peripheral in no-PSRAM RAM | Low-Med | High | Only 3 links now (was 5). M4 gate with 30 KB heap floor; §1.2 two-chip fallback. |
| Bluepad32's bundled BTstack config doesn't expose LE-peripheral hooks | Medium | Med | Patch/extend the component locally; upstream a Kconfig flag; worst case fork. |
| Windows won't XInput-bind the emulated descriptor | Low-Med | Med | Copy a known-working Xbox BT descriptor (BlueRetro / CompositeHID); fall back to generic BLE HID gamepad (still works on Win + Android, no XInput). |
| Latency budget (NFR-1) missed | Low | Med | Pin tasks to cores, 7.5 ms BLE conn interval, trim Joy-Con report parsing. |
| 4 MB flash too small for firmware + web assets | Low | Med | No dual-OTA; cap + gzip web bundle; drop unused IDF components. |
| Wi-Fi (Config Mode) disrupts BT | Low (Play/Config split) | Low | Hard split; allow suspending the host link during Config Mode. |
| Joy-Con clones with quirky calibration/report timing | Low | Low | Defensive parsing, calibration fallbacks, per-device quirks table. |

## 7. Testing strategy

- **Host-side unit tests** — `merge_engine` + `xbox_report`, golden vectors, built for the dev PC (runs in CI).
- **HW bring-up logs** per phase — connection stability, report rate, free heap.
- **Latency rig** — GPIO toggle on Joy-Con report receipt vs. logic-analyzer capture
  of the BLE notification (or timestamp round-trip via a host test app).
- **OS matrix** — Windows 11 (latest) + Android (latest and one older major),
  gamepad-tester web app and `joy.cpl`.
- **Soak** — 1-hour at M4; 8-hour pre-release with periodic Joy-Con + host
  disconnect/reconnect injection.

## 8. Reference material

**Joy-Con protocol**
- dekuNukem — Nintendo Switch Reverse Engineering: https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering
  - `bluetooth_hid_notes.md`, `bluetooth_hid_subcommands_notes.md` — report modes, LED subcommand `0x30`, SPI calibration reads
- Joy-Con HID input reports on ESP32 (issue thread): https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/issues/81
- CTCaer/joycond (Linux combine-Joy-Cons daemon, mapping reference): https://github.com/CTCaer/joycond

**ESP32 Bluetooth host (upstream / Joy-Con read)**
- Bluepad32: https://github.com/ricardoquesada/bluepad32 · docs https://bluepad32.readthedocs.io/
  - Supported gamepads (Joy-Con L/R supported, not combinable — we do the combine): https://bluepad32.readthedocs.io/en/latest/supported_gamepads/
  - FAQ (only original ESP32 has BR/EDR): https://bluepad32.readthedocs.io/en/latest/FAQ/
  - CHANGELOG (ESP-IDF version pin, max-devices default): https://github.com/ricardoquesada/bluepad32/blob/main/CHANGELOG.md
- Arduino Joy-Con + Bluepad32 example: https://github.com/gjlp25/joycon_esp32
- BlueRetro (multi-controller BT adapter, ESP32, BTstack, descriptors): https://hackaday.io/project/170365-blueretro

**ESP32 as Xbox-style BLE controller (downstream / output)**
- ESP32-BLE-CompositeHID (Xbox One S / Series X emulation, VID/PID configurable): https://github.com/Mystfit/ESP32-BLE-CompositeHID
- ESP32-BLE-HID-exp / Xbox One notes: https://github.com/esp32beans/ESP32-BLE-HID-exp
- ESP32-BLE-Gamepad (generic BLE HID gamepad, NimBLE): https://github.com/lemmingDev/ESP32-BLE-Gamepad

**ESP-IDF / BTstack**
- BTstack (bundled with Bluepad32): https://github.com/bluekitchen/btstack — `hids_device`, `hog_*` examples
- ESP-IDF Bluetooth HID Host API: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/bluetooth/esp_hidh.html
- ESP-IDF Bluetooth HID Device API: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/bluetooth/esp_hidd.html
- ESP-IDF RF coexistence (Wi-Fi + BT, AP-mode caveats): https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/coexist.html
- `esp_http_server`: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/protocols/esp_http_server.html

## 9. First concrete steps

1. Confirm requirements open questions Q1–Q5.
2. When the board arrives: `esptool.py chip_id` / `flash_id` — confirm classic ESP32, 4 MB.
3. Pin ESP-IDF to Bluepad32's target version; commit `sdkconfig.defaults` + `partitions.csv`.
4. Vendor `bluepad32`; reach M0 (stock example, one Joy-Con) on the board.
5. In parallel (no hardware needed): write `merge_engine` + `xbox_report` + `/test` (M2).
6. Prototype `ble_xbox_hid` on a branch early to de-risk M4 (does a BTstack LE
   peripheral coexist with Bluepad32 at all?).
