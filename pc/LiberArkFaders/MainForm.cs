using System.Text.Json;
using HidSharp;
using NAudio.CoreAudioApi;

namespace LiberArkFaders;

/// <summary>
/// Reads the LiberArk68 dongle's hid-io fader joystick (report id 2:
/// [1]=left 0..254, [2]=right 0..254) and drives the volume of two chosen
/// Windows output devices (e.g. the Audeze Maxwell Game / Chat endpoints).
/// </summary>
public class MainForm : Form
{
    const int VID = 0x1D50, PID = 0x615E;

    // Fader byte (0..254) -> volume percent. The slide pot is a strong S-taper,
    // so this piecewise curve inverts it to feel ~linear across the throw.
    // Measured on this build: the bottom rests at a clean, stable 0, and the top
    // saturates around 248-250 (the jitter there is ADC noise, NOT the firmware
    // clamp -- the byte never reaches the 254 ceiling, so the pot's own taper is
    // what limits the top, and no firmware change helps it). The end points are
    // continuous dead bands -- ByteToPercent clamps past them, so byte 0 reads
    // 0% and byte >= 247 reads 100% with no cliff. Recalibrate from the live
    // "raw (min-max)" readouts if the pot or wiring changes.
    static readonly (int b, int pct)[] Curve =
        { (0, 0), (11, 25), (124, 50), (241, 75), (247, 100) };

    sealed class DeviceItem
    {
        public required string Id;
        public required string Name;
        public required MMDevice Device;
        public override string ToString() => Name;
    }

    /// <summary>Per-fader UI + filtering state.</summary>
    sealed class Axis
    {
        public required ComboBox Combo;
        public required ProgressBar Bar;
        public required Label Lbl;
        public double Sm = -1;          // EMA state (smoothed raw byte)
        public int LastPct = -1;        // last percent pushed to the device (deadband)
        public int Min = 255, Max = 0;  // observed raw range, for calibration
    }

    sealed class Settings
    {
        public string? LeftDeviceId { get; set; }
        public string? RightDeviceId { get; set; }
    }

