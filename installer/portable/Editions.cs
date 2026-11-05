using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using SASetup;

namespace SAPortable {
    // What matters is the game, not the store: a folder is accepted when its
    // executable is one of the verified originals of the manifest, and later
    // every other file is matched one by one (Composer.ChooseOriginal).
    // This table only gives the user a name for what was found.
    public sealed class EditionReport {
        public string folder, edition, kind, executable, md5, sha256, message;
        // "steam": installed inside a Steam library, Jugar in Steam opens it.
        // "standalone": copied to its own folder, the game folder is only read.
        public string mode;
        public long bytes;
        public bool accepted;
    }
    public static class Editions {
        sealed class Known { public string Name, Kind; public Known(string name, string kind) { Name = name; Kind = kind; } }
        // Executable MD5 values published by gtasa-open-downgrader (MIT),
        // downgrader/main.py VERSION_HASHES, release-candidate 2026-10-19.
        static readonly Dictionary<string, Known> ByMd5 = new Dictionary<string, Known>(StringComparer.Ordinal) {
            {"5bfd4dd83989a8264de4b8e771f237fd", new Known("Steam · NewSteam R2", "steam-r2")},
            {"d9cb35c898d3298ca904a63e10ee18d7", new Known("Steam · NewSteam R2 alemana", "steam-r2-german")},
            {"d9cb35c898d3298ca904a63e10ee18d9", new Known("Steam · NewSteam R2 alemana", "steam-r2-german")},
            {"170b3a9108687b26da2d8901c6948a18", new Known("PC 1.0 US (Hoodlum)", "pc-1.0-us")},
            {"2b5066bd4097ac2944ce6a9cf8fe5677", new Known("PC 1.0 US (Hoodlum + LAA)", "pc-1.0-us")},
            {"4e99d762f44b1d5e7652dfa7e73d6b6f", new Known("PC 1.0 US (ejecutable original del disco)", "pc-1.0-us")},
            {"667f799c4ba8c9e1054fccaea6d4259b", new Known("PC 1.0 US (Compact)", "pc-1.0-us")},
            {"6c6160da9b175b66cf9127c86be57bf7", new Known("PC 1.0 EU", "pc-other")},
            {"49dd417760484a18017805df46b308b8", new Known("PC 1.0 EU (alternativa)", "pc-other")},
            {"9f2d711dbf1fbbcda5ff9418a2cc1ef5", new Known("PC 1.01 US", "pc-other")},
            {"25405921d1c47747fd01fd0bfe0a05ae", new Known("PC 1.01 EU", "pc-other")},
        };
        static string Md5(string path) {
            using (var input = File.OpenRead(path)) using (var md5 = MD5.Create())
                return BitConverter.ToString(md5.ComputeHash(input)).Replace("-", "").ToLowerInvariant();
        }
        static Recipe ExecutableRecipe(Manifest manifest) {
            if (manifest == null || manifest.files == null) return null;
            return manifest.files.FirstOrDefault(f => f.path.Equals("gta_sa.exe", StringComparison.OrdinalIgnoreCase) && (f.kind == "steam" || f.kind == "steam_delta"));
        }
        // Executables that may sit as gta-sa.exe in a Steam library and be
        // replaced by the launcher; restore puts the same bytes back.
        public static HashSet<string> AcceptedExecutables(Manifest manifest) {
            var accepted = new HashSet<string>(StringComparer.Ordinal);
            if (manifest == null || manifest.profile == null) return accepted;
            accepted.Add(manifest.profile.stock_steam_executable_sha256);
            if (manifest.profile.accepted_steam_executable_sha256 != null)
                foreach (var h in manifest.profile.accepted_steam_executable_sha256) accepted.Add(h);
            var recipe = ExecutableRecipe(manifest);
            if (recipe != null)
                foreach (var c in Originals.Candidates(recipe))
                    if (c.source.Equals("gta-sa.exe", StringComparison.OrdinalIgnoreCase)) accepted.Add(c.source_sha256);
            return accepted;
        }
        // The installer core reads the Steam list from the profile, so the
        // list derived from the recipes is written there once at load time.
        public static void Complete(Manifest manifest) {
            if (manifest == null || manifest.profile == null) return;
            var all = AcceptedExecutables(manifest);
            all.Remove(manifest.profile.stock_steam_executable_sha256);
            manifest.profile.accepted_steam_executable_sha256 = all.Count == 0 ? null : all.OrderBy(h => h, StringComparer.Ordinal).ToArray();
        }
        static RecipeSource SourceFor(Manifest manifest, string name, string sha256) {
            var recipe = ExecutableRecipe(manifest);
            if (recipe == null) return null;
            return Originals.Candidates(recipe).FirstOrDefault(c => c.source.Equals(name, StringComparison.OrdinalIgnoreCase) && c.source_sha256 == sha256);
        }
        static bool SteamLibrary(string folder) {
            try { return Discovery.SteamGames().Contains(folder, StringComparer.OrdinalIgnoreCase); }
            catch (Exception) { return false; }
        }
        public static EditionReport Identify(string folder, Manifest manifest, string launcherSha256) {
            return Identify(folder, manifest, launcherSha256, SteamLibrary);
        }
        public static EditionReport Identify(string folder, Manifest manifest, string launcherSha256, Func<string, bool> steamLibrary) {
            var report = new EditionReport { folder = folder, edition = "desconocida", kind = "unknown" };
            if (String.IsNullOrWhiteSpace(folder) || !Directory.Exists(folder)) {
                report.kind = "missing"; report.message = "La carpeta seleccionada no existe."; return report;
            }
            folder = Paths.Full(folder); report.folder = folder;
            if (File.Exists(Path.Combine(folder, "Gameface", "Binaries", "Win64", "SanAndreas.exe"))) {
                report.edition = "The Definitive Edition"; report.kind = "definitive";
                report.message = "Esta carpeta es GTA San Andreas – The Definitive Edition. Es otro juego, hecho con Unreal Engine, y ninguno de estos mods funciona en él. Necesitas el San Andreas clásico, de Steam o de PC.";
                return report;
            }
            if (File.Exists(Path.Combine(folder, Engine.ReceiptName))) {
                report.edition = "SA Delirium ya instalado"; report.kind = "installed";
                report.message = "SA Delirium ya está instalado en esta carpeta, o una instalación anterior quedó a medias. Usa Restaurar antes de volver a instalar.";
                return report;
            }
            var names = new[] { "gta-sa.exe", "gta_sa.exe" }.Where(n => File.Exists(Path.Combine(folder, n))).ToArray();
            if (names.Length == 0) {
                report.kind = "missing";
                report.message = "No hay gta-sa.exe ni gta_sa.exe en esta carpeta. Elige la carpeta donde está instalado San Andreas.";
                return report;
            }
            // A folder may hold both names; the accepted one wins, Steam first.
            RecipeSource source = null;
            foreach (var name in names) {
                var path = Path.Combine(folder, name); Paths.NoLinks(path);
                var sha = Engine.Hash(path);
                if (!String.IsNullOrEmpty(launcherSha256) && sha == launcherSha256) {
                    report.executable = name; report.sha256 = sha; report.bytes = new FileInfo(path).Length;
                    report.edition = "SA Delirium ya instalado"; report.kind = "installed";
                    report.message = name + " ya es el lanzador de SA Delirium. Usa Restaurar antes de volver a instalar.";
                    return report;
                }
                var candidate = SourceFor(manifest, name, sha);
                bool steamTarget = name == "gta-sa.exe" && AcceptedExecutables(manifest).Contains(sha);
                if (report.executable == null || candidate != null || steamTarget) {
                    report.executable = name; report.sha256 = sha; report.bytes = new FileInfo(path).Length; report.md5 = Md5(path);
                    report.accepted = candidate != null || steamTarget; source = candidate;
                    if (report.accepted) break;
                }
            }
            Known known;
            if (ByMd5.TryGetValue(report.md5, out known)) { report.edition = known.Name; report.kind = known.Kind; }
            if (source != null && !String.IsNullOrEmpty(source.name)) report.edition = source.name;
            if (report.accepted) {
                bool steam = report.executable == "gta-sa.exe" && AcceptedExecutables(manifest).Contains(report.sha256) && steamLibrary(folder);
                report.mode = steam ? "steam" : "standalone";
                report.message = steam
                    ? "Detectado: " + report.edition + " en Steam. Compatible: Jugar en Steam abrirá el mod."
                    : "Detectado: " + report.edition + ". Compatible: el mod se instala en su propia carpeta y tu juego no se modifica.";
                return report;
            }
            switch (report.kind) {
                case "steam-r2":
                    // Known MD5 but not a verified SHA-256: never patch it.
                    report.message = "Detectado: " + report.edition + ", pero el ejecutable no coincide con la copia verificada. Usa Verificar integridad de los archivos en Steam y vuelve a intentarlo.";
                    break;
                case "steam-r2-german":
                    report.message = "Detectado: " + report.edition + ". Está reconocida, pero este instalador no incluye el parche para su ejecutable.";
                    break;
                case "pc-1.0-us":
                    report.message = "Detectado: GTA SA " + report.edition + ". Es la versión correcta del juego, pero este ejecutable concreto no tiene parche verificado. Funciona con el ejecutable 1.0 US Hoodlum o con la copia de Steam.";
                    break;
                case "pc-other":
                    report.message = "Detectado: GTA SA " + report.edition + ". Esta versión cambia el ejecutable y parte de los datos, y no está admitida. Funciona con PC 1.0 US o con la copia de Steam.";
                    break;
                default:
                    report.message = "Ejecutable no reconocido (" + report.executable + ", " + report.bytes + " bytes, SHA-256 " + report.sha256 + "). Puede ser una actualización de Steam, la versión del Rockstar Games Launcher o una copia modificada. No se ha tocado nada. Comunica este código para añadir soporte.";
                    break;
            }
            return report;
        }
    }
}
