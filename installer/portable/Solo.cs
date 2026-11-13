using System;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using SASetup;

namespace SAPortable {
    public static class Solo {
        internal const string Resources = "DeliriumSA-local-v3";
        internal const string Settings = "DeliriumSA.ini";
        internal const string Adapter = "scripts/DeliriumSA.asi";
        internal const string Loader = "winmm.dll";
        const string UltimateLoader = "f0e76c41b6d96270b33dd8c0e27822a5efb631c839ec9a6671ab449f2fca28a1";
        const string SilentLoader = "c33e1bafafd441b42eaa6d322393b9dd700f8a9c13fb2e7780daf49707a46353";
        const string OriginalVorbis = "a08923479000cec366967fb8259e0920b7aa18859722c7dda1415726bed4774f";
        static readonly string[] Core = { Settings, Adapter, "scripts/sa_delirium_bootstrap.dll",
            "scripts/sa_delirium_bootstrap_avx2.dll" };

        public static bool NativeCompatible(EditionReport edition) {
            if (edition == null || !edition.accepted) return false;
            // These local executables have the same layout and code. Only the LAA bit differs.
            return edition.sha256 == "f01a00ce950fa40ca1ed59df0e789848c6edcf6405456274965885d0929343ac" ||
                   edition.sha256 == "a559aa772fd136379155efa71f00c47aad34bbfeae6196b0fe1047d0645cbd26";
        }

        static string HashIfPresent(string game, string file) {
            var path = Paths.Inside(game, file);
            if (!File.Exists(path)) return null;
            Paths.NoLinks(path);
            return Engine.Hash(path);
        }

        // Solo Delirium is additive: an unknown loader already installed by the
        // owner is left untouched. A hash mismatch alone must not block the mod.
        // This does NOT certify that the third-party DLL actually loads ASI files.
        internal static bool NeedsLoader(string game, Action<string> warning = null) {
            warning = warning ?? delegate {};
            var vorbis = HashIfPresent(game, Originals.VorbisFile);
            if (vorbis == null)
                throw new IOException("Falta vorbisFile.dll en la carpeta del juego. Verifica los archivos de San Andreas o selecciona la carpeta principal del juego.");
            if (vorbis == UltimateLoader) return false;
            if (vorbis == SilentLoader) {
                if (HashIfPresent(game, "vorbisHooked.dll") == OriginalVorbis) return false;
                throw new IOException("Silent ASI Loader necesita la DLL original del juego en vorbisHooked.dll. Verifica la instalación del cargador o usa la instalación completa con una copia verificada de San Andreas.");
            }
            foreach (var name in new[] { "winmm.dll", "dinput8.dll", "d3d9.dll" })
                if (HashIfPresent(game, name) == UltimateLoader) return false;
            if (vorbis != OriginalVorbis) {
                warning("Se conservará vorbisFile.dll, que no coincide con un cargador ASI verificado. Si Delirium no arranca, prueba una copia limpia o la instalación completa.");
                return false;
            }
            foreach (var name in new[] { Loader, "dinput8.dll" }) {
                var path = Paths.Inside(game, name);
                if (!File.Exists(path)) continue;
                Paths.NoLinks(path);
                if (new FileInfo(path).Length == 0)
                    throw new IOException(name + " está vacío. Repara este cargador antes de instalar Delirium.");
                warning("Se conservará el posible cargador ASI existente (" + name + "). No se instalará otro para evitar conflictos.");
                return false;
            }
            return true;
        }

        internal static Recipe[] Recipes(Manifest manifest, bool needsLoader) {
            var selected = manifest.files.Where(f => Core.Contains(f.path, StringComparer.OrdinalIgnoreCase) &&
                f.kind == "payload").ToList();
            if (!Core.Take(3).All(path => selected.Any(f => f.path.Equals(path, StringComparison.OrdinalIgnoreCase))))
                throw new IOException("Faltan archivos esenciales de Delirium en este paquete.");
            if (needsLoader) {
                var proxy = manifest.files.SingleOrDefault(f => f.path == "vorbisFile.dll" && f.kind == "payload" && f.sha256 == UltimateLoader);
                if (proxy == null) throw new IOException("Falta el cargador ASI verificado en el instalador.");
                selected.Add(new Recipe { path=Loader, source=proxy.source, bytes=proxy.bytes, sha256=proxy.sha256,
                    kind=proxy.kind, components=proxy.components });
            }
            return selected.ToArray();
        }

        internal static void CheckLocations(string game, Recipe[] files) {
            Paths.NoLinks(game);
            var resources = Paths.Inside(game, Resources);
            if (File.Exists(resources) || Directory.Exists(resources))
                throw new IOException("Esta carpeta ya tiene recursos de Delirium. Usa Restaurar en su instalador.");
            foreach (var f in files) {
                var path = Paths.Inside(game, f.path);
                if (File.Exists(path) || Directory.Exists(path))
                    throw new IOException("Hay un archivo que este modo conservará: " + f.path + ". Usa una copia aparte o restaura su instalación anterior.");
            }
        }

        static bool KnownPath(string path) {
            return Core.Contains(path, StringComparer.OrdinalIgnoreCase) ||
                   path.Equals(Loader, StringComparison.OrdinalIgnoreCase) ||
                   path.StartsWith(Resources + "/", StringComparison.OrdinalIgnoreCase);
        }