    static string SettingsPath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "LiberArkFaders", "settings.json");

    readonly ComboBox _cbLeft = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 300 };
    readonly ComboBox _cbRight = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = 300 };
    readonly ProgressBar _pbLeft = new() { Maximum = 100, Width = 300 };
    readonly ProgressBar _pbRight = new() { Maximum = 100, Width = 300 };
    readonly Label _lblLeft = new() { Text = "--", AutoSize = true };
    readonly Label _lblRight = new() { Text = "--", AutoSize = true };
    readonly Label _status = new() { Text = "Starting...", AutoSize = true, Dock = DockStyle.Bottom, Padding = new Padding(8) };

    readonly MMDeviceEnumerator _enum = new();

    Axis _left = null!, _right = null!;

    Thread? _hidThread;
    volatile bool _run;
    bool _loadingSettings;

    public MainForm()
    {
        Text = "LiberArk68 Faders";
        ClientSize = new Size(540, 250);
        FormBorderStyle = FormBorderStyle.FixedSingle;
        MaximizeBox = false;
        StartPosition = FormStartPosition.CenterScreen;

        var lay = new TableLayoutPanel
        {
            Dock = DockStyle.Top,
            AutoSize = true,
            ColumnCount = 3,
            Padding = new Padding(12),
            CellBorderStyle = TableLayoutPanelCellBorderStyle.None,
        };
        lay.Controls.Add(new Label { Text = "Left fader", AutoSize = true, Anchor = AnchorStyles.Left, Padding = new Padding(0, 6, 8, 0) }, 0, 0);
        lay.Controls.Add(_cbLeft, 1, 0);
        lay.Controls.Add(_lblLeft, 2, 0);
        lay.Controls.Add(_pbLeft, 1, 1);
        lay.Controls.Add(new Label { Text = "Right fader", AutoSize = true, Anchor = AnchorStyles.Left, Padding = new Padding(0, 12, 8, 0) }, 0, 2);
        lay.Controls.Add(_cbRight, 1, 2);
        lay.Controls.Add(_lblRight, 2, 2);
        lay.Controls.Add(_pbRight, 1, 3);

        var btnRefresh = new Button { Text = "Refresh devices", AutoSize = true, Margin = new Padding(0, 10, 0, 0) };
        btnRefresh.Click += (_, _) => LoadDevices();
        lay.Controls.Add(btnRefresh, 1, 4);

        Controls.Add(lay);
        Controls.Add(_status);

        _left = new Axis { Combo = _cbLeft, Bar = _pbLeft, Lbl = _lblLeft };
        _right = new Axis { Combo = _cbRight, Bar = _pbRight, Lbl = _lblRight };

        _cbLeft.SelectedIndexChanged += (_, _) => OnDevicePicked();
        _cbRight.SelectedIndexChanged += (_, _) => OnDevicePicked();

        Load += (_, _) => { LoadDevices(); LoadSettings(); StartHid(); };
        FormClosing += (_, _) => { _run = false; };
    }

    // ---- audio devices ----------------------------------------------------

    void LoadDevices()
    {
        string? prevLeft = (_cbLeft.SelectedItem as DeviceItem)?.Id;
        string? prevRight = (_cbRight.SelectedItem as DeviceItem)?.Id;

        var items = _enum.EnumerateAudioEndPoints(DataFlow.Render, DeviceState.Active)
            .Select(d => new DeviceItem { Id = d.ID, Name = d.FriendlyName, Device = d })
            .OrderBy(d => d.Name)
            .ToArray();

        foreach (var cb in new[] { _cbLeft, _cbRight })
        {
            cb.BeginUpdate();
            cb.Items.Clear();
            cb.Items.AddRange(items);
            cb.EndUpdate();
        }
        SelectById(_cbLeft, prevLeft);
        SelectById(_cbRight, prevRight);
    }

    static void SelectById(ComboBox cb, string? id)
    {
        if (id == null) return;
        for (int i = 0; i < cb.Items.Count; i++)
            if (cb.Items[i] is DeviceItem di && di.Id == id) { cb.SelectedIndex = i; return; }
    }

    void OnDevicePicked()
    {
        if (_loadingSettings) return;
        SaveSettings();
    }

    // ---- settings ---------------------------------------------------------

    void LoadSettings()
    {
        try
        {
            if (!File.Exists(SettingsPath)) return;
            var s = JsonSerializer.Deserialize<Settings>(File.ReadAllText(SettingsPath));
            if (s == null) return;
            _loadingSettings = true;
            SelectById(_cbLeft, s.LeftDeviceId);
            SelectById(_cbRight, s.RightDeviceId);
        }
        catch { /* ignore corrupt settings */ }
        finally { _loadingSettings = false; }
    }

    void SaveSettings()
    {
        try
        {
            var s = new Settings
            {
                LeftDeviceId = (_cbLeft.SelectedItem as DeviceItem)?.Id,
                RightDeviceId = (_cbRight.SelectedItem as DeviceItem)?.Id,
            };
            Directory.CreateDirectory(Path.GetDirectoryName(SettingsPath)!);
            File.WriteAllText(SettingsPath, JsonSerializer.Serialize(s));
        }
        catch { /* best effort */ }
    }

    // ---- calibration ------------------------------------------------------

    static double ByteToPercent(int b)
    {
        if (b <= Curve[0].b) return Curve[0].pct;
        for (int i = 1; i < Curve.Length; i++)
        {
            if (b <= Curve[i].b)
            {
                var (b0, p0) = Curve[i - 1];
                var (b1, p1) = Curve[i];
                return p0 + (double)(b - b0) / (b1 - b0) * (p1 - p0);
            }
        }
        return Curve[^1].pct;
    }

    // ---- HID --------------------------------------------------------------

    static SortedSet<int> UsagePages(HidDevice d)
    {
        var pages = new SortedSet<int>();
        try
        {
            foreach (var item in d.GetReportDescriptor().DeviceItems)
                foreach (var u in item.Usages.GetAllValues())
                    pages.Add((int)(u >> 16));
        }
        catch { }
        return pages;
    }

    static HidDevice? FindFader() =>
        DeviceList.Local.GetHidDevices()
            .Where(d => d.VendorID == VID && d.ProductID == PID && UsagePages(d).Contains(0x0001))
            .OrderBy(d => d.GetMaxInputReportLength())
            .FirstOrDefault();

    void StartHid()
    {
        _run = true;
        _hidThread = new Thread(HidLoop) { IsBackground = true, Name = "fader-hid" };
        _hidThread.Start();
    }

    void HidLoop()
    {
        while (_run)
        {
            var dev = FindFader();
            if (dev == null) { SetStatus("Dongle not found — plug it in…"); Thread.Sleep(1500); continue; }
            if (!dev.TryOpen(out HidStream stream)) { SetStatus("Found dongle, but couldn't open it"); Thread.Sleep(1500); continue; }

            string devName; try { devName = dev.GetProductName(); } catch { devName = "LiberArk68"; }
            SetStatus($"Connected to {devName}");
            using (stream)
            {
                stream.ReadTimeout = 1000;
                var buf = new byte[dev.GetMaxInputReportLength()];
                while (_run)
                {
                    int n;
                    try { n = stream.Read(buf, 0, buf.Length); }
                    catch (TimeoutException) { continue; }
                    catch { break; } // disconnected — fall out and re-find
                    if (n >= 3) OnFaders(buf[1], buf[2]);
                }
            }
        }
    }

    void OnFaders(int left, int right)
    {
        if (!IsHandleCreated) return;
        BeginInvoke(() =>
        {
            ApplyAxis(_left, left);
            ApplyAxis(_right, right);
        });
    }

    void ApplyAxis(Axis a, int raw)
    {
        if (raw < a.Min) a.Min = raw;
        if (raw > a.Max) a.Max = raw;

        // Smooth the raw byte (EMA) for a stable reading, then map through the
        // curve. ByteToPercent clamps to the end points, so the dead bands at
        // 0% / 100% are continuous with the body — no cliff.
        a.Sm = a.Sm < 0 ? raw : a.Sm * 0.7 + raw * 0.3;
        int b = (int)Math.Round(a.Sm);
        int p = Math.Clamp((int)Math.Round(ByteToPercent(b)), 0, 100);

        a.Bar.Value = p;
        a.Lbl.Text = $"{p}%   raw {raw} ({a.Min}-{a.Max})";

        if (p == a.LastPct) return;     // deadband: only push 1% steps
        a.LastPct = p;
        if (a.Combo.SelectedItem is DeviceItem di)
        {
            try { di.Device.AudioEndpointVolume.MasterVolumeLevelScalar = p / 100f; }
            catch { /* device vanished; user can Refresh */ }
        }
    }

    void SetStatus(string text)
    {
        if (!IsHandleCreated) return;
        BeginInvoke(() => _status.Text = text);
    }
}
