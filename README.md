# LiberArk68

A 68-key wireless split mechanical keyboard with a column-staggered layout, rotated thumb cluster, and a dedicated dongle for ZMK Studio.

Designed ground-up around ZMK firmware. The PCB integrates winged gasket mounting points directly into the board outline, intended to pair with a dedicated low-profile 3D-printed gasket-mount case — no separate plate or mounting hardware required.

![LiberArk68 build](docs/liberark68.jpg)

## Hardware

- **MCU**: 2× [Seeed Studio XIAO nRF52840](https://wiki.seeedstudio.com/XIAO_BLE/) for the keyboard halves; a Pro Micro footprint nRF52840 controller for the dongle
- **Matrix**: 3 rows × 12 columns per half (34 keys per side; row 2 has 10 keys)
  - Rows: driven directly from MCU GPIOs (D0–D2)
  - Columns: two daisy-chained [74HC595](https://www.ti.com/product/SN74HC595) shift registers per side (through-hole DIP-16 package), fed over SPI (D3 = CS, D8 = SCLK, D10 = MOSI), giving 16 virtual GPIOs of which 12 are used
- **Diodes**: switch → diode → row (col2row scanning)
- **Connectivity**: BLE; each half is a peripheral and the Pro Micro footprint controller is the BLE central
- **Dongle**: [wide-screen Prospector](https://github.com/Aleblazer/prospector-zmk-module/tree/codex/st7789-284x76-port) with a 2.25-inch 284×76 ST7789 display
- **VIK connector** on each half for future peripherals (encoder, trackpad, etc.)

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
- `settings_reset_xiao` / `settings_reset_dongle` — controller-specific settings wipe images

Built artifacts are downloadable from the Actions run page as a `firmware.zip` containing five `.uf2` files.

## Flashing

1. Double-tap the reset button on the target board to enter UF2 bootloader mode
2. Drag the matching `.uf2` onto the bootloader drive that appears (`XIAO-SENSE`
   for the halves; the dongle drive name depends on the Pro Micro clone)
3. The board reboots automatically

**Order matters for pairing**: flash the dongle first, then pair the left half, then the right half. Prospector arranges its battery widgets in pairing order.

If pairings ever get wedged, use `settings_reset_xiao.uf2` for either half or
`settings_reset_dongle.uf2` for the Pro Micro footprint dongle, then re-flash
the normal firmware.

## ZMK Studio

With the dongle plugged in via USB, open [zmk.studio](https://zmk.studio) (Chrome / Edge) and connect. The keyboard will render with its actual column-staggered geometry and rotated thumbs. Edits persist to the dongle's storage and survive power cycles.

## Credits

- [ZMK](https://zmk.dev) — firmware
- [carrefinho/prospector-zmk-module](https://github.com/carrefinho/prospector-zmk-module) — dongle display + status screens
