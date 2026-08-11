# LiberArk68

A 68-key wireless split mechanical keyboard with a column-staggered layout, rotated thumb cluster, and a dedicated dongle for ZMK Studio.

Designed ground-up around ZMK firmware. The PCB integrates winged gasket mounting points directly into the board outline, intended to pair with a dedicated low-profile 3D-printed gasket-mount case — no separate plate or mounting hardware required.

![LiberArk68 build](docs/liberark68.jpg)

## Hardware

- **MCU**: 3× [Seeed Studio XIAO nRF52840](https://wiki.seeedstudio.com/XIAO_BLE/) — one per half, one for the dongle
- **Matrix**: 3 rows × 12 columns per half (34 keys per side; row 2 has 10 keys)
  - Rows: driven directly from MCU GPIOs (D0–D2)
  - Columns: two daisy-chained [74HC595](https://www.ti.com/product/SN74HC595) shift registers per side (through-hole DIP-16 package), fed over SPI (D3 = CS, D8 = SCLK, D10 = MOSI), giving 16 virtual GPIOs of which 12 are used
- **Diodes**: switch → diode → row (col2row scanning)
- **Connectivity**: BLE; each half is a peripheral, the third XIAO is the BLE central
- **Dongle**: [Prospector](https://github.com/Aleblazer/prospector-zmk-module/tree/codex/ili9341-port) adapter with an ILI9341 320x240 LCD for live status (layer, modifiers, battery)
- **VIK connector** on each half for future peripherals (encoder, trackpad, etc.)

### FT6336 touchpad test wiring

The `codex/ili9341-touchpad-test` branch uses the ILI9341 module's FT6336
touch controller as a one-finger relative mouse. Connect its touch signals to
the dongle Xiao as follows:

| Touch signal | Xiao nRF52840 pin |
|---|---|
| `TP_SDA` | D4 / P0.04 |
| `TP_SCL` | D5 / P0.05 |
| `TP_INT` | D0 / P0.02 |
| `TP_RST` | D1 / P0.03 |
| `TP_VCC` | 3.3 V |
| `TP_GND` | GND |

This test converts absolute touch coordinates into cursor movement. For the
display's landscape orientation, horizontal movement is inverted and both axes
are scaled to 2.5x. Tap-to-click and multitouch gestures are intentionally not
enabled yet.

## Firmware

This repo is a [ZMK](https://zmk.dev) user config. The shield lives in [`boards/shields/liberark68/`](boards/shields/liberark68/) and includes:

| File | Purpose |
|---|---|
| `liberark68.dtsi` | Shared matrix transform + physical layout (used by Studio) |
| `liberark68_kscan.dtsi` | SPI + 595 + kscan, shared by both halves |
| `liberark68_left.overlay` | Left half (peripheral) |
| `liberark68_right.overlay` | Right half (peripheral) — applies `row-offset = <3>` to map into the right slice of the combined matrix |
| `liberark68_dongle.overlay` | Dongle (BLE central) — no local kscan |
| `liberark68.keymap` | Base layer + four placeholder layers (`L1`–`L4`) |
| `Kconfig.shield` / `Kconfig.defconfig` | Shield declarations and split-role defaults |
| `*.conf` | Per-target Kconfig overrides (Studio, status screen, sleep) |

ZMK Studio is enabled on the dongle, so layers 1–4 can be edited live without re-flashing.

## Building

CI builds run automatically on every push via [`.github/workflows/build.yml`](.github/workflows/build.yml). Targets defined in [`build.yaml`](build.yaml):

- `liberark68_left` — left half firmware
- `liberark68_right` — right half firmware
- `liberark68_dongle prospector_adapter` — dongle with Studio + Prospector display
- `settings_reset` — wipes the settings partition on any board (re-pairing, recovery)

Built artifacts are downloadable from the Actions run page as a `firmware.zip` containing four `.uf2` files.

## Flashing

1. Double-tap the reset button on the target board to enter UF2 bootloader mode
2. Drag the matching `.uf2` onto the `XIAO-SENSE` USB mass-storage drive that appears
3. The board reboots automatically

**Order matters for pairing**: flash the dongle first, then pair the left half, then the right half. Prospector arranges its battery widgets in pairing order.

If pairings ever get wedged, flash `settings_reset.uf2` onto the misbehaving board to wipe its settings partition, then re-flash the normal firmware.

## ZMK Studio

With the dongle plugged in via USB, open [zmk.studio](https://zmk.studio) (Chrome / Edge) and connect. The keyboard will render with its actual column-staggered geometry and rotated thumbs. Edits persist to the dongle's storage and survive power cycles.

## Credits

- [ZMK](https://zmk.dev) — firmware
- [Aleblazer/prospector-zmk-module](https://github.com/Aleblazer/prospector-zmk-module/tree/codex/ili9341-port) — ILI9341 dongle display + status screens (forked from carrefinho)
