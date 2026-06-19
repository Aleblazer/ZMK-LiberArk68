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

The pot is an S-taper *and* the firmware clamps its top travel to byte 254, so
the byte→percent mapping is a piecewise curve (`Curve` in `MainForm.cs`), not
linear. Its end points snap to a clean 0% / 100% so the bottom doesn't float on
ADC noise and the top always reaches full volume.

Each fader label shows a live `raw (min-max)` readout. To recalibrate: sweep
both faders fully bottom-to-top once, note the observed min/max bytes, set
`Curve`'s first point to `(min, 0)` and last to `(max, 100)`, and adjust the
mids to taste.

## fader_read.py — optional Python byte-dumper

A minimal raw-report reader, handy for debugging without .NET:

```bat
py -m pip install hidapi
py pc\fader_read.py
```
