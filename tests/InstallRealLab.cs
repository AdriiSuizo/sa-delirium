using System;
using System.IO;
using SASetup;
public static class InstallRealLab {
    public static int Main(string[] args) {
        Paths.EnableLongPaths();
        var engine = Program.CreateEngine();
        long next = 0;
        engine.Progress = delegate(long current, long total, string name) {
            if (current < next) return;
            next = current + 1024L * 1024 * 1024;
            Console.WriteLine((100.0 * current / total).ToString("F1") + "% " + Path.GetFileName(name));
        };
        var plan = engine.MakePlan(args[0], args[1], true);
        var receipt = engine.Install(plan); Json.Write(args[2], receipt);
        Console.WriteLine(receipt.State); return receipt.State == "installed" ? 0 : 1;
    }
}
