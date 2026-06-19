# LiberArk68 PC companion

Host-side tooling for the dual-fader volume variant. The dongle exposes the
two faders as a 2-axis HID "joystick" on usage page `0xFF0C` (via
[zmk-hid-io](https://github.com/badjeff/zmk-hid-io)); these tools read that and
drive Windows audio.

## fader_read.py — bring-up / verification

Prints the dongle's HID report bytes as they change, so you can confirm both
faders arrive and see which byte is the left vs right slider.

```bat
py -m pip install hidapi
py pc\fader_read.py
```

Move each slider and note which byte index reacts. `--list` dumps every HID
interface the dongle exposes if auto-detect misses it.

## Coming next

A small GUI app that maps each fader to a chosen Windows output device (e.g.
the Audeze Maxwell "Game" / "Chat" endpoints), applies the S-taper
calibration curve, smooths jitter, and sets per-device volume via the Core
Audio API.
