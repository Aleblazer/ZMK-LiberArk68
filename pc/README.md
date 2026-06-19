# LiberArk68 PC companion

Host-side tooling for the dual-fader volume variant. The dongle exposes the
two faders as a 2-axis HID "joystick" on usage page `0xFF0C` (via
[zmk-hid-io](https://github.com/badjeff/zmk-hid-io)); these tools read that and
drive Windows audio.

## LiberArkFaders (C#) — bring-up / verification, and the future GUI

The main app, built on .NET. Right now it's a console HID reader; it grows
into the device-picker GUI. Requires the .NET SDK:

```bat
winget install Microsoft.DotNet.SDK.8
```

```bat
cd pc\LiberArkFaders
dotnet run                 :: list every HID interface (with usage pages)
dotnet run -- 0xFF0C       :: open the fader interface and print changing bytes
dotnet run -- 2            :: ...or open by the [index] from the list
```

Move each slider and note which byte index reacts (left vs right) and that it
sweeps ~0-254.

## fader_read.py — optional Python alternative

Same job, if you happen to have Python instead of .NET:

```bat
py -m pip install hidapi
py pc\fader_read.py
```

## Coming next

A small GUI app that maps each fader to a chosen Windows output device (e.g.
the Audeze Maxwell "Game" / "Chat" endpoints), applies the S-taper
calibration curve, smooths jitter, and sets per-device volume via the Core
Audio API.