        public static Receipt Install(ProfileIndex index, string prepared, string game, Action<long,long,string> progress, Func<bool> cancelled) {
            Engine.NoRunningGame();
            game = Paths.Full(game);
            var journal = Paths.Inside(game, Engine.ReceiptName);
            if (File.Exists(journal)) throw new IOException("Usa Restaurar antes de volver a instalar Delirium.");
            if (index.files.Any(f => !KnownPath(f.path))) throw new IOException("El paquete Solo Delirium contiene un archivo ajeno.");
            var id = DateTime.UtcNow.ToString("yyyyMMdd-HHmmss") + "-" + Guid.NewGuid().ToString("N").Substring(0,8);
            var receipt = new Receipt { Mode="solo", State="copying", Target=game, Source=game, Profile=index.profile,
                StartedUtc=DateTime.UtcNow.ToString("o"), BackupDirectory="SA-Delirium-backups/" + id,
                StageDirectory=".SA-Solo-" + id, OwnedFiles=index.files, Shortcuts=new string[0] };
            var stage = Paths.Inside(game, receipt.StageDirectory);
            var resources = Paths.Inside(game, Resources);
            if (File.Exists(resources) || Directory.Exists(resources)) throw new IOException("Ya existen recursos de Delirium.");
            foreach (var file in index.files) {
                var target = Paths.Inside(game, file.path);
                if (File.Exists(target) || Directory.Exists(target))
                    throw new IOException("Delirium necesita una ruta ya ocupada: " + file.path);
            }
            Directory.CreateDirectory(stage);
            long completed = 0;
            // Stage every file before touching active game paths. Recovery
            // begins when the journal exists and works from the staged files.
            try {
                foreach (var file in index.files)
                    Standalone.CopyChecked(Paths.Inside(prepared, file.path), Paths.Inside(stage, file.path),
                        file.sha256, file.bytes, cancelled, delegate (long count) {
                            completed += count;
                            progress(completed, index.total_bytes, file.path);
                        });
                if (cancelled()) throw new OperationCanceledException();
                Json.Write(journal, receipt);
                Paths.MoveDirectory(Paths.Inside(stage, Resources), resources);
                foreach (var file in index.files.Where(f => !f.path.StartsWith(Resources + "/", StringComparison.OrdinalIgnoreCase))
                                                .OrderBy(f => f.path.Equals(Adapter, StringComparison.OrdinalIgnoreCase))) {
                    var target = Paths.Inside(game, file.path);
                    Directory.CreateDirectory(Path.GetDirectoryName(target));
                    File.Move(Paths.Inside(stage, file.path), target);
                }
                receipt.State = "installed";
                Json.Write(journal, receipt);
                return receipt;
            } catch {
                if (File.Exists(journal)) {
                    try { Restore(game); } catch (IOException) { }
                }
                throw;
            }
        }

        public static Receipt Restore(string game) {
            Engine.NoRunningGame();
            game = Paths.Full(game);
            var journal = Paths.Inside(game, Engine.ReceiptName);
            var receipt = Json.Read<Receipt>(journal);
            if (receipt.Schema != 1 || receipt.Mode != "solo" ||
                !String.Equals(receipt.Target, game, StringComparison.OrdinalIgnoreCase) ||
                !Regex.IsMatch(receipt.StageDirectory ?? "", @"^\.SA-Solo-\d{8}-\d{6}-[a-f0-9]{8}$") ||
                !Regex.IsMatch(receipt.BackupDirectory ?? "", @"^SA-Delirium-backups/\d{8}-\d{6}-[a-f0-9]{8}$") ||
                receipt.OwnedFiles == null || receipt.OwnedFiles.Any(f => !KnownPath(f.path)))
                throw new IOException("El registro de Solo Delirium no es válido.");
            var stage = Paths.Inside(game, receipt.StageDirectory);
            var backup = Paths.Inside(game, receipt.BackupDirectory);
            var activeFiles = receipt.OwnedFiles.Where(f => !f.path.StartsWith(Resources + "/", StringComparison.OrdinalIgnoreCase))
                .Where(f => !File.Exists(Paths.Inside(stage, f.path)) && File.Exists(Paths.Inside(game, f.path)) &&
                            !File.Exists(Paths.Inside(backup, f.path)))
                .OrderByDescending(f => f.path.Equals(Adapter, StringComparison.OrdinalIgnoreCase)).ToArray();
            foreach (var file in activeFiles) {
                if (!file.path.Equals(Settings, StringComparison.OrdinalIgnoreCase) &&
                    Engine.Hash(Paths.Inside(game, file.path)) != file.sha256)
                    throw new IOException("Se ha conservado un archivo que cambió después de instalar: " + file.path);
            }
            foreach (var file in activeFiles) {
                var active = Paths.Inside(game, file.path);
                var saved = Paths.Inside(backup, file.path);
                Directory.CreateDirectory(Path.GetDirectoryName(saved));
                File.Move(active, saved);
            }
            var cache = Paths.Inside(game, Resources);
            if (Directory.Exists(cache) && !Directory.Exists(Paths.Inside(stage, Resources)) &&
                !Directory.Exists(Paths.Inside(backup, Resources))) {
                Directory.CreateDirectory(backup);
                Paths.MoveDirectory(cache, Paths.Inside(backup, Resources));
            }
            receipt.State = "restored";
            Directory.CreateDirectory(backup);
            File.Move(journal, Paths.Inside(backup, Engine.ReceiptName));
            return receipt;
        }
    }
}
