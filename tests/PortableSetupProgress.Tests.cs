using System;
using System.Reflection;
using System.Windows.Forms;

internal static class PortableSetupProgressTests {
    [STAThread]
    static int Main(string[] args) {
        var assembly = Assembly.LoadFrom(args[0]);
        var program = assembly.GetType("SAPortable.Program", true);
        var manifest = program.GetMethod("ReadManifest").Invoke(null, null);
        var formType = assembly.GetType("SAPortable.SetupForm", true);
        const BindingFlags InstancePrivate = BindingFlags.Instance | BindingFlags.NonPublic;

        using (var form = (Form)Activator.CreateInstance(formType, manifest)) {
            var bar = (ProgressBar)formType.GetField("progress", InstancePrivate).GetValue(form);
            var report = formType.GetMethod("ShowProgress", InstancePrivate);
            if (report == null) {
                // Reproduce the value that was sent by BeginWork in 0.3.12.
                bar.Value = 985;
                throw new Exception("The baseline unexpectedly accepted the out-of-range value.");
            }

            var cases = new[] {
                new long[] { 985, 1000, 985 },
                new long[] { 1000, 1000, 1000 },
                new long[] { 1200, 1000, 1000 },
                new long[] { -20, 1000, 0 },
                new long[] { 1, 0, 0 },
                new long[] { long.MaxValue, long.MaxValue, 1000 },
            };

            foreach (var test in cases) {
                report.Invoke(form, new object[] { test[0], test[1], "Preparing files" });
                if (bar.Value != test[2])
                    throw new Exception("Unexpected progress for " + test[0] + "/" + test[1] + ": " + bar.Value);
            }
            bar.Value = bar.Maximum;
            Console.WriteLine("Passed six progress cases and the completion value on the actual WinForms control.");
            return 0;
        }
    }
}
