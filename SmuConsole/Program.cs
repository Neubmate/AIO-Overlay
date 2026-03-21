using System;
using ZenStates.Core;

namespace SmuConsole
{
    static class Program
    {
        private const int FreqMin  = 550;
        private const int FreqMax  = 7000;
        private const int FreqStep = 25;

        static void Main(string[] args)
        {
            try
            {
                using (Cpu cpu = new Cpu())
                {
                    float? temp = cpu.GetCpuTemperature();
                    bool prochot = cpu.IsProchotEnabled() == true;

                    Console.WriteLine("CPU:           " + cpu.systemInfo.CpuName);
                    Console.WriteLine("CPU ID:        " + cpu.systemInfo.GetCpuIdString() + " (" + cpu.info.codeName + ")");
                    Console.WriteLine("Model:         " + cpu.systemInfo.Model.ToString("X2"));
                    Console.WriteLine("Package:       " + cpu.info.packageType);
                    Console.WriteLine("Config:        " + cpu.info.topology.ccds + " CCD / "
                                                        + cpu.info.topology.ccxs + " CCX / "
                                                        + cpu.systemInfo.PhysicalCoreCount + " cores / "
                                                        + cpu.info.topology.logicalCores + " threads");
                    Console.WriteLine("Mobo:          " + cpu.systemInfo.MbVendor + " " + cpu.systemInfo.MbName);
                    Console.WriteLine("BIOS:          " + cpu.systemInfo.BiosVersion);
                    Console.WriteLine("SMU Version:   " + (cpu.smu.Version == 0 ? "N/A" : cpu.systemInfo.GetSmuVersionString()));
                    Console.WriteLine("Patch:         " + cpu.systemInfo.PatchLevel.ToString("X8"));
                    Console.WriteLine("Temp:          " + (temp.HasValue ? temp.Value.ToString("F1") : "N/A"));
                    Console.WriteLine("Prochot:       " + (prochot ? "1" : "0"));
                    Console.WriteLine();

                    int acfFreq = ParseAcfArg(args);
                    if (acfFreq > 0)
                    {
                        ApplyFrequency(cpu, acfFreq);
                        return;
                    }
                    else if (acfFreq == -1)
                    {
                        Console.WriteLine("Invalid --acf value. Range: " + FreqMin + "-" + FreqMax + " MHz, steps of " + FreqStep + ".");
                        Environment.Exit(1);
                        return;
                    }

                    // Interactive mode — only when running in a real console (not piped)
                    if (Console.IsInputRedirected)
                        return;

                    Console.WriteLine("-----------------------------------------");
                    Console.WriteLine("  Interactive  |  type 'q' to quit");
                    Console.WriteLine("-----------------------------------------");

                    while (true)
                    {
                        bool ph = cpu.IsProchotEnabled() == true;
                        float? t  = cpu.GetCpuTemperature();

                        Console.Write(ph ? "[PROCHOT] " : "[OK] ");
                        if (t.HasValue)
                            Console.Write(t.Value.ToString("F1") + "C  ");
                        Console.Write("Enter MHz (or 'q'): ");

                        string line = Console.ReadLine();
                        if (line == null || line.Trim().ToLower() == "q")
                            break;

                        if (TryParseFrequency(line, out int freq))
                            ApplyFrequency(cpu, freq);
                        else
                            Console.WriteLine("Invalid. Must be " + FreqMin + "-" + FreqMax + " MHz, multiples of " + FreqStep + ".");
                    }
                }
            }
            catch (ApplicationException ex)
            {
                Console.WriteLine("Error: " + ex.Message);
                Environment.Exit(1);
            }
            catch (Exception ex)
            {
                Console.WriteLine("Unexpected error: " + ex.Message);
                Environment.Exit(1);
            }
        }

        private static void ApplyFrequency(Cpu cpu, int mhz)
        {
            Console.Write("  Setting all cores to " + mhz + " MHz... ");
            Console.WriteLine(cpu.SetFrequencyAllCore(Convert.ToUInt32(mhz)) ? "OK" : "Failed");
        }

        private static int ParseAcfArg(string[] args)
        {
            for (int i = 0; i < args.Length; i++)
            {
                if (string.Compare(args[i], "--acf", StringComparison.OrdinalIgnoreCase) == 0)
                {
                    if (i + 1 < args.Length && TryParseFrequency(args[i + 1], out int freq))
                        return freq;
                    return -1;
                }
            }
            return 0;
        }

        private static bool TryParseFrequency(string input, out int freq)
        {
            if (int.TryParse(input.Trim(), out freq) && freq >= FreqMin && freq <= FreqMax && freq % FreqStep == 0)
                return true;
            freq = 0;
            return false;
        }
    }
}
