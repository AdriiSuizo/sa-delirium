using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;
using Microsoft.Win32;

namespace SASetup {
    public class ProfileFile {
        public string path, sha256;
        public long bytes;
        public string[] components;
    }
    public class ProfileComponent { public string id, name; public bool requested; public int files; public long bytes; }
    public class ProfileIndex {
        public int schema;
        public int native_save_path_limit_bytes;
        public string profile, source_executable, source_executable_sha256, stock_steam_executable_sha256;
        // Optional extra Steam builds that have their own verified delta.
        // Absent in older indexes: then only the stock build is accepted.
        public string[] accepted_steam_executable_sha256;
        public long total_bytes;
        public bool AcceptsSteamExecutable(string sha256) {
            return sha256 != null && (sha256 == stock_steam_executable_sha256 ||
                (accepted_steam_executable_sha256 != null && Array.IndexOf(accepted_steam_executable_sha256, sha256) >= 0));
        }
        public ProfileFile[] files;
        public ProfileComponent[] components;
    }
    public class Plan {
        public string Source, Target, Profile;
        public long Bytes;
        public int FileCount;
        public bool CopySaves;
        public string[] Missing;
    }
    public class Receipt {
        public int Schema = 1;
        public string State, Target, Source, Profile, OriginalExeSha256, LauncherSha256, StartedUtc;
        public string BackupDirectory, StageDirectory, RetainedProfile;
        public bool ProfileMoved, LauncherInstalled;
        // Previous profile whose saves were copied into this one, if any.
        public string SavesFrom;
        // Absent for Steam installs. "standalone": own folder, game only read.
        public string Mode;
        public string[] Shortcuts;
        public ProfileFile[] OwnedFiles;
    }
    public static class Paths {
        public static void EnableLongPaths() {
            AppContext.SetSwitch("Switch.System.IO.UseLegacyPathHandling", false);
            AppContext.SetSwitch("Switch.System.IO.BlockLongPaths", false);
        }
        public static string Full(string p) {
            if (String.IsNullOrWhiteSpace(p)) throw new IOException("Falta una ruta.");
            var full = Path.GetFullPath(p);
            return full.Length == Path.GetPathRoot(full).Length ? full : full.TrimEnd(Path.DirectorySeparatorChar);
        }
        public static bool Under(string path, string root) {
            var full = Full(path);
            var prefix = Full(root);
            if (!prefix.EndsWith(Path.DirectorySeparatorChar.ToString(), StringComparison.Ordinal)) prefix += Path.DirectorySeparatorChar;
            return full.Length > prefix.Length && full.StartsWith(prefix, StringComparison.OrdinalIgnoreCase);
        }
        public static void NoLinks(string path) {
            for (var p = new DirectoryInfo(Full(path)); p != null; p = p.Parent)
                if (p.Exists && (p.Attributes & FileAttributes.ReparsePoint) != 0)
                    throw new IOException("La ruta contiene un enlace o unión: " + p.FullName);
            if (File.Exists(path) && (File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0)
                throw new IOException("El archivo es un enlace: " + path);
        }
        // Extended form for both sides of a move: Windows refuses to move
        // between an ordinary and a \\?\ path ("identical roots").
        public static string Long(string path) {
            if (path.StartsWith(@"\\?\")) return path;
            var full = Path.GetFullPath(path);
            return full.StartsWith(@"\\") ? @"\\?\UNC\" + full.Substring(2) : @"\\?\" + full;
        }
        public static void MoveDirectory(string source, string destination) { Directory.Move(Long(source), Long(destination)); }
        public static string Inside(string root, string relative) {
            if (String.IsNullOrEmpty(relative) || relative.Contains('\\') || relative.Contains(':') || relative.StartsWith("/"))
                throw new IOException("Ruta de archivo no válida: " + relative);
            var parts = relative.Split('/');
            if (parts.Any(p => p.Length == 0 || p == "." || p == ".." || p.EndsWith(".") || p.EndsWith(" ") ||
                p.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || Regex.IsMatch(p, @"^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)", RegexOptions.IgnoreCase)))
                throw new IOException("Ruta de archivo no válida: " + relative);
            var result = Path.GetFullPath(Path.Combine(root, relative.Replace('/', Path.DirectorySeparatorChar)));
            if (!Under(result, root)) throw new IOException("El archivo sale de su carpeta.");
            NoLinks(result);
            // Explicit extended paths also work when the machine-wide long-path
            // policy is disabled; no registry change is needed by the installer.
            if (result.Length >= 240 && !result.StartsWith(@"\\?\"))
                return result.StartsWith(@"\\") ? @"\\?\UNC\" + result.Substring(2) : @"\\?\" + result;
            return result;
        }
    }
    public static class Json {
        public static JavaScriptSerializer Serializer() { return new JavaScriptSerializer { MaxJsonLength = 64 * 1024 * 1024, RecursionLimit = 100 }; }
        public static T Read<T>(string path) { return Serializer().Deserialize<T>(File.ReadAllText(Paths.Long(path), Encoding.UTF8)); }
        public static void Write(string path, object value) {
            path = Paths.Long(path);
            var temp = path + ".tmp";
            File.WriteAllText(temp, Serializer().Serialize(value), new UTF8Encoding(false));
            if (File.Exists(path)) File.Replace(temp, path, null); else File.Move(temp, path);
        }
    }
    public static class Discovery {
        // A small KeyValues tokenizer handles current and old libraryfolders VDF,
        // escaped Windows paths, comments and nested apps maps.
        public static List<string> SteamLibraries(string root) {
            var result = new List<string> { Paths.Full(root) };
            var file = Path.Combine(root, "steamapps", "libraryfolders.vdf");
            if (!File.Exists(file)) return result;
            var text = File.ReadAllText(file);
            var tokens = Regex.Matches(text, "\\\"((?:[^\\\"\\\\]|\\\\.)*)\\\"|[{}]|//[^\\r\\n]*");
            for (int i = 0; i + 1 < tokens.Count; ++i) {
                if (!tokens[i].Value.StartsWith("\"")) continue;
                var key = tokens[i].Groups[1].Value;
                var next = tokens[i + 1];
                if (!next.Value.StartsWith("\"")) continue;
                var value = next.Groups[1].Value.Replace("\\\\", "\\").Replace("\\\"", "\"");
                if (key.Equals("path", StringComparison.OrdinalIgnoreCase) || Regex.IsMatch(key, @"^\d+$") && Path.IsPathRooted(value)) {
                    try { if (Directory.Exists(value)) result.Add(Paths.Full(value)); } catch (ArgumentException) { }
                }
            }
            return result.Distinct(StringComparer.OrdinalIgnoreCase).ToList();
        }
        public static List<string> SteamGames() {
            var roots = new List<string>();
            using (var key = Registry.CurrentUser.OpenSubKey(@"Software\Valve\Steam"))
                if (key != null && key.GetValue("SteamPath") is string) roots.Add((string)key.GetValue("SteamPath"));
            foreach (var view in new[] { RegistryView.Registry32, RegistryView.Registry64 })
                using (var registry = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view))
                using (var key = registry.OpenSubKey(@"Software\Valve\Steam"))
                    if (key != null && key.GetValue("InstallPath") is string) roots.Add((string)key.GetValue("InstallPath"));
            roots.Add(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Steam"));
            var games = new List<string>();
            foreach (var root in roots.Distinct(StringComparer.OrdinalIgnoreCase)) {
                if (!Directory.Exists(root)) continue;
                foreach (var library in SteamLibraries(root)) {
                    var manifest = Path.Combine(library, "steamapps", "appmanifest_12120.acf");
                    if (!File.Exists(manifest)) continue;
                    var match = Regex.Match(File.ReadAllText(manifest), "\\\"installdir\\\"\\s+\\\"([^\\\"]+)\\\"");
                    if (!match.Success) continue;
                    var name = match.Groups[1].Value;
                    if (name.Contains("/") || name.Contains("\\") || name.Contains(":")) continue;
                    var game = Path.Combine(library, "steamapps", "common", name);
                    if (File.Exists(Path.Combine(game, "gta-sa.exe"))) games.Add(Paths.Full(game));
                }
            }
            return games.Distinct(StringComparer.OrdinalIgnoreCase).ToList();
        }
        public static List<string> Sources(ProfileIndex index) {
            var candidates = new List<string>();
            foreach (var desktop in new[] { Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),
                                          Environment.GetFolderPath(Environment.SpecialFolder.CommonDesktopDirectory) }) {
                if (!Directory.Exists(desktop)) continue;
                candidates.Add(Path.Combine(desktop, "Repo", "GTA SA", "game"));
                object shell = null;
                try {
                    var type = Type.GetTypeFromProgID("WScript.Shell");
                    shell = Activator.CreateInstance(type);
                    foreach (var file in Directory.GetFiles(desktop, "*.lnk")) {
                        if (!Path.GetFileName(file).ToLowerInvariant().Contains("gta")) continue;
                        object shortcut = null;
                        try {
                            shortcut = type.InvokeMember("CreateShortcut", BindingFlags.InvokeMethod, null, shell, new object[] { file });
                            var target = (string)shortcut.GetType().InvokeMember("TargetPath", BindingFlags.GetProperty, null, shortcut, null);
                            if (Path.GetFileName(target).Equals("gta_sa.exe", StringComparison.OrdinalIgnoreCase)) candidates.Add(Path.GetDirectoryName(target));
                        } finally { if (shortcut != null && Marshal.IsComObject(shortcut)) Marshal.ReleaseComObject(shortcut); }
                    }
                } catch (COMException) { } finally { if (shell != null && Marshal.IsComObject(shell)) Marshal.ReleaseComObject(shell); }
            }
            candidates.Add(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "game"));
            return candidates.Where(c => File.Exists(Path.Combine(c, index.source_executable)) &&
                      File.Exists(Path.Combine(c, "scripts", "DeliriumSA.asi")))
                .Select(Paths.Full).Distinct(StringComparer.OrdinalIgnoreCase).ToList();
        }
    }
    public class Engine {
        public const string ReceiptName = "SA-Delirium-install.json";
        public const string ProfileDirectory = "SA";
        public readonly ProfileIndex Index;
        public readonly byte[] Launcher;
        public readonly IDictionary<string, byte[]> Bundled;
        public Action<long, long, string> Progress = delegate { };
        public Func<bool> Cancelled = delegate { return false; };
        // Tests inject a deliberately tiny fixture contract; production always
        // loads its immutable embedded index and embedded launcher.
        public Engine(ProfileIndex index, byte[] launcher, IDictionary<string, byte[]> bundled = null) {
            Index = index; Launcher = launcher;
            Bundled = bundled ?? new Dictionary<string, byte[]>();
            if (Index.schema != 1 || Index.files == null || Index.files.Length == 0) throw new IOException("Índice de instalación no válido.");
            var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            long sum = 0;
            foreach (var file in Index.files) {
                Paths.Inside(Path.GetTempPath(), file.path);
                if (!names.Add(file.path) || file.bytes < 0 || !Regex.IsMatch(file.sha256 ?? "", "^[a-f0-9]{64}$"))
                    throw new IOException("Índice de instalación inconsistente.");
                if (file.path.Equals("scripts/V_HUD_by_DK22Pac.asi", StringComparison.OrdinalIgnoreCase) ||
                    file.path.StartsWith("scripts/GTAVHud_by_DK22Pac/", StringComparison.OrdinalIgnoreCase))
                    throw new IOException("El índice incluye el HUD excluido.");
                sum = checked(sum + file.bytes);
            }
            if (sum != Index.total_bytes) throw new IOException("Tamaño de perfil inconsistente.");
            foreach (var item in Bundled) {
                var expected = Index.files.SingleOrDefault(f => f.path.Equals(item.Key, StringComparison.OrdinalIgnoreCase));
                if (expected == null || expected.bytes != item.Value.LongLength || Hash(item.Value) != expected.sha256)
                    throw new IOException("El mod incluido no coincide con el perfil: " + item.Key);
            }
        }
        public static string Hash(string file) { using (var input = File.OpenRead(file)) using (var hash = SHA256.Create()) return Hex(hash.ComputeHash(input)); }
        public static string Hash(byte[] data) { using (var hash = SHA256.Create()) return Hex(hash.ComputeHash(data)); }
        private static string Hex(byte[] data) { return BitConverter.ToString(data).Replace("-", "").ToLowerInvariant(); }
        public static void NoRunningGame() {
            foreach (var name in new[] { "gta_sa", "gta-sa" })
                if (Process.GetProcessesByName(name).Length != 0)
                    throw new IOException("Cierra GTA San Andreas antes de instalar o restaurar.");
        }
        public Plan MakePlan(string source, string target, bool copySaves) {
            source = Paths.Full(source); target = Paths.Full(target);
            Paths.NoLinks(source); Paths.NoLinks(target);
            if (source.Equals(target, StringComparison.OrdinalIgnoreCase) || Paths.Under(source, target) || Paths.Under(target, source))
                throw new IOException("Origen y destino deben ser carpetas independientes.");
            if (!Directory.Exists(source) || !Directory.Exists(target)) throw new IOException("Comprueba las carpetas de origen y destino.");
            // SA 1.0's native load filename buffers are 104 bytes. PortableGTA
            // uses an absolute path. A short profile folder avoids truncation;
            // reject long library roots before copying any content.
            if (Index.native_save_path_limit_bytes > 0 && Encoding.Default.GetByteCount(Path.Combine(target, ProfileDirectory, "userfiles", "GTASAsf8.b")) >= Index.native_save_path_limit_bytes)
                throw new IOException("La ruta de esta biblioteca es demasiado larga para cargar partidas en SA 1.0. Usa una biblioteca de Steam con una ruta más corta.");
            var exe = Paths.Inside(source, Index.source_executable);
            if (!File.Exists(exe) || Hash(exe) != Index.source_executable_sha256)
                throw new IOException("El origen no contiene la versión de desarrollo compatible con este instalador.");
            var targetExe = Paths.Inside(target, "gta-sa.exe");
            if (!File.Exists(targetExe) || !Index.AcceptsSteamExecutable(Hash(targetExe)))
                throw new IOException("El destino no contiene el ejecutable original de Steam esperado. Si ya está instalado, usa Restaurar primero.");
            if (Directory.Exists(Path.Combine(target, ProfileDirectory)) || File.Exists(Path.Combine(target, ReceiptName)))
                throw new IOException("Ya existe una instalación o una operación pendiente. Usa Restaurar o revisa el diario.");
            var missing = new List<string>();
            foreach (var f in Index.files) {
                if (Bundled.ContainsKey(f.path)) continue;
                var path = Paths.Inside(source, f.path);
                if (!File.Exists(path) || new FileInfo(path).Length != f.bytes) missing.Add(f.path);
            }
            return new Plan { Source = source, Target = target, Profile = Index.profile, Bytes = Index.total_bytes,
                FileCount = Index.files.Length, CopySaves = copySaves, Missing = missing.ToArray() };
        }
        public void CheckSteamTarget(string target) {
            if (!Discovery.SteamGames().Contains(Paths.Full(target), StringComparer.OrdinalIgnoreCase))
                throw new IOException("Selecciona la carpeta de San Andreas clásico reconocida por Steam (AppID 12120).");
        }
        private string CopyChecked(string source, string destination, string expected, long size, ref long completed, long total) {
            Paths.NoLinks(source); Paths.NoLinks(destination);
            Directory.CreateDirectory(Path.GetDirectoryName(destination));
            using (var input = new FileStream(source, FileMode.Open, FileAccess.Read, FileShare.Read))
            using (var output = new FileStream(destination, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            using (var hash = SHA256.Create()) {
                if (input.Length != size) throw new IOException("El origen cambió: " + source);
                var buffer = new byte[1024 * 1024]; int count;
                while ((count = input.Read(buffer, 0, buffer.Length)) != 0) {
                    if (Cancelled()) throw new OperationCanceledException("Instalación cancelada; Steam conserva su ejecutable original.");
                    output.Write(buffer, 0, count); hash.TransformBlock(buffer, 0, count, null, 0);
                    completed += count; Progress(completed, total, source);
                }
                hash.TransformFinalBlock(new byte[0], 0, 0); output.Flush(true);
                var actual = Hex(hash.Hash);
                if (actual != expected) throw new IOException("El archivo de origen no coincide con esta versión: " + source);
                return actual;
            }
        }
        private void Save(Receipt receipt) { Json.Write(Path.Combine(receipt.Target, ReceiptName), receipt); }
        public Receipt Install(Plan plan) {
            NoRunningGame();
            // Re-read paths and source metadata at the mutation boundary.
            var fresh = MakePlan(plan.Source, plan.Target, plan.CopySaves);
            if (fresh.Missing.Length != 0) throw new IOException("Faltan archivos en el origen: " + String.Join(", ", fresh.Missing.Take(5)));
            var drive = new DriveInfo(Path.GetPathRoot(plan.Target));
            if (drive.AvailableFreeSpace < checked(Index.total_bytes + 1024L * 1024 * 1024))
                throw new IOException("No hay espacio suficiente. Se necesitan " + (Index.total_bytes / (1024.0 * 1024 * 1024)).ToString("F1") + " GiB más 1 GiB de margen.");
            string id = DateTime.UtcNow.ToString("yyyyMMdd-HHmmss") + "-" + Guid.NewGuid().ToString("N").Substring(0, 8);
            var receipt = new Receipt { State = "copying", Source = plan.Source, Target = plan.Target,
                Profile = Index.profile, OriginalExeSha256 = Hash(Paths.Inside(plan.Target, "gta-sa.exe")),
                LauncherSha256 = Hash(Launcher), StartedUtc = DateTime.UtcNow.ToString("o"),
                BackupDirectory = "SA-Delirium-backups/" + id, StageDirectory = ".SA-Setup-" + id };
            var stage = Paths.Inside(plan.Target, receipt.StageDirectory);
            var backup = Paths.Inside(plan.Target, receipt.BackupDirectory);
            var original = Paths.Inside(plan.Target, "gta-sa.exe");
            var profile = Paths.Inside(plan.Target, ProfileDirectory);
            Directory.CreateDirectory(stage); Save(receipt);
            try {
                long completed = 0;
                foreach (var f in Index.files) {
                    if (Bundled.ContainsKey(f.path)) {
                        if (Cancelled()) throw new OperationCanceledException();
                        var path = Paths.Inside(stage, f.path); Directory.CreateDirectory(Path.GetDirectoryName(path));
                        using (var output = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.None)) {
                            var bytes = Bundled[f.path]; output.Write(bytes, 0, bytes.Length); output.Flush(true);
                        }
                        completed += f.bytes; Progress(completed, Index.total_bytes, f.path);
                    } else CopyChecked(Paths.Inside(plan.Source, f.path), Paths.Inside(stage, f.path), f.sha256, f.bytes, ref completed, Index.total_bytes);
                }
                // Save/profile import is a copy into a new separate directory.
                // Neither Documents saves nor the development saves are written.
                if (plan.CopySaves && Directory.Exists(Path.Combine(plan.Source, "userfiles"))) {
                    foreach (var file in Directory.GetFiles(Path.Combine(plan.Source, "userfiles"))) {
                        var name = Path.GetFileName(file);
                        if (!Regex.IsMatch(name, @"^(GTASAsf[1-8]\.b|gta_sa\.set)$", RegexOptions.IgnoreCase)) continue;
                        long saveProgress = 0;
                        CopyChecked(file, Paths.Inside(stage, "userfiles/" + name), Hash(file), new FileInfo(file).Length, ref saveProgress, new FileInfo(file).Length);
                    }
                }
                Json.Write(Path.Combine(stage, "SA-Development-profile.json"), new {
                    profile = Index.profile, files = Index.files, copied_saves = plan.CopySaves,
                    omitted_gameplay = new[] { "GTA V HUD / minimap / weapon wheel" },
                    install_utc = DateTime.UtcNow.ToString("o") });
                if (Cancelled()) throw new OperationCanceledException();
                NoRunningGame();
                if (Hash(original) != receipt.OriginalExeSha256) throw new IOException("Steam ha cambiado durante la copia; no se sustituye su ejecutable.");
                Directory.CreateDirectory(backup);
                File.Copy(Paths.Long(original), Paths.Long(Path.Combine(backup, "gta-sa.exe")));
                if (Hash(Path.Combine(backup, "gta-sa.exe")) != receipt.OriginalExeSha256) throw new IOException("La copia de seguridad no coincide.");
                File.WriteAllBytes(Path.Combine(backup, "launcher.exe"), Launcher);
                receipt.State = "prepared"; Save(receipt);
                Paths.MoveDirectory(stage, profile);
                receipt.ProfileMoved = true; Save(receipt);
                var temporaryExe = Path.Combine(backup, "launcher-pending.exe");
                File.WriteAllBytes(temporaryExe, Launcher);
                // Atomic replacement. The independently verified original backup
                // remains available even if the process dies before the journal.
                File.Replace(Paths.Long(temporaryExe), Paths.Long(original), Paths.Long(Path.Combine(backup, "gta-sa.swap-original.exe")));
                receipt.LauncherInstalled = true; receipt.State = "installed"; Save(receipt);
                return receipt;
            } catch {
                receipt.State = "interrupted";
                try { Save(receipt); Restore(plan.Target); } catch { /* Durable journal and original backup survive for recovery. */ }
                throw;
            }
        }
        private Receipt ReadReceipt(string target) {
            target = Paths.Full(target); Paths.NoLinks(target);
            var receipt = Json.Read<Receipt>(Path.Combine(target, ReceiptName));
            if (receipt.Mode != null) throw new IOException("Esta instalación de SA Delirium está en su propia carpeta, no en Steam. Usa Restaurar desde el asistente.");
            if (receipt.Schema != 1 || !target.Equals(Paths.Full(receipt.Target), StringComparison.OrdinalIgnoreCase) ||
                !Regex.IsMatch(receipt.BackupDirectory ?? "", @"^SA-Delirium-backups/\d{8}-\d{6}-[a-f0-9]{8}$") ||
                !Regex.IsMatch(receipt.StageDirectory ?? "", @"^\.SA-Setup-\d{8}-\d{6}-[a-f0-9]{8}$") ||
                !Index.AcceptsSteamExecutable(receipt.OriginalExeSha256) || receipt.LauncherSha256 != Hash(Launcher))
                throw new IOException("Diario de instalación no válido. No se modifica el juego.");
            return receipt;
        }
        public Receipt Restore(string target) {
            NoRunningGame();
            var receipt = ReadReceipt(target);
            var original = Paths.Inside(receipt.Target, "gta-sa.exe");
            var backup = Paths.Inside(receipt.Target, receipt.BackupDirectory);
            var currentHash = Hash(original);
            if (currentHash != receipt.LauncherSha256 && currentHash != receipt.OriginalExeSha256)
                throw new IOException("El ejecutable fue cambiado después de instalar. Se conserva todo para revisión.");
            if (currentHash == receipt.LauncherSha256) {
                var saved = Path.Combine(backup, "gta-sa.exe");
                if (Hash(saved) != receipt.OriginalExeSha256) throw new IOException("El respaldo del ejecutable original no coincide.");
                var temp = Path.Combine(backup, "restore-pending.exe");
                File.Copy(Paths.Long(saved), Paths.Long(temp), true); File.Replace(Paths.Long(temp), Paths.Long(original), null);
            }
            var profile = Paths.Inside(receipt.Target, ProfileDirectory);
            var stage = Paths.Inside(receipt.Target, receipt.StageDirectory);
            var retained = Paths.Inside(receipt.Target, receipt.BackupDirectory + "/retained-profile");
            if (Directory.Exists(profile)) {
                Directory.CreateDirectory(backup);
                if (Directory.Exists(retained)) throw new IOException("Ya hay un perfil conservado; revisa el respaldo.");
                Paths.MoveDirectory(profile, retained); receipt.RetainedProfile = retained;
            } else if (Directory.Exists(stage)) {
                Directory.CreateDirectory(backup);
                Paths.MoveDirectory(stage, retained); receipt.RetainedProfile = retained;
            }
            receipt.State = "restored"; Save(receipt);
            Directory.CreateDirectory(backup);
            var journal = Path.Combine(receipt.Target, ReceiptName);
            File.Move(Paths.Long(journal), Paths.Long(Path.Combine(backup, "restored-install.json")));
            return receipt;
        }
    }
}
