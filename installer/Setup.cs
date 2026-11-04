using System;
using System.ComponentModel;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Windows.Forms;

namespace SASetup {
    public static class Program {
        public const string Version = "0.2.0-dev-import.1";
        public static byte[] Resource(string name) {
            using (var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream(name)) {
                if (stream == null) throw new IOException("Falta un recurso del instalador: " + name);
                using (var output = new MemoryStream()) { stream.CopyTo(output); return output.ToArray(); }
            }
        }
        public static Engine CreateEngine() {
            return new Engine(Json.Serializer().Deserialize<ProfileIndex>(Encoding.UTF8.GetString(Resource("profile-index.json"))), Resource("SteamLauncher.exe"),
                new Dictionary<string, byte[]>(StringComparer.OrdinalIgnoreCase) {
                    { "scripts/DeliriumSA.asi", Resource("core.DeliriumSA.asi") },
                    { "delirium_physics.dll", Resource("core.delirium_physics.dll") },
                    { "DeliriumSA.ini", Resource("core.DeliriumSA.ini") }
                });
        }
        private static string Option(string[] args, string name) {
            int pos = Array.IndexOf(args, name);
            if (pos < 0 || pos + 1 == args.Length) throw new ArgumentException("Falta " + name);
            return args[pos + 1];
        }
        [STAThread]
        public static int Main(string[] args) {
            Paths.EnableLongPaths();
            bool cli = args.Length != 0 && args[0] != "--render";
            try {
                var engine = CreateEngine();
                if (cli) {
                    var report = Option(args, "--report");
                    object result;
                    switch (args[0]) {
                        case "--detect": result = new { version = Version, steam = Discovery.SteamGames(), sources = Discovery.Sources(engine.Index),
                            source_required = true, profile = engine.Index.profile, bytes = engine.Index.total_bytes }; break;
                        case "--plan": result = engine.MakePlan(Option(args, "--source"), Option(args, "--target"), args.Contains("--copy-saves")); break;
                        case "--install":
                            engine.CheckSteamTarget(Option(args, "--target"));
                            result = engine.Install(engine.MakePlan(Option(args, "--source"), Option(args, "--target"), args.Contains("--copy-saves"))); break;
                        case "--restore": result = engine.Restore(Option(args, "--target")); break;
                        default: throw new ArgumentException("Modo no reconocido.");
                    }
                    Json.Write(report, result); return 0;
                }
                Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
                using (var form = new SetupForm(engine)) {
                    if (args.Length != 0) {
                        form.CreateControl();
                        using (var bitmap = new Bitmap(form.Width, form.Height)) {
                            form.DrawToBitmap(bitmap, new Rectangle(0, 0, form.Width, form.Height));
                            // Hidden forms make descendant Visible false. Render
                            // each control explicitly without showing a window.
                            int dx = (form.Width - form.ClientSize.Width) / 2;
                            int dy = form.Height - form.ClientSize.Height - dx;
                            foreach (Control control in form.Controls) {
                                var handle = control.Handle;
                                control.DrawToBitmap(bitmap, new Rectangle(control.Left + dx, control.Top + dy, control.Width, control.Height));
                            }
                            bitmap.Save(Option(args, "--render"));
                        }
                    } else Application.Run(form);
                }
                return 0;
            } catch (Exception error) {
                if (cli) {
                    try { Json.Write(Option(args, "--report"), new { error = error.Message, type = error.GetType().Name }); } catch { }
                } else MessageBox.Show(error.Message, "SA Delirium", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return 1;
            }
        }
    }
    public class SetupForm : Form {
        private readonly Engine engine;
        private readonly TextBox source = new TextBox(), target = new TextBox();
        private readonly CheckBox saves = new CheckBox();
        private readonly Button check = new Button(), install = new Button(), restore = new Button(), cancel = new Button();
        private readonly ProgressBar progress = new ProgressBar();
        private readonly Label status = new Label(), summary = new Label();
        private readonly BackgroundWorker worker = new BackgroundWorker();
        private Plan plan;
        private bool busy;
        private DateTime lastProgress = DateTime.MinValue;
        public SetupForm(Engine value) {
            engine = value; Text = "SA Delirium · Instalar en Steam";
            ClientSize = new Size(740, 570); MinimumSize = Size; MaximumSize = Size;
            StartPosition = FormStartPosition.CenterScreen; Font = new Font("Segoe UI", 10);
            BackColor = Color.FromArgb(246, 248, 251); FormBorderStyle = FormBorderStyle.FixedDialog; MaximizeBox = false;
            var title = new Label { Text = "Tu versión de desarrollo, desde Steam", Font = new Font("Segoe UI", 18, FontStyle.Bold),
                Location = new Point(28, 25), Size = new Size(685, 38) }; Controls.Add(title);
            var intro = new Label { Text = "Conserva los mods y las físicas. Retira el minimapa y la rueda de armas de GTA V.",
                Location = new Point(30, 74), Size = new Size(680, 43) }; Controls.Add(intro);
            AddPath("1  Copia de desarrollo que ya funciona", source, 126);
            AddPath("2  San Andreas clásico instalado en Steam", target, 222);
            saves.Text = "Copiar mis partidas y ajustes a un perfil independiente";
            saves.Checked = true; saves.Location = new Point(31, 319); saves.Size = new Size(680, 29); Controls.Add(saves);
            summary.Location = new Point(31, 360); summary.Size = new Size(675, 67);
            summary.ForeColor = Color.FromArgb(65, 77, 96);
            summary.Text = "Necesitas tu instalación de Steam y una copia local de desarrollo compatible.\nEl asistente no descarga el juego ni los paquetes de texturas."; Controls.Add(summary);
            progress.Location = new Point(31, 437); progress.Size = new Size(677, 16); Controls.Add(progress);
            status.Location = new Point(31, 464); status.Size = new Size(677, 40); status.Text = "Detectando rutas…"; Controls.Add(status);
            check.Text = "Comprobar"; check.SetBounds(31, 517, 125, 34); check.Click += delegate { CheckPlan(); }; Controls.Add(check);
            restore.Text = "Restaurar Steam"; restore.SetBounds(165, 517, 150, 34); restore.Click += delegate { BeginRestore(); }; Controls.Add(restore);
            cancel.Text = "Cancelar"; cancel.SetBounds(440, 517, 115, 34); cancel.Click += delegate { if (busy) worker.CancelAsync(); else Close(); }; Controls.Add(cancel);
            install.Text = "Instalar"; install.SetBounds(565, 517, 143, 34); install.Enabled = false;
            install.BackColor = Color.FromArgb(32, 91, 210); install.ForeColor = Color.White; install.FlatStyle = FlatStyle.Flat;
            install.Click += delegate { BeginInstall(); }; Controls.Add(install);
            foreach (var field in new[] { source, target }) field.TextChanged += delegate { plan = null; install.Enabled = false; UpdateRestore(); };
            saves.CheckedChanged += delegate { plan = null; install.Enabled = false; };
            worker.WorkerSupportsCancellation = true;
            FormClosing += delegate(object sender, FormClosingEventArgs ev) { if (busy) { worker.CancelAsync(); ev.Cancel = true; } };
            worker.RunWorkerCompleted += delegate(object sender, RunWorkerCompletedEventArgs ev) {
                SetBusy(false); plan = null; install.Enabled = false; UpdateRestore();
                if (ev.Error != null) { status.Text = ev.Error.Message; progress.Value = 0; return; }
                var receipt = (Receipt)ev.Result;
                progress.Value = 1000;
                status.Text = receipt.State == "installed" ? "Instalado. Abre San Andreas con Jugar en Steam." : "Steam restaurado. El perfil y sus partidas se conservan en el respaldo.";
                summary.Text = receipt.State == "installed" ? "El ejecutable original está respaldado. Tus carpetas de desarrollo y partidas originales se han conservado." : receipt.RetainedProfile;
            };
            Detect();
        }
        private void AddPath(string label, TextBox field, int y) {
            Controls.Add(new Label { Text = label, Font = new Font(Font, FontStyle.Bold), Location = new Point(31, y), Size = new Size(680, 26) });
            field.SetBounds(31, y + 33, 553, 31); Controls.Add(field);
            var browse = new Button { Text = "Examinar…" }; browse.SetBounds(594, y + 31, 114, 33);
            browse.Click += delegate {
                using (var dialog = new FolderBrowserDialog { Description = label, SelectedPath = field.Text, ShowNewFolderButton = false })
                    if (dialog.ShowDialog(this) == DialogResult.OK) field.Text = dialog.SelectedPath;
            }; Controls.Add(browse);
        }
        private void Detect() {
            var games = Discovery.SteamGames(); var sources = Discovery.Sources(engine.Index);
            if (games.Count != 0) target.Text = games[0];
            if (sources.Count != 0) source.Text = sources[0];
            UpdateRestore();
            status.Text = games.Count != 0 && sources.Count != 0 ? "Rutas encontradas. Pulsa Comprobar para revisar la instalación." : "Selecciona con Examinar las carpetas que faltan.";
        }
        private void UpdateRestore() { restore.Enabled = !busy && File.Exists(Path.Combine(target.Text, Engine.ReceiptName)); }
        private void CheckPlan() {
            try {
                engine.CheckSteamTarget(target.Text);
                plan = engine.MakePlan(source.Text, target.Text, saves.Checked);
                if (plan.Missing.Length != 0) throw new IOException("Faltan " + plan.Missing.Length + " archivos. Primero: " + plan.Missing[0]);
                summary.Text = String.Format("Se importarán {0:N0} archivos ({1:F1} GiB), comprobados por SHA-256.\nSe conservarán los mods, las prioridades y sus ajustes. Steam tendrá copia de seguridad.", plan.FileCount, plan.Bytes / (1024.0 * 1024 * 1024));
                status.Text = "Todo listo. Elige Instalar para empezar."; install.Enabled = true;
            } catch (Exception error) { status.Text = error.Message; install.Enabled = false; plan = null; }
        }
        private void SetBusy(bool value) {
            busy = value;
            foreach (Control control in Controls) if (control is Button || control is TextBox || control is CheckBox) control.Enabled = !value;
            cancel.Enabled = true; cancel.Text = value ? "Cancelar copia" : "Cerrar";
        }
        private void BeginInstall() {
            if (plan == null) return;
            var chosen = plan; SetBusy(true); status.Text = "Copiando y verificando los archivos…";
            engine.Cancelled = delegate { return worker.CancellationPending; };
            engine.Progress = delegate(long current, long total, string file) {
                var now = DateTime.UtcNow; if ((now - lastProgress).TotalMilliseconds < 150) return; lastProgress = now;
                BeginInvoke((Action)delegate { progress.Value = (int)Math.Min(1000, 1000.0 * current / Math.Max(1, total)); status.Text = "Copiando: " + Path.GetFileName(file); });
            };
            DoWorkEventHandler run = null;
            run = delegate(object sender, DoWorkEventArgs ev) { worker.DoWork -= run; ev.Result = engine.Install(chosen); };
            worker.DoWork += run; worker.RunWorkerAsync();
        }
        private void BeginRestore() {
            var chosen = target.Text; SetBusy(true); status.Text = "Restaurando el punto de entrada de Steam…";
            DoWorkEventHandler run = null;
            run = delegate(object sender, DoWorkEventArgs ev) { worker.DoWork -= run; ev.Result = engine.Restore(chosen); };
            worker.DoWork += run; worker.RunWorkerAsync();
        }
    }
}
