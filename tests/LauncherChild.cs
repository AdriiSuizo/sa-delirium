using System;
using System.IO;
using System.Threading;
using System.Web.Script.Serialization;
public static class LauncherChild {
    public static int Main(string[] args) {
        File.WriteAllText("child-result.json", new JavaScriptSerializer().Serialize(new {
            args = args, cwd = Environment.CurrentDirectory, inherited = Environment.GetEnvironmentVariable("SA_LAUNCHER_TEST") }));
        Thread.Sleep(350); return 42;
    }
}
