# LiberArk68 PC companion

Host-side app for the dual-fader volume variant. The dongle exposes the two
slide faders as a HID joystick (Generic Desktop, report id 2 — X = left,
Y = right, each a signed 16-bit little-endian axis carrying the raw wiper
voltage ~0..3300) via a [fork of zmk-hid-io](https://github.com/Aleblazer/zmk-hid-io/tree/absolute-faders)
patched to forward absolute axes and widened to 16-bit. So in the raw report:
bytes 1–2 = left, bytes 3–4 = right. The app reads that and sets the volume of
two chosen Windows output devices.

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
"Chat" endpoints) — and live bars for each fader. Move a fader and its device's
volume follows.

Each fader also has a **Max %** cap. The throw scales into `0..cap`, so a cap of
60 means bottom = 0%, top = 60%, middle = 30% — handy for devices that get
painfully loud past a certain point. Changing the cap applies immediately
(without moving the fader), so dropping it instantly pulls a loud device back.
Device choices and caps are saved to `%APPDATA%\LiberArkFaders\settings.json`.

To build a standalone exe:

```bat
dotnet publish -c Release -r win-x64 --self-contained false
```

### Calibration

The pot reads strongly compressed at the top of travel, so the value→percent
mapping is a piecewise curve (`Curve` in `MainForm.cs`), not linear — it inverts
that so the throw feels ~linear. The firmware now sends the raw wiper voltage
(~0..3300) over a 16-bit axis instead of an 8-bit 0..254, so the compressed top
keeps its ~150 ADC counts (it was crushed to ~10), and the curve's fine steps no
longer go chunky. The end points are continuous dead bands (value 0 → 0%,
value ≥ the last point → 100%), so the bottom rests cleanly and the top reaches
full volume without a cliff.

The 16-bit value is finer but noisier (~±15 counts of ADC noise), so the app
smooths harder before mapping. Each fader label shows a live `raw (min-max)`
readout. To recalibrate after a pot/wiring change: sweep both faders fully, note
the observed min/max, set `Curve`'s first point to `(min, 0)` and last to
`(max, 100)`, and adjust the mids to taste.

## fader_read.py — optional Python byte-dumper

A minimal raw-report reader, handy for debugging without .NET:

```bat
py -m pip install hidapi
py pc\fader_read.py
```
