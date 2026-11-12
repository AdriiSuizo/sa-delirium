using System;
using System.IO;
using System.Linq;
using System.Text;
using SASetup;

public static class InstallerTests {
    static int checks;
    static string root;
    static void Assert(bool valid, string reason) { if (!valid) throw new Exception(reason); ++checks; }
    static void Reject(Action action, string reason) {
        bool rejected = false; try { action(); } catch (IOException) { rejected = true; }
        Assert(rejected, reason);
    }
    class Fixture {
        public string Source, Target;
        public Engine Engine;
        public ProfileIndex Index;
        public Fixture(string name) {
            Source = Path.Combine(root, name, "source with spaces"); Target = Path.Combine(root, name, "steam with spaces");
            Directory.CreateDirectory(Source); Directory.CreateDirectory(Target);
            Put(Source, "gta_sa.exe", "owned-development"); Put(Source, "modloader/.data/plugins/native.dll", "plugin-resource");
            Put(Source, "userfiles/GTASAsf2.b", "protected-save"); Put(Source, "scripts/V_HUD_by_DK22Pac.asi", "excluded");
            Put(Target, "gta-sa.exe", "owned-steam"); Put(Target, "scripts/old-native.asi", "not-to-be-touched");
            Index = new ProfileIndex { schema = 1, profile = "fixture", source_executable = "gta_sa.exe",
                source_executable_sha256 = Engine.Hash(Path.Combine(Source, "gta_sa.exe")),
                stock_steam_executable_sha256 = Engine.Hash(Path.Combine(Target, "gta-sa.exe")),
                files = new[] { "gta_sa.exe", "modloader/.data/plugins/native.dll" }.Select(path => new ProfileFile {
                    path = path, bytes = new FileInfo(Path.Combine(Source, path)).Length,
                    sha256 = Engine.Hash(Path.Combine(Source, path)), components = new string[0] }).ToArray() };
            Index.total_bytes = Index.files.Sum(f => f.bytes);
            Engine = new Engine(Index, Encoding.ASCII.GetBytes("own-launcher"));
        }
    }
    static void Put(string folder, string name, string value) {
        var path = Path.Combine(folder, name); Directory.CreateDirectory(Path.GetDirectoryName(path)); File.WriteAllText(path, value);
    }
    public static int Main(string[] args) {
        Paths.EnableLongPaths();
        root = Path.GetFullPath(args[0]); Directory.CreateDirectory(root);
        var driveRoot = Path.GetPathRoot(root);
        Assert(Paths.Full(driveRoot) == driveRoot, "Drive root is absolute after normalization");
        Assert(Paths.Under(root, driveRoot) && !Paths.Under(driveRoot, driveRoot), "Containment uses the actual drive root");
        var f = new Fixture("roundtrip");
        var sourceHash = Engine.Hash(Path.Combine(f.Source, "gta_sa.exe"));
        var saveHash = Engine.Hash(Path.Combine(f.Source, "userfiles/GTASAsf2.b"));
        var plan = f.Engine.MakePlan(f.Source, f.Target, true);
        Assert(plan.Missing.Length == 0 && plan.FileCount == 2, "Complete source plan");
        var receipt = f.Engine.Install(plan);
        Assert(receipt.State == "installed", "Install committed");
        Assert(Engine.Hash(Path.Combine(f.Target, "gta-sa.exe")) == receipt.LauncherSha256, "Exact launcher installed");
        foreach (var file in f.Index.files)
            Assert(Engine.Hash(Path.Combine(f.Target, "SA", file.path)) == file.sha256, "Exact imported content " + file.path);
        Assert(!File.Exists(Path.Combine(f.Target, "SA/scripts/V_HUD_by_DK22Pac.asi")), "HUD excluded");
        Assert(File.ReadAllText(Path.Combine(f.Target, "scripts/old-native.asi")) == "not-to-be-touched", "Root plugins untouched");
        Assert(Engine.Hash(Path.Combine(f.Source, "gta_sa.exe")) == sourceHash, "Source executable unchanged");
        Assert(Engine.Hash(Path.Combine(f.Source, "userfiles/GTASAsf2.b")) == saveHash, "Source save unchanged");
        Assert(Engine.Hash(Path.Combine(f.Target, "SA/userfiles/GTASAsf2.b")) == saveHash, "Save copied without changes");
        Put(f.Target, "SA/userfiles/GTASAsf2.b", "new-progress");
        // Crash after File.Replace but before the final journal update.
        receipt.State = "prepared"; receipt.LauncherInstalled = false; receipt.ProfileMoved = false;
        Json.Write(Path.Combine(f.Target, Engine.ReceiptName), receipt);
        var restored = f.Engine.Restore(f.Target);
        Assert(Engine.Hash(Path.Combine(f.Target, "gta-sa.exe")) == f.Index.stock_steam_executable_sha256, "Original Steam restored despite stale journal");
        Assert(File.ReadAllText(Path.Combine(restored.RetainedProfile, "userfiles", "GTASAsf2.b")) == "new-progress", "New progress retained");
        Assert(!File.Exists(Path.Combine(f.Target, Engine.ReceiptName)), "Recovery journal archived");
        f = new Fixture("hash-mismatch");
        Put(f.Source, "modloader/.data/plugins/native.dll", "changed-resorce");
        Reject(delegate { f.Engine.Install(f.Engine.MakePlan(f.Source, f.Target, false)); }, "Same-size corrupt source refused");
        Assert(Engine.Hash(Path.Combine(f.Target, "gta-sa.exe")) == f.Index.stock_steam_executable_sha256, "Hash failure leaves Steam original");
        Assert(!Directory.Exists(Path.Combine(f.Target, "SA")), "Incomplete profile never activated");
        f = new Fixture("cancel"); f.Engine.Cancelled = delegate { return true; };
        bool cancelled = false; try { f.Engine.Install(f.Engine.MakePlan(f.Source, f.Target, false)); } catch (OperationCanceledException) { cancelled = true; }
        Assert(cancelled, "Cancellation observed");
        Assert(Engine.Hash(Path.Combine(f.Target, "gta-sa.exe")) == f.Index.stock_steam_executable_sha256, "Cancellation keeps Steam original");
        f = new Fixture("post-install-change");
        f.Engine.Install(f.Engine.MakePlan(f.Source, f.Target, false)); Put(f.Target, "gta-sa.exe", "later-user-change");
        Reject(delegate { f.Engine.Restore(f.Target); }, "Later executable modification preserved");
        Assert(File.ReadAllText(Path.Combine(f.Target, "gta-sa.exe")) == "later-user-change", "No destructive restore");
        f = new Fixture("traversal");
        Reject(delegate { f.Engine.MakePlan(f.Source, Path.Combine(f.Source, "nested"), false); }, "Nested paths refused");
        foreach (var path in new[] { "../outside", "a/../../outside", "/absolute", "C:/absolute", "a:stream", "a\\b", "NUL.txt", "a./b" })
            Reject(delegate { Paths.Inside(root, path); }, "Unsafe path refused " + path);
        f.Index.files[0].path = "../outside";
        Reject(delegate { new Engine(f.Index, new byte[0]); }, "Unsafe index refused");
        f = new Fixture("excluded-index"); f.Index.files[0].path = "scripts/V_HUD_by_DK22Pac.asi";
        Reject(delegate { new Engine(f.Index, new byte[0]); }, "Excluded gameplay cannot re-enter index");
        f = new Fixture("receipt-path"); var installed = f.Engine.Install(f.Engine.MakePlan(f.Source, f.Target, false));
        installed.BackupDirectory = "../outside"; Json.Write(Path.Combine(f.Target, Engine.ReceiptName), installed);
        Reject(delegate { f.Engine.Restore(f.Target); }, "Malicious journal refused");
        var library = Path.Combine(root, "library"); var second = Path.Combine(root, "second library");
        Directory.CreateDirectory(Path.Combine(library, "steamapps")); Directory.CreateDirectory(second);
        File.WriteAllText(Path.Combine(library, "steamapps/libraryfolders.vdf"), "\"libraryfolders\" { // comment\n\"1\" { \"path\" \"" + second.Replace("\\", "\\\\") + "\" \"apps\" { \"12120\" \"100\" } } }");
        Assert(Discovery.SteamLibraries(library).Contains(second), "Current VDF with nested apps and escaped paths");
        File.WriteAllText(Path.Combine(library, "steamapps/libraryfolders.vdf"), "\"LibraryFolders\" { \"1\" \"" + second.Replace("\\", "\\\\") + "\" }");
        Assert(Discovery.SteamLibraries(library).Contains(second), "Legacy VDF with space in path");
        var longName = String.Join("/", Enumerable.Repeat("long-directory-with-spaces", 12)) + "/test.bin";
        var longFile = Paths.Inside(root, longName);
        Assert(longFile.StartsWith(@"\\?\"), "Long paths use explicit extended Win32 syntax");
        Directory.CreateDirectory(Path.GetDirectoryName(longFile));
        var nestedLong = Paths.Inside(Path.GetDirectoryName(longFile), "nested.bin");
        File.WriteAllText(nestedLong, "nested-native-path");
        Assert(File.ReadAllText(nestedLong) == "nested-native-path", "Extended paths remain valid when nested");
        f = new Fixture("embedded-core");
        var embeddedName = "modloader/.data/plugins/native.dll";
        var bundled = new System.Collections.Generic.Dictionary<string, byte[]> { { embeddedName, File.ReadAllBytes(Path.Combine(f.Source, embeddedName)) } };
        f.Engine = new Engine(f.Index, Encoding.ASCII.GetBytes("own-launcher"), bundled);
        File.Delete(Path.Combine(f.Source, embeddedName));
        Assert(f.Engine.MakePlan(f.Source, f.Target, false).Missing.Length == 0, "Bundled core needs no source binary");
        f.Engine.Install(f.Engine.MakePlan(f.Source, f.Target, false));
        Assert(File.ReadAllText(Path.Combine(f.Target, "SA", embeddedName)) == "plugin-resource", "Exact bundled core installed");
        bundled[embeddedName] = Encoding.ASCII.GetBytes("modified-core");
        Reject(delegate { new Engine(f.Index, new byte[0], bundled); }, "Altered embedded binary refused");
        f = new Fixture("native-save-path-limit"); f.Index.native_save_path_limit_bytes = 104;
        Reject(delegate { f.Engine.MakePlan(f.Source, f.Target, false); }, "Native 104-byte save path limit checked before copying");
        Directory.CreateDirectory(Path.GetDirectoryName(longFile)); File.WriteAllText(longFile, "long-path-resource");
        Assert(longFile.Length > 300 && File.ReadAllText(longFile) == "long-path-resource", "Long paths work beyond 300 characters");
        // Another Steam build (German R2, a future update) is accepted only
        // when the index lists it; the journal keeps the exact bytes found.
        f = new Fixture("alternative-steam-edition");
        Put(f.Target, "gta-sa.exe", "owned-steam-other-edition");
        var otherEdition = Engine.Hash(Path.Combine(f.Target, "gta-sa.exe"));
        Reject(delegate { f.Engine.MakePlan(f.Source, f.Target, false); }, "Unlisted Steam edition refused");
        f.Index.accepted_steam_executable_sha256 = new[] { otherEdition };
        var alternative = f.Engine.Install(f.Engine.MakePlan(f.Source, f.Target, false));
        Assert(alternative.OriginalExeSha256 == otherEdition, "Journal records the edition actually found");
        Assert(Engine.Hash(Path.Combine(f.Target, alternative.BackupDirectory.Replace('/', Path.DirectorySeparatorChar), "gta-sa.exe")) == otherEdition, "Backup holds the alternative original");
        f.Engine.Restore(f.Target);
        Assert(Engine.Hash(Path.Combine(f.Target, "gta-sa.exe")) == otherEdition, "Alternative edition restored byte for byte");
        f = new Fixture("alternative-journal-not-accepted");
        Put(f.Target, "gta-sa.exe", "owned-steam-other-edition");
        f.Index.accepted_steam_executable_sha256 = new[] { otherEdition };
        f.Engine.Install(f.Engine.MakePlan(f.Source, f.Target, false));
        f.Index.accepted_steam_executable_sha256 = null;
        Reject(delegate { f.Engine.Restore(f.Target); }, "Journal for an edition this index does not accept is refused");
        Assert(Engine.Hash(Path.Combine(f.Target, "gta-sa.exe")) == Engine.Hash(Encoding.ASCII.GetBytes("own-launcher")), "Refused restore leaves the launcher in place");
        Console.WriteLine("PASS: " + checks + " installer assertions"); return 0;
    }
}
