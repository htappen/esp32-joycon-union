# Implementation Status

Tracks `02-implementation-plan.md` execution. **All phases have been built out
in code except the steps that require the physical board** (flashing, on-target
bring-up, soak, and OS-matrix testing), which are deferred until the ESP32 is
in hand.

Environment note: this pass had no ESP-IDF toolchain, so the ESP-IDF-side code
is written and self-consistent but **not compiled**. The pure-C core (merge
engine, Xbox report packer, Joy-Con decoder) **is** compiled and unit-tested
(`make -C firmware/test test` → 99 checks, 0 failed, `-Wall -Wextra -Werror
-pedantic`).

| Phase | Status | Notes |
|---|---|---|
| **0 — Skeleton & host bring-up** | Code complete; on-target pending | Repo tree, `sdkconfig.defaults`, `partitions.csv` (4 MB single-app), `Kconfig.projbuild`, `idf_component.yml` (bluepad32 + btstack + led_strip + mdns), CI. **Pending hardware:** `esptool chip_id`, build the stock Bluepad32 example, baseline free-heap. **Pending you:** lock managed-component versions. |
| **1 — Joy-Con input layer** | Decoder done + tested; host glue written | `joycon_decode.c` parses report 0x30 (buttons + both sticks), decodes factory stick calibration (0x603D / 0x6046 / 0x6080), applies center/range scaling + deadzone — unit-tested. `joycon_host.c` is the Bluepad32 custom-platform glue: one-L/one-R slot table, replace-on-duplicate (FR-2), player LED (FR-6), battery, forget. **Bluepad32 API symbol names must be reconciled with the pinned version.** |
| **2 — Merge engine + Xbox report** | **Done** | `merge_engine.c` (fixed L→left / R→right, radial deadzone, A/B position toggle, configurable triggers, degraded-half state machine: hold ~200 ms → neutralize → recover on reconnect). `xbox_report.c` (16-byte Xbox One S layout, hat encoding, Y-axis inversion, share-bit split). Golden vectors for every §4.4 control + all degraded transitions. |
| **3 — BLE Xbox HID peripheral** | Code written; on-target + OS test pending | `xbox_descriptor.h` (MS VID 0x045E, PID 0x0B13, full HID report descriptor matching the packer), `xbox_hid.gatt` (GAP/GATT/DIS/Battery/HID), `ble_xbox_hid.c` (BTstack `hids_device` + advertising + can-send-now flow + bond wipe + suspend/resume). **Pending hardware:** Windows 11 XInput binding + Android gamepad test; descriptor fallback to generic BLE HID gamepad if Windows won't bind. |
| **4 — Concurrency integration (risk gate M4)** | Wired; not measurable without hardware | `app_main.c` runs Bluepad32 host + BTstack LE peripheral under one stack, merge/output loop pinned to core 1 at `JCB_OUTPUT_RATE_HZ` (default 250). **Pending hardware:** the 1-hour soak, latency rig (NFR-1), free-heap floor (NFR-6, 30 KB) — this is the go/no-go for single-chip vs. the two-chip fallback. |
| **5 — Web configuration portal** | Code complete; on-target pending | `web_server.c` (STA-or-SoftAP with SoftAP fallback, mDNS, `esp_http_server`, REST for state/pairing/forget/mapping/reboot/factory-reset/version, `/ws` live status ≥ 5 Hz). Embedded vanilla-JS SPA (`www/`, no CDN) with live input-test view (FR-21). |
| **6 — Persistence & lifecycle** | Code complete; on-target pending | `config_store.c` (versioned NVS blob: 2 Joy-Con addrs + button map + tuning + host + A/B pref, with a migration hook). Boot restore + auto-reconnect addresses passed to `joycon_host`. Factory reset via UI + boot chord. |
| **7 — Hardening & polish** | Partial | Task WDT config in `sdkconfig.defaults`; status-LED legend (8 patterns); battery-to-host (FR-14); player-LED (FR-6). **Pending:** BTstack-error recovery paths, unknown-PID (Joy-Con 2) UI message, single-Joy-Con sideways mode (stretch), user README polish. |

## Open questions (from requirements §8)

Proceeding on the documented default assumptions:

- **Q1** single-Joy-Con sideways mode — deferred (stretch), not built.
- **Q2** player-LED confirmation — attempted in `joycon_host_set_player_led`.
- **Q3** test targets — latest Android + Windows 11 (Phase 3/M3).
- **Q4** chip variant — **must** verify with `esptool chip_id` before any flash.
- **Q5** Config Mode may suspend the host link — assumed yes; `enter_config()`
  calls `ble_xbox_hid_suspend()`.

## Next actions when the board arrives

1. `esptool.py -p <PORT> chip_id` + `flash_id` — confirm classic ESP32 / 4 MB.
2. Lock `firmware/main/idf_component.yml` versions against Bluepad32 CHANGELOG.
3. `idf.py set-target esp32 && idf.py build` — fix the first round of managed-
   component API drift (mostly `joycon_host.c` and `ble_xbox_hid.c`).
4. M0 → M1 → … per the plan; record free heap at each milestone.
