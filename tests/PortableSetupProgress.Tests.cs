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
            var show = formType.GetMethod("ShowProgress", InstancePrivate);
            var updateType = formType.GetNestedType("SetupUpdate", BindingFlags.NonPublic);
            var phaseType = assembly.GetType("SAPortable.SetupPhase", true);
            Action<string,long,long> report = delegate(string phase, long completed, long total) {
                var update = Activator.CreateInstance(updateType, true);
                updateType.GetField("Phase").SetValue(update, Enum.Parse(phaseType, phase));
                updateType.GetField("Completed").SetValue(update, completed);
                updateType.GetField("Total").SetValue(update, total);
                updateType.GetField("Item").SetValue(update, "Preparing files");
                show.Invoke(form, new object[] { update });
            };
            var cases = new[] {
                new long[] { 985, 1000, 985 },
                new long[] { 1000, 1000, 1000 },
                new long[] { 1200, 1000, 1000 },
                new long[] { -20, 1000, 0 },
                new long[] { 1, 0, 0 },
                new long[] { long.MaxValue, long.MaxValue, 1000 },
            };
            foreach (var test in cases) {
                report("Packages", 0, 0);
                report("Files", test[0], test[1]);
                if (bar.Value != test[2])
                    throw new Exception("Unexpected progress for " + test[0] + "/" + test[1] + ": " + bar.Value);
            }
            report("Install", 75, 100);
            report("Install", 10, 100);
            if (bar.Value != 750) throw new Exception("An update regressed progress inside the same phase.");
            report("Resources", 0, 0);
            if (bar.Value != 0 || bar.Style != ProgressBarStyle.Marquee)
                throw new Exception("An indeterminate phase must start its own progress.");
            var outcome = formType.GetField("outcome", InstancePrivate);
            var labels = (Label[])formType.GetField("stepLabels", InstancePrivate).GetValue(form);
            var updateChecklist = formType.GetMethod("UpdateChecklist", InstancePrivate);
            outcome.SetValue(form, Enum.Parse(outcome.FieldType, "Running"));
            report("Files", 5, 10);
            if (!labels[0].Text.StartsWith("✓") || !labels[2].Text.StartsWith("●") || !labels[4].Text.StartsWith("○"))
                throw new Exception("Progress checklist failed to mark complete, active and pending rows.");
            outcome.SetValue(form, Enum.Parse(outcome.FieldType, "Failed"));
            updateChecklist.Invoke(form, null);
            if (!labels[2].Text.StartsWith("✕") || !labels[0].Text.StartsWith("✓") || !labels[4].Text.StartsWith("○"))
                throw new Exception("Checklist failed to retain the failure and completed stages.");
            outcome.SetValue(form, Enum.Parse(outcome.FieldType, "Complete"));
            updateChecklist.Invoke(form, null);
            foreach (var label in labels)
                if (!label.Text.StartsWith("✓")) throw new Exception("All successful rows must be marked complete.");
            var diagnostic = assembly.GetType("SAPortable.SetupDiagnostic", true).GetMethod("Build");
            var error = new System.IO.IOException("Cannot open D:\\Other Account\\Private\\custom.iso\nContact person@example.com");
            error.Data["SetupTool"] = "Verify ISO";
            error.Data["SetupExitCode"] = 7;
            string text = (string)diagnostic.Invoke(null, new object[] { "0.3.14", "Check", "Steam", false, error, new string[0] });
            if (text.Contains("Other Account") || text.Contains("person@example.com") ||
                !text.Contains("Cannot open") || !text.Contains("Verify ISO") || !text.Contains("7"))
                throw new Exception("Diagnostic failed to redact personal information or lost the actionable error.");
            Console.WriteLine("Passed progress, checklist and privacy checks on the actual installer.");
            return 0;
        }
    }
}
