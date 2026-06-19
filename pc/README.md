# LiberArk68 PC companion

Host-side app for the dual-fader volume variant. The dongle exposes the two
slide faders as a HID joystick (Generic Desktop, report id 2 — byte 1 = left,
byte 2 = right, each 0..254) via a [fork of zmk-hid-io](https://github.com/Aleblazer/zmk-hid-io/tree/absolute-faders)
patched to forward absolute axes. The app reads that and sets the volume of two
chosen Windows output devices.

## LiberArkFaders (C# / WinForms)

Requires the .NET SDK:

```bat
winget install Microsoft.DotNet.SDK.8
```

Run it:

```bat
cd pc\LiberArkFaders
dotnet run
```

It auto-finds the dongle (VID 0x1D50 / PID 0x615E), shows two dropdowns — pick
the output device each fader controls (e.g. the Audeze Maxwell "Game" and
"Chat" endpoints) — and live bars for each fader. Selections are saved to
`%APPDATA%\LiberArkFaders\settings.json`. Move a fader and its device's volume
follows.

To build a standalone exe:

```bat
dotnet publish -c Release -r win-x64 --self-contained false
```

### Calibration

The pot is an S-taper, so the byte→percent mapping is a piecewise curve
(`Curve` in `MainForm.cs`), not linear. The live bars show the resulting
percent; if the feel is off, capture the byte values at even slider positions
and edit the curve points.

## fader_read.py — optional Python byte-dumper

A minimal raw-report reader, handy for debugging without .NET:

```bat
py -m pip install hidapi
py pc\fader_read.py
```
