using HidSharp;

// LiberArk68 fader reader (Phase 2 bring-up).
//
//   dotnet run                 list every HID interface (with usage pages)
//   dotnet run -- <index>      open that interface and print changing bytes
//   dotnet run -- 0xFF0C       open the first interface on that usage page
//
// Use it to confirm both faders reach the PC and see which byte is the left
// vs right slider. This console grows into the device-picker GUI next.

const int HidIoUsagePage = 0xFF0C;

var devices = DeviceList.Local.GetHidDevices().ToList();
if (devices.Count == 0)
{
    Console.WriteLine("No HID devices found. Is the dongle plugged in?");
    return;
}

static IEnumerable<int> UsagePages(HidDevice d)
{
    var pages = new SortedSet<int>();
    try
    {
        foreach (var item in d.GetReportDescriptor().DeviceItems)
            foreach (var usage in item.Usages.GetAllValues())
                pages.Add((int)(usage >> 16));
    }
    catch { /* some interfaces refuse descriptor reads; ignore */ }
    return pages;
}

string Describe(HidDevice d)
{
    string name;
    try { name = d.GetProductName(); } catch { name = "?"; }
    var pages = UsagePages(d);
    var pageStr = pages.Count > 0 ? string.Join(",", pages.Select(p => $"0x{p:X4}")) : "?";
    return $"VID=0x{d.VendorID:X4} PID=0x{d.ProductID:X4} pages=[{pageStr}] in={d.GetMaxInputReportLength()} | {name}";
}

// No argument: list and exit.
if (args.Length == 0)
{
    for (int i = 0; i < devices.Count; i++)
        Console.WriteLine($"[{i}] {Describe(devices[i])}");
    Console.WriteLine("\nRun:  dotnet run -- <index>   or   dotnet run -- 0xFF0C");
    return;
}

// Resolve the target: an index, or a usage page like 0xFF0C.
HidDevice? target = null;
int arg = Convert.ToInt32(args[0], args[0].StartsWith("0x") ? 16 : 10);
if (args[0].StartsWith("0x") || arg > 0xFF)
    target = devices.FirstOrDefault(d => UsagePages(d).Contains(arg));
else if (arg >= 0 && arg < devices.Count)
    target = devices[arg];

if (target == null)
{
    Console.WriteLine("Could not resolve that device. Listing what is available:");
    for (int i = 0; i < devices.Count; i++)
        Console.WriteLine($"[{i}] {Describe(devices[i])}");
    return;
}

Console.WriteLine($"Opening: {Describe(target)}");
if (!target.TryOpen(out HidStream stream))
{
    Console.WriteLine("Failed to open the device (another app may hold it).");
    return;
}

Console.WriteLine("Move the faders — changed bytes are marked. Ctrl+C to stop.\n");
var buffer = new byte[target.GetMaxInputReportLength()];
byte[]? last = null;
using (stream)
{
    stream.ReadTimeout = Timeout.Infinite;
    while (true)
    {
        int n = stream.Read(buffer, 0, buffer.Length);
        if (n <= 0) continue;
        var data = buffer[..n];
        if (last != null && data.AsSpan().SequenceEqual(last)) continue;

        Console.WriteLine("bytes: " + string.Join(" ", data.Select(b => b.ToString().PadLeft(3))));
        if (last != null && last.Length == data.Length)
        {
            var marks = Enumerable.Range(0, data.Length)
                .Select(i => data[i] != last[i] ? "^^^" : "   ");
            Console.WriteLine("       " + string.Join(" ", marks));
        }
        last = data;
    }
}
