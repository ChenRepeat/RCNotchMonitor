using System;
using System.Diagnostics;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using LibreHardwareMonitor.Hardware;

[StructLayout(LayoutKind.Sequential, Pack = 1, CharSet = CharSet.Ansi)]
public struct ShmSensorData
{
    public uint magic;
    public uint version;
    public ulong updateTickMs;
    public float cpuTempC;
    public float cpuPowerW;
    public float cpuClockMhz;
    public float gpuTempC;
    public float gpuLoadPct;
    public float gpuPowerW;
    public float gpuVramUsedGb;
    public float gpuVramTotalGb;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 96)]
    public string cpuName;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 96)]
    public string gpuName;
}

public class UpdateVisitor : IVisitor
{
    public void VisitComputer(IComputer computer) { computer.Traverse(this); }
    public void VisitHardware(IHardware hardware)
    {
        hardware.Update();
        foreach (IHardware subHardware in hardware.SubHardware) subHardware.Accept(this);
    }
    public void VisitSensor(ISensor sensor) { }
    public void VisitParameter(IParameter parameter) { }
}

class Program
{
    static void Main(string[] args)
    {
        int parentPid = -1;
        if (args.Length >= 1) int.TryParse(args[0], out parentPid);

        Computer computer = new Computer
        {
            IsCpuEnabled = true,
            IsGpuEnabled = true,
            IsMotherboardEnabled = true
        };

        try
        {
            computer.Open();
        }
        catch
        {
            return;
        }

        UpdateVisitor visitor = new UpdateVisitor();
        int structSize = Marshal.SizeOf(typeof(ShmSensorData));

        using (var mmf = MemoryMappedFile.CreateOrOpen("Local\\WinNotchSensorShm", 512))
        using (var accessor = mmf.CreateViewAccessor(0, 512))
        {
            byte[] buffer = new byte[structSize];
            while (true)
            {
                if (parentPid > 0)
                {
                    try
                    {
                        var proc = Process.GetProcessById(parentPid);
                        if (proc.HasExited) break;
                    }
                    catch
                    {
                        break;
                    }
                }

                try
                {
                    computer.Accept(visitor);

                    ShmSensorData data = new ShmSensorData
                    {
                        magic = 0x574E4D31,
                        version = 1,
                        updateTickMs = (ulong)Environment.TickCount,
                        cpuTempC = 0,
                        cpuPowerW = 0,
                        cpuClockMhz = 0,
                        gpuTempC = 0,
                        gpuLoadPct = 0,
                        gpuPowerW = 0,
                        gpuVramUsedGb = 0,
                        gpuVramTotalGb = 0,
                        cpuName = "",
                        gpuName = ""
                    };

                    foreach (IHardware hw in computer.Hardware)
                    {
                        if (hw.HardwareType == HardwareType.Cpu)
                        {
                            data.cpuName = hw.Name ?? "";
                            float fallbackTemp = 0;
                            float maxClock = 0;
                            foreach (ISensor s in hw.Sensors)
                            {
                                if (!s.Value.HasValue) continue;
                                float val = s.Value.Value;
                                if (s.SensorType == SensorType.Temperature)
                                {
                                    string n = s.Name ?? "";
                                    if (n.IndexOf("Tctl", StringComparison.OrdinalIgnoreCase) >= 0 ||
                                        n.IndexOf("Package", StringComparison.OrdinalIgnoreCase) >= 0 ||
                                        n.IndexOf("Average", StringComparison.OrdinalIgnoreCase) >= 0)
                                    {
                                        data.cpuTempC = val;
                                    }
                                    else if (val > fallbackTemp && val < 115)
                                    {
                                        fallbackTemp = val;
                                    }
                                }
                                else if (s.SensorType == SensorType.Power)
                                {
                                    if ((s.Name ?? "").IndexOf("Package", StringComparison.OrdinalIgnoreCase) >= 0 ||
                                        (s.Name ?? "").IndexOf("PPT", StringComparison.OrdinalIgnoreCase) >= 0 ||
                                        data.cpuPowerW == 0)
                                    {
                                        data.cpuPowerW = val;
                                    }
                                }
                                else if (s.SensorType == SensorType.Clock)
                                {
                                    if ((s.Name ?? "").IndexOf("Core", StringComparison.OrdinalIgnoreCase) >= 0 && val > maxClock)
                                    {
                                        maxClock = val;
                                    }
                                }
                            }
                            if (data.cpuTempC <= 0 && fallbackTemp > 0) data.cpuTempC = fallbackTemp;
                            if (maxClock > 0) data.cpuClockMhz = maxClock;
                        }
                        else if (hw.HardwareType == HardwareType.GpuNvidia ||
                                 hw.HardwareType == HardwareType.GpuAmd ||
                                 hw.HardwareType == HardwareType.GpuIntel)
                        {
                            // Prefer discrete GPU over integrated Radeon Graphics if both exist
                            bool isIntegrated = (hw.Name ?? "").IndexOf("Radeon(TM) Graphics", StringComparison.OrdinalIgnoreCase) >= 0;
                            if (!string.IsNullOrEmpty(data.gpuName) && isIntegrated) continue;

                            data.gpuName = hw.Name ?? "";
                            foreach (ISensor s in hw.Sensors)
                            {
                                if (!s.Value.HasValue) continue;
                                float val = s.Value.Value;
                                if (s.SensorType == SensorType.Temperature && (s.Name ?? "").IndexOf("Core", StringComparison.OrdinalIgnoreCase) >= 0)
                                    data.gpuTempC = val;
                                else if (s.SensorType == SensorType.Load && (s.Name ?? "").IndexOf("Core", StringComparison.OrdinalIgnoreCase) >= 0)
                                    data.gpuLoadPct = val;
                                else if (s.SensorType == SensorType.Power)
                                    data.gpuPowerW = val;
                                else if (s.SensorType == SensorType.SmallData && (s.Name ?? "").IndexOf("Memory Used", StringComparison.OrdinalIgnoreCase) >= 0)
                                    data.gpuVramUsedGb = val / 1024.0f;
                                else if (s.SensorType == SensorType.SmallData && (s.Name ?? "").IndexOf("Memory Total", StringComparison.OrdinalIgnoreCase) >= 0)
                                    data.gpuVramTotalGb = val / 1024.0f;
                            }
                        }
                        else if (hw.HardwareType == HardwareType.Motherboard && data.cpuTempC <= 0)
                        {
                            foreach (IHardware sub in hw.SubHardware)
                            {
                                foreach (ISensor s in sub.Sensors)
                                {
                                    if (s.SensorType == SensorType.Temperature && s.Value.HasValue && s.Value.Value > 20 && s.Value.Value < 110)
                                    {
                                        if ((s.Name ?? "").IndexOf("CPU", StringComparison.OrdinalIgnoreCase) >= 0 || data.cpuTempC <= 0)
                                            data.cpuTempC = s.Value.Value;
                                    }
                                }
                            }
                        }
                    }

                    IntPtr ptr = Marshal.AllocHGlobal(structSize);
                    try
                    {
                        Marshal.StructureToPtr(data, ptr, false);
                        Marshal.Copy(ptr, buffer, 0, structSize);
                        accessor.WriteArray(0, buffer, 0, structSize);
                    }
                    finally
                    {
                        Marshal.FreeHGlobal(ptr);
                    }
                }
                catch
                {
                }

                if (args.Length >= 2 && args[1] == "--once") break;
                Thread.Sleep(500);
            }
        }

        computer.Close();
    }
}
