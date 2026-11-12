using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using SASetup;

namespace SAPortable {
    // Install for a game that is not run by Steam (PC 1.0, a copy outside a
    // Steam library). The owner's game folder is only read. The prepared
    // profile goes to its own folder with a shortcut; nothing is replaced.
    // Saves of an earlier SA Delirium profile of the same game, kept in its
    // backup by Restore. A reinstall copies them; the backup stays as it is.
    public static class Saves {
        public static bool IsSave(string name) { return Regex.IsMatch(name, @"^(GTASAsf[1-8]\.b|gta_sa\.set)$", RegexOptions.IgnoreCase); }
        public static string Latest(string root) {
            var backups = Path.Combine(root, "SA-Delirium-backups");
            if (!Directory.Exists(backups)) return null;
            foreach (var folder in Directory.GetDirectories(backups).Where(d => Regex.IsMatch(Path.GetFileName(d), @"^\d{8}-\d{6}-[a-f0-9]{8}$")).OrderByDescending(d => Path.GetFileName(d), StringComparer.Ordinal)) {
                var userfiles = Path.Combine(folder, "retained-profile", "userfiles");
                if (Directory.Exists(userfiles) && Directory.GetFiles(userfiles).Any(f => IsSave(Path.GetFileName(f)))) return userfiles;
            }
            return null;
        }
        // Copies into the prepared profile; returns the source folder or null.
        public static string Carry(string root, string prepared) {
            var source = Latest(root);
            if (source == null) return null;
            Paths.NoLinks(source);
            var target = Path.Combine(prepared, "userfiles"); Directory.CreateDirectory(target);
            foreach (var file in Directory.GetFiles(source).Where(f => IsSave(Path.GetFileName(f)))) {
                Paths.NoLinks(file);
                var copy = Path.Combine(target, Path.GetFileName(file));
                File.Copy(file, copy, true);
                if (Engine.Hash(copy) != Engine.Hash(file)) throw new IOException("No se pudo copiar una partida anterior.");
            }
            return source;
        }
    }
    public static class Standalone {
        public const string ShortcutName = "SA Delirium.lnk";
        public static string DefaultDestination {
            get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "SA-Delirium", "Juego"); }
        }
        public static string[] DefaultShortcutFolders {
            get { return new[] { Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), Environment.GetFolderPath(Environment.SpecialFolder.Programs) }; }
        }
        public static bool Installed(string destination) {
            return !String.IsNullOrWhiteSpace(destination) && File.Exists(Path.Combine(destination, Engine.ReceiptName));
        }
        static void Save(Receipt receipt) { Json.Write(Path.Combine(receipt.Target, Engine.ReceiptName), receipt); }
        internal static void CopyChecked(string source, string destination, string expected, long size, Func<bool> cancelled, Action<long> advance) {
            Paths.NoLinks(source); Directory.CreateDirectory(Path.GetDirectoryName(destination));
            using (var input = new FileStream(source, FileMode.Open, FileAccess.Read, FileShare.Read))
            using (var output = new FileStream(destination, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            using (var hash = SHA256.Create()) {
                if (input.Length != size) throw new IOException("El contenido preparado cambió: " + source);
                var buffer = new byte[1024 * 1024]; int count;
                while ((count = input.Read(buffer, 0, buffer.Length)) != 0) {
                    if (cancelled()) throw new OperationCanceledException("Instalación cancelada; tu juego no se ha modificado.");
                    output.Write(buffer, 0, count); hash.TransformBlock(buffer, 0, count, null, 0); advance(count);
                }
                hash.TransformFinalBlock(new byte[0], 0, 0); output.Flush(true);
                if (BitConverter.ToString(hash.Hash).Replace("-", "").ToLowerInvariant() != expected)
                    throw new IOException("El contenido preparado no coincide: " + source);
            }
        }
        public static string CheckDestination(string game, string destination, int savePathLimit) {
            game = Paths.Full(game);
            destination = Paths.Full(destination);
            Paths.NoLinks(destination);
            if (File.Exists(destination)) throw new IOException("El destino debe ser una carpeta.");
            if (destination.Equals(game, StringComparison.OrdinalIgnoreCase) || Paths.Under(destination, game) || Paths.Under(game, destination))
                throw new IOException("La carpeta del mod debe estar fuera de la carpeta del juego.");
            if (Installed(destination)) throw new IOException("Ya hay una instalación de SA Delirium en " + destination + ". Usa Restaurar primero.");
            var profile = Path.Combine(destination, Engine.ProfileDirectory);
            if (Directory.Exists(profile)) throw new IOException("La carpeta de destino ya contiene una copia del juego: " + profile);
            if (savePathLimit > 0 && Encoding.Default.GetByteCount(Path.Combine(profile, "userfiles", "GTASAsf8.b")) >= savePathLimit)
                throw new IOException("La ruta de destino es demasiado larga para cargar partidas en SA 1.0. Elige una carpeta con una ruta más corta.");
            return destination;
        }
        public static Receipt Install(ProfileIndex index, string prepared, string game, string destination, string[] shortcutFolders,
                                      Action<long, long, string> progress, Func<bool> cancelled) {
            Engine.NoRunningGame();
            prepared = Paths.Full(prepared); game = Paths.Full(game);
            Paths.NoLinks(prepared); Paths.NoLinks(game);
            destination = CheckDestination(game, destination, index.native_save_path_limit_bytes);
            var profile = Path.Combine(destination, Engine.ProfileDirectory);
            Directory.CreateDirectory(destination); Paths.NoLinks(destination);
            var drive = new DriveInfo(Path.GetPathRoot(destination));
            if (drive.AvailableFreeSpace < checked(index.total_bytes + 1024L * 1024 * 1024))
                throw new IOException("No hay espacio suficiente. Se necesitan " + (index.total_bytes / (1024.0 * 1024 * 1024)).ToString("F1") + " GiB más 1 GiB de margen.");
            string id = DateTime.UtcNow.ToString("yyyyMMdd-HHmmss") + "-" + Guid.NewGuid().ToString("N").Substring(0, 8);
            var receipt = new Receipt { Mode = "standalone", State = "copying", Source = game, Target = destination, Profile = index.profile,
                StartedUtc = DateTime.UtcNow.ToString("o"), BackupDirectory = "SA-Delirium-backups/" + id, StageDirectory = ".SA-Setup-" + id, Shortcuts = new string[0] };
            var stage = Paths.Inside(destination, receipt.StageDirectory);
            Directory.CreateDirectory(stage); Save(receipt);
            try {
                long completed = 0;
                foreach (var f in index.files) {
                    CopyChecked(Paths.Inside(prepared, f.path), Paths.Inside(stage, f.path), f.sha256, f.bytes, cancelled,
                        delegate (long count) { completed += count; progress(completed, index.total_bytes, f.path); });
                }
                var saves = Path.Combine(prepared, "userfiles");
                if (Directory.Exists(saves))
                    foreach (var file in Directory.GetFiles(saves).Where(x => Saves.IsSave(Path.GetFileName(x)))) {
                        var size = new FileInfo(file).Length;
                        CopyChecked(file, Paths.Inside(stage, "userfiles/" + Path.GetFileName(file)), Engine.Hash(file), size, cancelled, delegate { });
                    }
                Json.Write(Path.Combine(stage, "SA-Development-profile.json"), new {
                    profile = index.profile, files = index.files, copied_saves = false, mode = "standalone", game_folder_only_read = game,
                    omitted_gameplay = new[] { "GTA V HUD / minimap / weapon wheel" }, install_utc = DateTime.UtcNow.ToString("o") });
                if (cancelled()) throw new OperationCanceledException();
                Paths.MoveDirectory(stage, profile);
                receipt.ProfileMoved = true; receipt.State = "prepared"; Save(receipt);
                var exe = Path.Combine(profile, "gta_sa.exe");
                if (!File.Exists(exe)) throw new IOException("Falta el ejecutable preparado.");
                var created = new System.Collections.Generic.List<string>();
                foreach (var folder in shortcutFolders ?? new string[0]) {
                    if (String.IsNullOrEmpty(folder) || !Directory.Exists(folder)) continue;
                    var link = Path.Combine(folder, ShortcutName);
                    // Never overwrite a shortcut that is not ours.
                    if (File.Exists(link) && !String.Equals(ShortcutTarget(link), exe, StringComparison.OrdinalIgnoreCase)) continue;
                    CreateShortcut(link, exe, profile); created.Add(link);
                    receipt.Shortcuts = created.ToArray(); Save(receipt);
                }
                receipt.State = "installed"; Save(receipt);
                return receipt;
            } catch {
                receipt.State = "interrupted";
                try { Save(receipt); Restore(destination); } catch { /* The journal stays for a later Restore. */ }
                throw;
            }
        }
        public static Receipt Restore(string destination) {
            Engine.NoRunningGame();
            destination = Paths.Full(destination); Paths.NoLinks(destination);
            var receipt = Json.Read<Receipt>(Path.Combine(destination, Engine.ReceiptName));
            if (receipt.Schema != 1 || receipt.Mode != "standalone" || !destination.Equals(Paths.Full(receipt.Target), StringComparison.OrdinalIgnoreCase) ||
                !Regex.IsMatch(receipt.BackupDirectory ?? "", @"^SA-Delirium-backups/\d{8}-\d{6}-[a-f0-9]{8}$") ||
                !Regex.IsMatch(receipt.StageDirectory ?? "", @"^\.SA-Setup-\d{8}-\d{6}-[a-f0-9]{8}$"))
                throw new IOException("Diario de instalación no válido. No se modifica nada.");
            var profile = Paths.Inside(destination, Engine.ProfileDirectory);
            var stage = Paths.Inside(destination, receipt.StageDirectory);
            var backup = Paths.Inside(destination, receipt.BackupDirectory);
            var retained = Paths.Inside(destination, receipt.BackupDirectory + "/retained-profile");
            var exe = Path.Combine(profile, "gta_sa.exe");
            // Only shortcuts this install created and that still point here.
            foreach (var link in receipt.Shortcuts ?? new string[0])
                if (Path.GetFileName(link).Equals(ShortcutName, StringComparison.OrdinalIgnoreCase) && File.Exists(link) &&
                    String.Equals(ShortcutTarget(link), exe, StringComparison.OrdinalIgnoreCase))
                    File.Delete(link);
            Directory.CreateDirectory(backup);
            if (Directory.Exists(retained) && (Directory.Exists(profile) || Directory.Exists(stage)))
                throw new IOException("Ya hay un perfil conservado; revisa el respaldo.");
            // The profile and its saves are kept in the backup, never deleted.
            if (Directory.Exists(profile)) { Paths.MoveDirectory(profile, retained); receipt.RetainedProfile = retained; }
            else if (Directory.Exists(stage)) { Paths.MoveDirectory(stage, retained); receipt.RetainedProfile = retained; }
            receipt.State = "restored"; Save(receipt);
            File.Move(Paths.Long(Path.Combine(destination, Engine.ReceiptName)), Paths.Long(Path.Combine(backup, "restored-install.json")));
            return receipt;
        }
        static object Shell() { return Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell", true)); }
        static object Link(object shell, string path) { return shell.GetType().InvokeMember("CreateShortcut", BindingFlags.InvokeMethod, null, shell, new object[] { path }); }
        static void CreateShortcut(string path, string target, string directory) {
            var shell = Shell(); object link = null;
            try {
                link = Link(shell, path); var type = link.GetType();
                type.InvokeMember("TargetPath", BindingFlags.SetProperty, null, link, new object[] { target });
                type.InvokeMember("WorkingDirectory", BindingFlags.SetProperty, null, link, new object[] { directory });
                type.InvokeMember("Description", BindingFlags.SetProperty, null, link, new object[] { "GTA San Andreas con SA Delirium" });
                type.InvokeMember("Save", BindingFlags.InvokeMethod, null, link, null);
            } finally { if (link != null) Marshal.FinalReleaseComObject(link); Marshal.FinalReleaseComObject(shell); }
        }
        static string ShortcutTarget(string path) {
            var shell = Shell(); object link = null;
            try {
                link = Link(shell, path);
                return (string)link.GetType().InvokeMember("TargetPath", BindingFlags.GetProperty, null, link, null);
            } finally { if (link != null) Marshal.FinalReleaseComObject(link); Marshal.FinalReleaseComObject(shell); }
        }
    }
}
