using System;
using System.IO;
using System.Reflection;
using System.Text;
using SASetup;
using SAPortable;

// Cache recovery uses an immutable bundled fixture; no game or network access.
static class PortableCacheTests {
    static void Assert(bool condition, string message) { if (!condition) throw new Exception(message); }
    static string Resolve(Composer composer) {
        try { return (string)typeof(Composer).GetMethod("Resolve", BindingFlags.Instance | BindingFlags.NonPublic)
            .Invoke(composer, new object[] { "bundled" }); }
        catch (TargetInvocationException error) { throw error.InnerException; }
    }
    public static int Main(string[] args) {
        try { return Run(args); }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
    static int Run(string[] args) {
        Paths.EnableLongPaths();
        string root = Path.GetFullPath(args[0]);
        var tools = Path.Combine(root, "tools");
        var cache = Path.Combine(root, "cache");
        Directory.CreateDirectory(tools);
        Directory.CreateDirectory(cache);
        var bytes = Encoding.ASCII.GetBytes("immutable original archive fixture");
        var hash = Engine.Hash(bytes);
        var manifest = new Manifest { schema=1, files=new Recipe[0], profile=new ProfileIndex { schema=1 },
            packages=new[] { new Package { id="bundled", filename="original.zip", resolver="bundled", sha256=hash, bytes=bytes.Length } } };
        var original = Path.Combine(tools, "packages", hash, "original.zip");
        Directory.CreateDirectory(Path.GetDirectoryName(original));
        File.WriteAllBytes(original, bytes);
        var cached = Path.Combine(cache, "downloads", hash, "original.zip");
        Directory.CreateDirectory(Path.GetDirectoryName(cached));
        // A previous interrupted/corrupt cache is never installed or trusted,
        // but must not make every subsequent retry fail permanently either.
        File.WriteAllText(cached, "incomplete download");
        var prepared = Resolve(new Composer(manifest, tools, cache));
        Assert(prepared==cached && Engine.Hash(prepared)==hash, "A damaged pinned cache must recover from its verified bundled original");
        Assert(Engine.Hash(original)==hash, "The immutable bundled original changed");
        var quarantine = Directory.GetFiles(Path.GetDirectoryName(cached), "original.zip.invalid-*");
        Assert(quarantine.Length==1 && File.ReadAllText(quarantine[0])=="incomplete download", "Keep the rejected cache separately for diagnosis");
        var modified = File.GetLastWriteTimeUtc(cached);
        Assert(Resolve(new Composer(manifest, tools, cache))==cached && File.GetLastWriteTimeUtc(cached)==modified,
            "A good cache must be reused without replacement");
        var changed = (byte[])bytes.Clone();
        changed[0] ^= 0x40;
        File.WriteAllBytes(cached, changed);
        Assert(Engine.Hash(Resolve(new Composer(manifest, tools, cache)))==hash,
            "Same-size corruption must also be detected by the complete pinned hash");
        // A file held by another program is an access error, not a reason to
        // replace it. The original IOException must remain the reported error.
        File.WriteAllText(cached, "held damaged cache");
        bool blocked=false;
        using (var held = new FileStream(cached, FileMode.Open, FileAccess.ReadWrite, FileShare.None)) {
            try { Resolve(new Composer(manifest, tools, cache)); } catch (IOException error) {
                blocked=(error.HResult & 0xffff)==32;
            }
        }
        Assert(blocked && File.ReadAllText(cached)=="held damaged cache", "A locked cache must remain untouched with its sharing error");
        // A corrupt replacement cannot be installed just because recovery ran.
        File.WriteAllText(original, "also corrupt");
        File.WriteAllText(cached, "broken again");
        bool rejected=false;
        try { Resolve(new Composer(manifest, tools, cache)); } catch (IOException) { rejected=true; }
        Assert(rejected && !File.Exists(cached), "A changed bundled source must still be refused");
        Directory.CreateDirectory(cached);
        File.WriteAllText(Path.Combine(cached,"sentinel.txt"),"unrelated data");
        File.WriteAllBytes(original,bytes);
        rejected=false;
        try { Resolve(new Composer(manifest, tools, cache)); } catch (IOException) { rejected=true; }
        catch (UnauthorizedAccessException) { rejected=true; }
        Assert(rejected && File.ReadAllText(Path.Combine(cached,"sentinel.txt"))=="unrelated data",
            "A directory collision must stop without moving or deleting its contents");
        Console.WriteLine("PASS: pinned cache recovery, same-size corruption, preserved evidence, valid cache reuse, locked cache, corrupt-source rejection and directory collision");
        return 0;
    }
}
