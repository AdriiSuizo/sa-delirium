using System;
using System.ComponentModel;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Reflection;
using System.Diagnostics;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows.Forms;
using SASetup;

namespace SAPortable {
    public static class Program {
        public static string ReleaseVersion(Manifest manifest) { return manifest.version ?? "Local"; }
        public static byte[] Resource(string name) {
            using(var input=Assembly.GetExecutingAssembly().GetManifestResourceStream(name)) {
                if(input==null)throw new IOException("Falta un recurso del instalador: "+name);
                using(var output=new MemoryStream()){input.CopyTo(output);return output.ToArray();}
            }
        }
        public static Manifest ReadManifest() {
            var manifest=Json.Serializer().Deserialize<Manifest>(System.Text.Encoding.UTF8.GetString(Resource("portable-manifest.json")));
            Editions.Complete(manifest);return manifest;
        }
        public static string DefaultCache { get {return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"SA-Delirium","Setup");} }
        public static string Helpers(string cache) {
            var root=Paths.Inside(cache,"helpers-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(root);
            var manifest=ReadManifest();
            Stream archive=Assembly.GetExecutingAssembly().GetManifestResourceStream("portable-tools.zip");
            // The private multi-GB verification fixture uses a pinned sidecar.
            // A production build must embed its small tools/patches bundle.
            if(archive==null && manifest.lab_only)archive=new FileStream(Path.Combine(Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location),"portable-tools.zip"),FileMode.Open,FileAccess.Read,FileShare.Read);
            if(archive==null)throw new IOException("Falta el contenido del instalador.");
            using(archive) {
                using(var hash=System.Security.Cryptography.SHA256.Create()) {
                    var digest=BitConverter.ToString(hash.ComputeHash(archive)).Replace("-","").ToLowerInvariant();
                    if(digest!=manifest.tools_sha256)throw new IOException("El contenido del instalador ha cambiado.");
                }
                archive.Position=0;
                using(var zip=new ZipArchive(archive,ZipArchiveMode.Read)) {
                foreach(var entry in zip.Entries) {
                    if(entry.FullName.EndsWith("/"))continue;
                    if(((entry.ExternalAttributes>>16)&0xF000)==0xA000)throw new IOException("Enlace no permitido en el paquete.");
                    var output=Paths.Inside(root,entry.FullName);Directory.CreateDirectory(Path.GetDirectoryName(output));
                    using(var input=entry.Open())using(var destination=new FileStream(output,FileMode.CreateNew))input.CopyTo(destination);
                }
                }
            }
            return root;
        }
        static string Option(string[] args,string key,string fallback=null) {
            int i=Array.IndexOf(args,key);if(i<0){if(fallback!=null)return fallback;throw new ArgumentException("Falta "+key);}
            if(i+1>=args.Length)throw new ArgumentException("Falta el valor de "+key);return args[i+1];
        }
        // One Restore for both kinds of install, chosen by the journal.
        public static Receipt Restore(Manifest manifest,string target) {
            var journal=Path.Combine(target,Engine.ReceiptName);
            if(File.Exists(journal)) {
                var mode=Json.Read<Receipt>(journal).Mode;
                if(mode=="standalone")return Standalone.Restore(target);
                if(mode=="solo")return Solo.Restore(target);
            }
            return new Engine(manifest.profile,Resource("SteamLauncher.exe")).Restore(target);
        }
        public static EditionReport Identify(Manifest manifest,string folder) {
            return Editions.Identify(folder,manifest,Engine.Hash(Resource("SteamLauncher.exe")));
        }
        // Steam mode only for a Steam library folder (or a lab fixture of one);
        // any other verified copy of the game gets its own folder.
        public static bool SteamMode(Manifest manifest,EditionReport edition,bool lab,bool forceStandalone) {
            if(forceStandalone || !edition.accepted)return false;
            return edition.mode=="steam" || (lab && edition.executable=="gta-sa.exe" && Editions.AcceptedExecutables(manifest).Contains(edition.sha256));
        }
        public static Receipt Install(Manifest manifest,string steam,string iso,string cache,Action<long,long,string> progress,Func<bool> cancelled,bool unattended,bool lab,
                                      string destination=null,string[] shortcuts=null,bool forceStandalone=false,bool hdModels=false,Action<SetupPhase> phase=null,bool solo=false) {
            phase=phase??delegate{};
            phase(SetupPhase.Validation);
            Engine.NoRunningGame();
            if(manifest.lab_only && !lab)throw new IOException("Candidato local de validación; no es una distribución publicable.");
            var launcher=Resource("SteamLauncher.exe");var initial=new Engine(manifest.profile,launcher);
            var source=steam;
            var edition=Identify(manifest,source);
            if(!edition.accepted)throw new IOException(edition.message);
            steam=edition.folder;
            bool steamMode=!solo&&SteamMode(manifest,edition,lab,forceStandalone);
            if(steamMode && !lab)initial.CheckSteamTarget(steam);
            if(!steamMode&&!solo)destination=Standalone.CheckDestination(steam,destination??Standalone.DefaultDestination,manifest.profile.native_save_path_limit_bytes);
            using(var mutex=new Mutex(false,"Local\\SA-Delirium-Install-"+Engine.Hash(System.Text.Encoding.UTF8.GetBytes(Paths.Full(steam).ToLowerInvariant())).Substring(0,24))) {
                if(!mutex.WaitOne(0))throw new IOException("Ya hay otro instalador trabajando con esta copia de San Andreas.");
                try {
                    Recipe[] soloRecipes=null;
                    if(solo) {
                        if(!manifest.unified || !Solo.NativeCompatible(edition))
                            throw new IOException("Solo Delirium requiere el ejecutable nativo compatible de GTA SA 1.0 US. El perfil completo puede preparar otras copias verificadas aparte.");
                        soloRecipes=Solo.Recipes(manifest,Solo.NeedsLoader(steam, message => progress(0, 0, message)));
                        Solo.CheckLocations(steam,soloRecipes);
                    }
                    var helper=Helpers(cache);
                    var composer=new Composer(manifest,helper,cache){Progress=progress,Phase=phase,Cancelled=cancelled,Unattended=unattended,HdModels=hdModels};
                    string prepared;
                    if(solo) {
                        var soloIndex=composer.PrepareSolo(edition,iso,soloRecipes,out prepared);
                        phase(SetupPhase.Install);
                        return Solo.Install(soloIndex,prepared,steam,progress,cancelled);
                    }
                    var index=composer.Prepare(source,iso,out prepared);
                    phase(SetupPhase.Install);
                    var savesFrom=Saves.Carry(steamMode?steam:destination,prepared);
                    Receipt receipt;
                    if(!steamMode)receipt=Standalone.Install(index,prepared,steam,destination,shortcuts??Standalone.DefaultShortcutFolders,progress,cancelled);
                    else {
                        var engine=new Engine(index,launcher){Progress=progress,Cancelled=cancelled};
                        receipt=engine.Install(engine.MakePlan(prepared,steam,savesFrom!=null));
                    }
                    if(savesFrom!=null){receipt.SavesFrom=savesFrom;Json.Write(Path.Combine(receipt.Target,Engine.ReceiptName),receipt);}
                    return receipt;
                } finally {mutex.ReleaseMutex();}
            }
        }
        [STAThread] public static int Main(string[] args) {
            AppContext.SetSwitch("Switch.System.Net.DontEnableSchUseStrongCrypto",false);
            AppContext.SetSwitch("Switch.System.Net.DontEnableSystemDefaultTlsVersions",false);
            Paths.EnableLongPaths();bool cli=args.Length!=0 && args[0]!="--render";
            Manifest manifest = null;
            try {
                manifest=ReadManifest();
                if(cli) {
                    object result;var report=Option(args,"--report");var cache=Option(args,"--cache",DefaultCache);
                    switch(args[0]) {
                        case "--detect":result=new{version=ReleaseVersion(manifest),steam=Discovery.SteamGames(),editions=Discovery.SteamGames().Select(g=>Identify(manifest,g)).ToArray(),iso_required=true,development_source_required=false,omissions=manifest.omissions};break;
                        case "--soften":{var kept=new List<ProfileFile>();result=new{models=SoftModels.Write(Option(args,"--target"),kept,delegate{}),files=kept};break;}
                        case "--identify":result=Identify(manifest,Option(args,"--target"));break;
                        case "--verify-source": {
                            var edition = Identify(manifest, Option(args,"--target"));
                            var scan = edition.accepted ? Composer.ScanOriginals(manifest, edition, delegate { return false; }, delegate {}) : null;
                            bool compatible = scan != null && scan.Missing.Count == 0;
                            Json.Write(report, new { compatible, edition, verified_files = scan == null ? 0 : scan.Files.Count,
                                missing_or_changed = scan == null ? new string[0] : scan.Missing.ToArray() });
                            return compatible ? 0 : 1;
                        }
                        case "--install":result=Install(manifest,Option(args,"--target"),Option(args,"--iso"),cache,delegate{},delegate{return false;},args.Contains("--unattended"),args.Contains("--lab"),
                            Option(args,"--destination",Standalone.DefaultDestination),args.Contains("--no-shortcuts")?new string[0]:null,args.Contains("--standalone"),args.Contains("--hd-models"),solo:args.Contains("--solo"));break;
                        case "--prepare":
                            var composer=new Composer(manifest,Helpers(cache),cache){Unattended=args.Contains("--unattended"),HdModels=args.Contains("--hd-models")};string prepared;
                            var index=composer.Prepare(Option(args,"--target"),Option(args,"--iso"),out prepared);
                            result=new{status="prepared",prepared_root=prepared,index};break;
                        case "--restore":result=Restore(manifest,Option(args,"--target"));break;
                        default:throw new ArgumentException("Modo no reconocido.");
                    }
                    Json.Write(report,result);return 0;
                }
                Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
                using(var form=new SetupForm(manifest)) {
                    if(args.Length==0)Application.Run(form);
                    else {
                        form.CreateControl();
                        using(var bitmap=new Bitmap(form.Width,form.Height)) {
                            form.DrawToBitmap(bitmap,new Rectangle(0,0,form.Width,form.Height));
                            int dx=(form.Width-form.ClientSize.Width)/2,dy=form.Height-form.ClientSize.Height-dx;
                            form.DrawPreviewControls(bitmap, dx, dy);
                            bitmap.Save(Option(args,"--render"));
                        }
                    }
                }
                return 0;
            } catch(Exception error) {
                if(cli){try{Json.Write(Option(args,"--report"),new{status="failed",error=error.Message});}catch{}}
                else if (args.Length == 0) SetupForm.ShowStartupError(manifest, error);
                return 1;
            }
        }
    }
    public sealed class SetupForm : Form {
        static readonly Color Background = Color.FromArgb(20, 19, 28);
        static readonly Color Surface = Color.FromArgb(33, 30, 46);
        static readonly Color Field = Color.FromArgb(26, 23, 37);
        static readonly Color TextColor = Color.FromArgb(248, 246, 255);
        static readonly Color Muted = Color.FromArgb(192, 183, 209);
        static readonly Color Accent = Color.FromArgb(110, 57, 218);
        static readonly Color Warning = Color.FromArgb(255, 197, 145);
        static readonly Color Ready = Color.FromArgb(162, 228, 194);
        static readonly Color Active = Color.FromArgb(255, 221, 137);
        static readonly Color Failed = Color.FromArgb(255, 136, 153);
        const string Channel = "https://www.youtube.com/@AdriiSuizo";
        static readonly string[] Steps = {
            "Comprobar requisitos", "Preparar componentes", "Copiar archivos", "Importar recursos de GTA IV", "Instalar SA Delirium"
        };
        enum Outcome { Pending, Running, Failed, Cancelled, Complete }

        readonly Manifest manifest;
        readonly TextBox game = new TextBox(), iso = new TextBox(), destination = new TextBox();
        readonly Button install = new Button(), restore = new Button(), cancel = new Button(), diagnostic = new Button();
        readonly Label gameHint = new Label(), isoHint = new Label(), destinationHint = new Label();
        readonly Label status = new Label(), detail = new Label();
        readonly Label[] stepLabels = new Label[Steps.Length];
        readonly RadioButton originals = new RadioButton(), hd = new RadioButton();
        readonly RadioButton full = new RadioButton(), solo = new RadioButton();
        readonly Label packageSummary = new Label();
        readonly ProgressBar progress = new ProgressBar { Maximum = 1000, MarqueeAnimationSpeed = 25 };
        readonly BackgroundWorker worker = new BackgroundWorker { WorkerSupportsCancellation = true, WorkerReportsProgress = true };
        readonly BackgroundWorker verifier = new BackgroundWorker();
        readonly System.Windows.Forms.Timer verifyDelay = new System.Windows.Forms.Timer { Interval = 350 };
        readonly List<Control> inputControls = new List<Control>();
        Button destinationBrowse;
        EditionReport edition;
        string checkedFolder, pendingFolder, diagnosticText;
        bool busy, restoring, cancelling, finished;
        SetupPhase phase;
        Outcome outcome;

        public SetupForm(Manifest manifest) {
            this.manifest = manifest;
            Text = "SA Delirium · Asistente de instalación";
            Font = new Font("Segoe UI", 9.5f);
            AutoScaleMode = AutoScaleMode.Font;
            AutoScaleDimensions = new SizeF(7, 17);
            ClientSize = new Size(844, 725);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;
            BackColor = Background;
            ForeColor = TextColor;

            var header = new Panel { BackColor = Surface, Bounds = new Rectangle(0, 0, 844, 100) };
            Controls.Add(header);
            header.Controls.Add(new Panel { BackColor = Accent, Bounds = new Rectangle(0, 0, 844, 4) });
            LabelAt(header, new Label(), "SA DELIRIUM", 26, 16, 540, 22, 10, Muted, true);
            LabelAt(header, new Label(), "Asistente de instalación", 24, 38, 650, 45, 24, TextColor, true);
            var version = LabelAt(header, new Label(), Program.ReleaseVersion(manifest), 580, 19, 236, 26, 11, Muted, true);
            version.TextAlign = ContentAlignment.MiddleRight;

            var modes=new Panel {Bounds=new Rectangle(26,114,510,33)};
            Controls.Add(modes);
            full.Text="Instalación completa";full.Bounds=new Rectangle(0,0,224,30);full.Checked=true;
            solo.Text="Solo Delirium";solo.Bounds=new Rectangle(234,0,240,30);solo.Visible=manifest.unified;
            modes.Controls.Add(full);modes.Controls.Add(solo);inputControls.Add(full);inputControls.Add(solo);
            LabelAt(this, packageSummary, "", 26, 151, 513, 53, 9.5f, Muted);
            ButtonAt(this, new Button(), "Ver componentes…", 562, 119, 256, false, ShowComponents);
            AddPath("GTA San Andreas clásico", game, gameHint, 210, BrowseGame);
            AddPath("ISO de GTA IV para Xbox 360", iso, isoHint, 312, BrowseIso);
            destinationBrowse = AddPath("Carpeta de SA Delirium", destination, destinationHint, 414, BrowseDestination);
            LabelAt(this, new Label(), "Modelos de personajes · instalación completa", 26, 517, 510, 22, 10, TextColor, true);
            ConfigureRadio(originals, "Originales con codos y rodillas suavizados", 542);
            ConfigureRadio(hd, "Pack HD de V@DO$ · descarga de 278 MB", 568);
            originals.Checked = true;

            var card = new Panel { BackColor = Surface, Bounds = new Rectangle(562, 210, 256, 438) };
            Controls.Add(card);
            LabelAt(card, new Label(), "Progreso de la instalación", 17, 14, 222, 27, 10, TextColor, true);
            for (int i = 0; i < Steps.Length; i++)
                stepLabels[i] = LabelAt(card, new Label(), "", 17, 45 + 25 * i, 225, 24, 9.5f, Muted);
            card.Controls.Add(new Panel { BackColor = Background, Bounds = new Rectangle(17, 176, 222, 1) });
            LabelAt(card, status, "Selecciona tu juego", 17, 191, 222, 65, 16, TextColor, true);
            LabelAt(card, detail, "", 17, 260, 222, 91, 9.5f, Muted);
            progress.SetBounds(17, 356, 222, 13);
            card.Controls.Add(progress);
            ButtonAt(card, diagnostic, "Ver diagnóstico", 17, 385, 222, false, ShowDiagnostic);
            diagnostic.Visible = false;

            var help = new LinkLabel { Text = "Ayuda en YouTube · AdriiSuizo", LinkColor = Ready,
                ActiveLinkColor = TextColor, VisitedLinkColor = Ready, Bounds = new Rectangle(26, 619, 490, 30) };
            help.LinkClicked += delegate { OpenChannel(); };
            Controls.Add(help);
            Controls.Add(new Panel { BackColor = Surface, Bounds = new Rectangle(26, 661, 792, 1) });
            ButtonAt(this, restore, "Restaurar", 26, 675, 126, false, delegate { BeginWork(true); });
            ButtonAt(this, cancel, "Cerrar", 543, 675, 116, false, CancelOrClose);
            ButtonAt(this, install, "Instalar", 672, 675, 146, true, delegate { BeginWork(false); });
            AcceptButton = install;

            destination.Text = Standalone.DefaultDestination;
            solo.CheckedChanged += delegate { InputsChanged(); };
            game.TextChanged += delegate { QueueVerification(); };
            iso.TextChanged += delegate { InputsChanged(); };
            destination.TextChanged += delegate { InputsChanged(); };
            verifyDelay.Tick += delegate {
                if (verifier.IsBusy) return;
                verifyDelay.Stop();
                if (pendingFolder != null && !busy) verifier.RunWorkerAsync(pendingFolder);
            };
            verifier.DoWork += delegate(object sender, DoWorkEventArgs e) {
                var selection = (string)e.Argument;
                EditionReport result;
                try { result = Program.Identify(manifest, selection); }
                catch (Exception error) { result = new EditionReport { message = ShortMessage(error) }; }
                e.Result = new KeyValuePair<string, EditionReport>(selection, result);
            };
            verifier.RunWorkerCompleted += delegate(object sender, RunWorkerCompletedEventArgs e) {
                if (IsDisposed || Disposing || e.Cancelled || e.Error != null) return;
                var result = (KeyValuePair<string, EditionReport>)e.Result;
                if (!String.Equals(pendingFolder, result.Key, StringComparison.OrdinalIgnoreCase)) return;
                checkedFolder = result.Key;
                edition = result.Value;
                verifyDelay.Stop();
                UpdateControls();
            };
            worker.DoWork += Work;
            worker.ProgressChanged += delegate(object sender, ProgressChangedEventArgs e) {
                if (busy && !cancelling) ShowProgress((SetupUpdate)e.UserState);
            };
            worker.RunWorkerCompleted += Complete;
            Load += delegate {
                var area = Screen.FromControl(this).WorkingArea;
                if (Height > area.Height || Width > area.Width) {
                    AutoScroll = true;
                    Size = new Size(Math.Min(Width, area.Width), Math.Min(Height, area.Height));
                }
            };
            FormClosing += delegate(object sender, FormClosingEventArgs e) {
                if (busy) { e.Cancel = true; RequestCancellation(); }
            };

            try {
                var games = Discovery.SteamGames();
                if (games.Count == 1) {
                    game.Text = games[0];
                    var images = Directory.GetFiles(games[0], "*.iso", SearchOption.TopDirectoryOnly);
                    if (images.Length == 1) iso.Text = images[0];
                }
            } catch (IOException) { }
            catch (UnauthorizedAccessException) { }
            UpdateControls();
        }

        static Label LabelAt(Control owner, Label label, string value, int x, int y, int w, int h, float size, Color color, bool bold = false) {
            label.Text = value;
            label.Font = new Font("Segoe UI", size, bold ? FontStyle.Bold : FontStyle.Regular);
            label.ForeColor = color;
            label.SetBounds(x, y, w, h);
            owner.Controls.Add(label);
            return label;
        }

        static void ButtonAt(Control owner, Button button, string value, int x, int y, int w, bool primary, Action click) {
            button.Text = value;
            button.SetBounds(x, y, w, 36);
            button.FlatStyle = FlatStyle.Flat;
            button.FlatAppearance.BorderColor = primary ? Accent : Color.FromArgb(76, 67, 94);
            button.BackColor = primary ? Accent : Surface;
            button.ForeColor = TextColor;
            button.UseVisualStyleBackColor = false;
            button.Click += delegate { click(); };
            owner.Controls.Add(button);
        }

        Button AddPath(string title, TextBox box, Label hint, int y, Action browse) {
            LabelAt(this, new Label(), title, 26, y, 511, 22, 10, TextColor, true);
            box.SetBounds(26, y + 26, 406, 28);
            box.Font = new Font("Segoe UI", 10);
            box.BackColor = Field;
            box.ForeColor = TextColor;
            box.AccessibleName = title;
            Controls.Add(box);
            var button = new Button { AccessibleName = "Buscar " + title };
            ButtonAt(this, button, "Buscar…", 443, y + 21, 94, false, browse);
            LabelAt(this, hint, "", 26, y + 59, 510, 40, 9, Muted);
            inputControls.Add(box);
            inputControls.Add(button);
            return button;
        }

        void ConfigureRadio(RadioButton radio, string value, int y) {
            radio.Text = value;
            radio.SetBounds(26, y, 511, 25);
            Controls.Add(radio);
            inputControls.Add(radio);
        }

        void ShowComponents() {
            string description;
            if (solo.Checked) {
                description = "Delirium para GTA San Andreas\r\n"
                    + "Físicas de personajes, combate, movimiento de CJ y conducción GTA IV.\r\n"
                    + "Recursos y sonidos preparados desde tu ISO de Xbox 360.\r\n"
                    + "Cargador ASI verificado si tu copia necesita uno.";
            } else {
                var ids = new HashSet<string>(manifest.files.SelectMany(f => f.components ?? new string[0]), StringComparer.OrdinalIgnoreCase);
                description = String.Join("\r\n", (manifest.profile.components ?? new ProfileComponent[0])
                    .Where(c => ids.Contains(c.id) && (hd.Checked || c.id != "hd-skin-pack-299"))
                    .Select(c => "• " + c.name).OrderBy(name => name));
                if (manifest.unified) description += "\r\n\r\nEl manejo GTA IV es el predeterminado. Clásico e Izzy están en Pausa → Opciones.";
                description += "\r\n\r\nUrbanize se descarga del autor si está disponible.";
            }
            using (var dialog = new Form { Text = "Componentes de la instalación", Font = Font, ClientSize = new Size(570, 440),
                StartPosition = FormStartPosition.CenterParent, FormBorderStyle = FormBorderStyle.FixedDialog,
                BackColor = Background, ForeColor = TextColor, MaximizeBox = false, MinimizeBox = false }) {
                var contents = new TextBox { Text = description, ReadOnly = true, Multiline = true, ScrollBars = ScrollBars.Vertical,
                    BackColor = Field, ForeColor = TextColor, Bounds = new Rectangle(18, 18, 534, 352) };
                dialog.Controls.Add(contents);
                var close = new Button { DialogResult = DialogResult.Cancel };
                ButtonAt(dialog, close, "Cerrar", 426, 386, 126, false, delegate { dialog.Close(); });
                dialog.CancelButton = close;
                dialog.ShowDialog(this);
            }
        }

        static string ReadPath(string value) {
            value = value.Trim().Trim('"').Trim();
            try { return Path.IsPathRooted(value) ? Paths.Full(value) : null; }
            catch (ArgumentException) { return null; }
            catch (IOException) { return null; }
            catch (NotSupportedException) { return null; }
        }

        string GameFolder() {
            var path = ReadPath(game.Text);
            if (path == null) return null;
            if (File.Exists(path) && Path.GetExtension(path).Equals(".exe", StringComparison.OrdinalIgnoreCase))
                return Path.GetDirectoryName(path);
            return path;
        }

        string IsoPath() {
            var path = ReadPath(iso.Text);
            return path != null && Path.GetExtension(path).Equals(".iso", StringComparison.OrdinalIgnoreCase) && File.Exists(path) ? path : null;
        }

        void BrowseGame() {
            using (var dialog = new OpenFileDialog {
                Title = "Selecciona el ejecutable de GTA San Andreas clásico",
                Filter = "GTA San Andreas (gta_sa.exe, gta-sa.exe)|gta_sa.exe;gta-sa.exe|Otros ejecutables (*.exe)|*.exe", CheckFileExists = true, RestoreDirectory = true,
                InitialDirectory = Directory.Exists(GameFolder()) ? GameFolder() : ""
            }) if (dialog.ShowDialog(this) == DialogResult.OK) {
                if (String.Equals(game.Text, dialog.FileName, StringComparison.OrdinalIgnoreCase)) {
                    checkedFolder = null;
                    QueueVerification();
                } else game.Text = dialog.FileName;
            }
        }

        void BrowseIso() {
            using (var dialog = new OpenFileDialog {
                Filter = "Imagen de disco Xbox 360 (*.iso)|*.iso", CheckFileExists = true,
                Title = "Selecciona tu ISO de GTA IV para Xbox 360", RestoreDirectory = true
            }) if (dialog.ShowDialog(this) == DialogResult.OK) iso.Text = dialog.FileName;
        }

        void BrowseDestination() {
            using (var dialog = new FolderBrowserDialog {
                Description = "Elige dónde instalar SA Delirium.",
                SelectedPath = Directory.Exists(ReadPath(destination.Text)) ? ReadPath(destination.Text) : "",
                ShowNewFolderButton = true
            }) if (dialog.ShowDialog(this) == DialogResult.OK) destination.Text = dialog.SelectedPath;
        }

        void QueueVerification() {
            pendingFolder = ReadPath(game.Text);
            verifyDelay.Stop();
            if (!String.Equals(checkedFolder, pendingFolder, StringComparison.OrdinalIgnoreCase)) {
                edition = null;
                checkedFolder = null;
                if (pendingFolder != null && !busy) verifyDelay.Start();
            }
            InputsChanged();
        }

        void InputsChanged() {
            finished = false;
            diagnosticText = null;
            if (!busy) {
                outcome = Outcome.Pending;
                restoring = false;
            }
            UpdateControls();
        }

        string RestoreTarget() {
            var folder = GameFolder();
            if (folder != null && File.Exists(Path.Combine(folder, Engine.ReceiptName))) return folder;
            var own = ReadPath(destination.Text);
            return Standalone.Installed(own) ? own : null;
        }

        string DestinationIssue(string own) {
            if (own == null) return "Indica una ruta de destino válida.";
            try { Standalone.CheckDestination(edition.folder, own, manifest.profile.native_save_path_limit_bytes); return null; }
            catch (IOException error) { return ShortMessage(error); }
            catch (UnauthorizedAccessException error) { return ShortMessage(error); }
            catch (ArgumentException error) { return ShortMessage(error); }
        }

        void UpdateControls() {
            foreach (var control in inputControls) control.Enabled = !busy;
            var pure = solo.Checked;
            var steamMode = !pure && edition != null && edition.accepted && edition.mode == "steam";
            destination.Enabled = destinationBrowse.Enabled = !busy && !steamMode && !pure;
            originals.Enabled = hd.Enabled = !busy && !pure;
            var image = IsoPath();
            var own = ReadPath(destination.Text);
            var destinationIssue = edition == null || !edition.accepted ? null : pure ?
                (Solo.NativeCompatible(edition) ? null : "Solo Delirium requiere un ejecutable 1.0 US compatible. Prueba la instalación completa para esta edición.") :
                !steamMode ? DestinationIssue(own) : null;
            install.Enabled = !busy && !manifest.lab_only && edition != null && edition.accepted && image != null && destinationIssue == null;
            restore.Enabled = !busy && RestoreTarget() != null;
            cancel.Enabled = !busy || (!restoring && !cancelling);
            cancel.Text = busy ? (cancelling ? "Cancelando…" : "Cancelar") : "Cerrar";
            install.BackColor = install.Enabled ? Accent : Surface;
            install.FlatAppearance.BorderColor = install.Enabled ? Accent : Color.FromArgb(76, 67, 94);
            UpdateChecklist();
            if (busy) return;

            packageSummary.Text = pure
                ? "Añade físicas, combate, movimiento y conducción de Delirium a tu juego. Añade un cargador ASI verificado si hace falta. Conserva tus mods."
                : "Delirium, fixes, aspecto PS2, Trainer, Mod Loader, CLEO, Urbanize e Izzy." +
                  (manifest.unified ? " El manejo GTA IV viene seleccionado; puedes cambiarlo en el menú." : "");
            gameHint.Text = edition != null ? (edition.accepted ? edition.edition + " · " + edition.executable : edition.message)
                : Directory.Exists(GameFolder()) ? "Identificando el ejecutable…" :
                String.IsNullOrWhiteSpace(game.Text) ? "Tu instalación actual sirve para verificar el juego y leer sus archivos." :
                "Elige un ejecutable o una carpeta que exista y esté accesible.";
            gameHint.ForeColor = edition == null ? Muted : edition.accepted ? Ready : Warning;
            gameHint.AutoEllipsis = true;
            isoHint.Text = image != null ? "Se extraerán las físicas, animaciones y sonidos desde esta ISO." :
                String.IsNullOrWhiteSpace(iso.Text) ? "Elige tu ISO para importar las físicas, animaciones y sonidos de GTA IV." : "Elige un archivo .iso que exista y esté accesible.";
            isoHint.ForeColor = image == null && !String.IsNullOrWhiteSpace(iso.Text) ? Warning : Muted;
            destinationHint.Text = pure ? "Solo Delirium se añade al GTA San Andreas que has elegido." :
                steamMode ? "El mod se abre desde el botón Jugar de Steam." :
                destinationIssue ?? "El mod se instala aparte y se abre con un acceso directo.";
            destinationHint.ForeColor = destinationIssue == null ? Muted : Warning;

            if (finished) return;
            diagnostic.Visible = diagnosticText != null || (edition != null && !edition.accepted);
            progress.Style = ProgressBarStyle.Continuous;
            progress.Value = 0;
            if (manifest.lab_only) {
                status.Text = "Compilación de pruebas";
                detail.Text = "Usa el modo de validación local.";
            } else if (edition != null && !edition.accepted) {
                status.Text = "Revisa el juego";
                detail.Text = ShortMessage(new IOException(edition.message));
            } else if (edition == null) {
                status.Text = Directory.Exists(GameFolder()) ? "Comprobando el juego" : "Selecciona tu juego";
                detail.Text = "El asistente identificará la edición y te dirá cómo se abrirá el mod.";
            } else if (image == null) {
                status.Text = "Selecciona la ISO";
                detail.Text = "Los recursos de GTA IV se preparan en este PC a partir de tu imagen de disco de Xbox 360.";
            } else if (destinationIssue != null) {
                status.Text = "Revisa el destino";
                detail.Text = destinationIssue;
            } else {
                status.Text = "Listo para instalar";
                detail.Text = pure ? "Se añadirán los archivos de Delirium. Tus partidas y tu ejecutable se conservan." :
                    steamMode ? "Se guardará una copia de respaldo del ejecutable para poder restaurar Steam." :
                    "Tu juego original se conservará. Se creará un acceso directo a SA Delirium.";
            }
        }

        sealed class SetupJob {
            public string Target, Image, Destination, Edition;
            public bool Restore, Hd, Solo;
        }

        sealed class SetupUpdate {
            public SetupPhase Phase;
            public long Completed, Total;
            public string Item;
        }

        static string PhaseName(SetupPhase value) {
            switch (value) {
                case SetupPhase.Validation: return "Comprobando requisitos";
                case SetupPhase.Packages: return "Preparando componentes";
                case SetupPhase.Files: return "Copiando archivos";
                case SetupPhase.Resources: return "Importando recursos";
                case SetupPhase.Install: return "Instalando SA Delirium";
                default: return "Restaurando";
            }
        }

        void ShowProgress(SetupUpdate update) {
            var changed = phase != update.Phase;
            phase = update.Phase;
            if (changed) progress.Value = 0;
            UpdateChecklist();
            status.Text = PhaseName(phase);
            if (update.Total > 0) {
                var value = (int)(Math.Max(0, Math.Min(1, (double)update.Completed / update.Total)) * progress.Maximum);
                progress.Style = ProgressBarStyle.Continuous;
                progress.Value = Math.Max(progress.Value, value);
                detail.Text = (progress.Value / 10) + " % de este paso" + (update.Item == null ? "" : "\n\n" + update.Item);
            } else {
                progress.Style = ProgressBarStyle.Marquee;
                detail.Text = update.Item ?? "";
            }
        }

        SetupJob job;

        void BeginWork(bool undo) {
            var target = undo ? RestoreTarget() : ReadPath(game.Text);
            if (target == null || (!undo && !install.Enabled)) return;
            job = new SetupJob { Target = target, Image = IsoPath(), Destination = ReadPath(destination.Text),
                Edition = EditionDiagnostic(edition),
                Restore = undo, Hd = hd.Checked && !solo.Checked, Solo = solo.Checked };
            verifyDelay.Stop();
            restoring = undo;
            cancelling = false;
            busy = true;
            outcome = Outcome.Running;
            finished = false;
            diagnosticText = null;
            diagnostic.Visible = false;
            progress.Value = 0;
            ShowProgress(new SetupUpdate { Phase = undo ? SetupPhase.Restore : SetupPhase.Validation });
            UpdateControls();
            worker.RunWorkerAsync(job);
        }

        void Work(object sender, DoWorkEventArgs e) {
            var request = (SetupJob)e.Argument;
            var currentPhase = request.Restore ? SetupPhase.Restore : SetupPhase.Validation;
            try {
                var fresh = Program.ReadManifest();
                if (request.Restore) {
                    e.Result = Program.Restore(fresh, request.Target);
                    return;
                }
                var last = Stopwatch.StartNew();
                e.Result = Program.Install(fresh, request.Target, request.Image, Program.DefaultCache,
                    delegate(long completed, long total, string item) {
                        if (completed != total && last.ElapsedMilliseconds < 120) return;
                        last.Restart();
                        worker.ReportProgress(0, new SetupUpdate { Phase = currentPhase, Completed = completed, Total = total, Item = item });
                    }, delegate { return worker.CancellationPending; }, false, false, request.Destination, null, false, request.Hd,
                    delegate(SetupPhase value) {
                        currentPhase = value;
                        worker.ReportProgress(0, new SetupUpdate { Phase = value });
                    }, request.Solo);
            } catch (Exception error) {
                error.Data["SetupPhase"] = currentPhase;
                throw;
            }
        }

        void RequestCancellation() {
            if (restoring || cancelling) return;
            cancelling = true;
            worker.CancelAsync();
            status.Text = "Cancelando…";
            detail.Text = "Espera a que el asistente termine este paso.";
            UpdateControls();
        }

        void CancelOrClose() {
            if (busy) RequestCancellation();
            else Close();
        }

        static string ShortMessage(Exception error) {
            var message = (error.Data["SetupDetail"] as string) ?? error.Message;
            message = Regex.Split(message, @"\r?\n\s*Registro:", RegexOptions.IgnoreCase)[0].Trim();
            if (message.StartsWith("Este ejecutable de GTA IV no coincide", StringComparison.Ordinal))
                return "Esta edición de GTA IV para Xbox 360 todavía no es compatible. Prueba otra ISO de tu juego o copia el diagnóstico.";
            if (error is UnauthorizedAccessException || (error is Win32Exception && ((Win32Exception)error).NativeErrorCode == 5))
                return "Windows no dio acceso a uno de los archivos. Comprueba que puedes abrir los originales y elige un destino donde puedas escribir.";
            if (error is System.Net.WebException)
                return "No se pudo descargar un componente. Comprueba tu conexión e inténtalo de nuevo.";
            if (error is IOException && (error.HResult & 0xffff) == 112)
                return "No hay espacio suficiente en disco. Libera espacio y vuelve a intentarlo.";
            return message.Length > 270 ? message.Substring(0, 267) + "…" : message;
        }

        internal void DrawPreviewControls(Bitmap bitmap, int x, int y) {
            DrawPreviewChildren(this, bitmap, x, y);
        }

        void DrawPreviewChildren(Control owner, Bitmap bitmap, int x, int y) {
            foreach (var control in owner.Controls.Cast<Control>().Reverse()) {
                if (control == diagnostic && diagnosticText == null) continue;
                var rect = new Rectangle(x + control.Left, y + control.Top, control.Width, control.Height);
                var handle = control.Handle;
                control.DrawToBitmap(bitmap, rect);
                DrawPreviewChildren(control, bitmap, rect.X, rect.Y);
            }
        }

        void UpdateChecklist() {
            int current = restoring ? 0 : (int)phase;
            for (int i = 0; i < stepLabels.Length; i++) {
                var label = stepLabels[i];
                label.Visible = !restoring || i == 0;
                var mark = "○";
                label.ForeColor = Muted;
                if (outcome == Outcome.Complete || (outcome != Outcome.Pending && i < current)) {
                    label.ForeColor = Ready;
                    mark = "✓";
                } else if (i == current && outcome == Outcome.Running) {
                    label.ForeColor = Active;
                    mark = "●";
                } else if (i == current && outcome == Outcome.Failed) {
                    label.ForeColor = Failed;
                    mark = "✕";
                }
                label.Text = mark + "  " + (restoring ? (i == 0 ? "Restaurar el juego" : "") : Steps[i]);
            }
        }

        static string EditionDiagnostic(EditionReport report) {
            if (report == null) return null;
            return report.edition + (report.compatibility == null ? "" : " (" + report.compatibility + ")") +
                (report.sha256 == null ? "" : "\r\nSHA-256 de GTA SA: " + report.sha256);
        }

        void Complete(object sender, RunWorkerCompletedEventArgs e) {
            busy = false;
            finished = true;
            progress.Style = ProgressBarStyle.Continuous;
            bool showError = false;
            if (e.Error != null && e.Error.Data["SetupPhase"] is SetupPhase) phase = (SetupPhase)e.Error.Data["SetupPhase"];
            if (e.Error is OperationCanceledException || e.Cancelled) {
                outcome = Outcome.Cancelled;
                status.Text = "Instalación cancelada";
                detail.Text = "Puedes volver a intentarlo. Tus rutas siguen seleccionadas.";
            } else if (e.Error != null) {
                outcome = Outcome.Failed;
                status.Text = "Revisa este paso";
                detail.Text = SetupDiagnostic.Sanitize(ShortMessage(e.Error), job.Target, GameFolder(), job.Image, job.Destination);
                diagnosticText = SetupDiagnostic.Build(Program.ReleaseVersion(manifest), PhaseName(phase),
                    job.Edition, job.Hd, e.Error, job.Target, GameFolder(), job.Image, job.Destination);
                diagnostic.Visible = true;
                showError = true;
            } else {
                outcome = Outcome.Complete;
                progress.Value = progress.Maximum;
                var receipt = (Receipt)e.Result;
                var own = receipt.Mode == "standalone";
                status.Text = receipt.State == "installed" ? "Ya puedes jugar" : "Juego restaurado";
                detail.Text = receipt.State == "installed"
                    ? (receipt.Mode == "solo" ? "Abre tu juego como de costumbre." : own ? "Abre SA Delirium desde su acceso directo." : "Abre San Andreas con Jugar en Steam.") +
                        (receipt.SavesFrom != null ? "\n\nSe han recuperado tus partidas anteriores." : "")
                    : "El perfil y sus partidas están en la copia de respaldo.";
                checkedFolder = null;
                edition = null;
                pendingFolder = ReadPath(game.Text);
                if (Directory.Exists(GameFolder())) verifyDelay.Start();
            }
            job = null;
            UpdateControls();
            if (showError) ShowDiagnostic();
        }

        static void OpenChannel() {
            try { Process.Start(new ProcessStartInfo(Channel) { UseShellExecute = true }); }
            catch (Win32Exception) { MessageBox.Show(Channel, "Canal de AdriiSuizo"); }
        }

        internal static void ShowStartupError(Manifest manifest, Exception error) {
            var version = manifest == null ? "versión sin identificar" : Program.ReleaseVersion(manifest);
            var text = SetupDiagnostic.Build(version, "Abrir el asistente", null, false, error);
            using (var font = new Font("Segoe UI", 9.5f))
                DiagnosticDialog(null, font, "El asistente no pudo abrirse. Prueba a descargar el instalador de nuevo.", text);
        }

        void ShowDiagnostic() {
            var text = diagnosticText;
            var explanation = detail.Text;
            if (text == null && edition != null && !edition.accepted) {
                text = SetupDiagnostic.Build(Program.ReleaseVersion(manifest), PhaseName(SetupPhase.Validation),
                    EditionDiagnostic(edition), hd.Checked, new IOException(edition.message), edition.folder);
                explanation = edition.message;
            }
            if (text == null) return;
            DiagnosticDialog(this, Font, explanation, text);
        }

        static void DiagnosticDialog(IWin32Window owner, Font font, string explanation, string text) {
            using (var dialog = new Form {
                Text = "SA Delirium · Ayuda con la instalación", Font = font, ClientSize = new Size(658, 460),
                StartPosition = owner == null ? FormStartPosition.CenterScreen : FormStartPosition.CenterParent,
                FormBorderStyle = FormBorderStyle.FixedDialog,
                MaximizeBox = false, MinimizeBox = false, BackColor = Background, ForeColor = TextColor
            }) {
                LabelAt(dialog, new Label(), explanation, 18, 17, 622, 75, 10.5f, TextColor);
                LabelAt(dialog, new Label(), "Copia este texto y pégalo en un comentario de mi vídeo de instalación.", 18, 96, 622, 34, 10, Muted);
                var report = new TextBox { Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Vertical,
                    Text = text, BackColor = Field, ForeColor = TextColor, Bounds = new Rectangle(18, 137, 622, 246) };
                dialog.Controls.Add(report);
                var close = new Button { DialogResult = DialogResult.Cancel };
                ButtonAt(dialog, close, "Volver", 514, 400, 126, false, delegate { dialog.Close(); });
                dialog.CancelButton = close;
                var copy = new Button();
                ButtonAt(dialog, copy, "Copiar para YouTube", 18, 400, 177, true, delegate {
                    try { Clipboard.SetText(text); copy.Text = "Copiado"; }
                    catch (System.Runtime.InteropServices.ExternalException) { report.Focus(); report.SelectAll(); copy.Text = "Pulsa Ctrl+C"; }
                });
                ButtonAt(dialog, new Button(), "Abrir canal", 207, 400, 132, false, OpenChannel);
                dialog.ShowDialog(owner);
            }
        }

        protected override void Dispose(bool disposing) {
            if (disposing) {
                verifyDelay.Dispose();
                verifier.Dispose();
                worker.Dispose();
            }
            base.Dispose(disposing);
        }
    }

    public static class SetupDiagnostic {
        public static string Sanitize(string value, params string[] paths) {
            var roots = paths.Concat(new[] { Program.DefaultCache, Environment.GetFolderPath(Environment.SpecialFolder.UserProfile) })
                .Where(p => !String.IsNullOrWhiteSpace(p) && p.Length > 3)
                .SelectMany(p => new[] { p, p.Replace('\\', '/') }).Distinct(StringComparer.OrdinalIgnoreCase).OrderByDescending(p => p.Length);
            foreach (var path in roots)
                value = Regex.Replace(value, Regex.Escape(path), "[ruta local]", RegexOptions.IgnoreCase);
            foreach (var path in paths.Where(p => !String.IsNullOrEmpty(p))) {
                var name = Path.GetFileName(path);
                if (name.EndsWith(".iso", StringComparison.OrdinalIgnoreCase) ||
                    (name.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) &&
                        !name.Equals("gta_sa.exe", StringComparison.OrdinalIgnoreCase) &&
                        !name.Equals("gta-sa.exe", StringComparison.OrdinalIgnoreCase)))
                    value = Regex.Replace(value, Regex.Escape(name), "[archivo local]", RegexOptions.IgnoreCase);
            }
            value = Regex.Replace(value, @"(?:[A-Z]:[/\\]|\\\\[^\\\s]+\\)[^\r\n""'<>|]*", "[ruta local]", RegexOptions.IgnoreCase);
            value = Regex.Replace(value, @"(?:https?|file)://[^\s<>""']+", "[enlace]", RegexOptions.IgnoreCase);
            value = Regex.Replace(value, @"[\w.+%-]+@[\w.-]+\.[A-Z]{2,}|\bS-1-(?:\d+-)+\d+\b|\b(?:\d{1,3}\.){3}\d{1,3}\b", "[dato oculto]", RegexOptions.IgnoreCase);
            foreach (var identity in new[] { Environment.UserName, Environment.MachineName }.Where(x => !String.IsNullOrWhiteSpace(x))) {
                value = Regex.Replace(value, @"(?<![\w])" + Regex.Escape(identity) + @"(?![\w])", "[dato oculto]", RegexOptions.IgnoreCase);
            }
            return value;
        }

        public static string Build(string version, string phase, string edition, bool hdModels, Exception error, params string[] paths) {
            var report = new StringBuilder();
            report.AppendLine("Problema al instalar SA Delirium " + version);
            report.AppendLine("Windows " + Environment.OSVersion.Version + (Environment.Is64BitOperatingSystem ? " · 64 bits" : " · 32 bits"));
            report.AppendLine("Paso: " + phase);
            if (!String.IsNullOrEmpty(edition)) report.AppendLine("GTA SA: " + edition);
            report.AppendLine("Modelos: " + (hdModels ? "pack HD" : "originales"));
            report.AppendLine("Error: " + error.GetType().Name + " (0x" + error.HResult.ToString("X8") + ")");
            if (error is Win32Exception) report.AppendLine("Código de Windows: " + ((Win32Exception)error).NativeErrorCode);
            if (error.Data["SetupTool"] != null) report.AppendLine("Operación: " + Sanitize(error.Data["SetupTool"].ToString(), paths));
            if (error.Data["SetupExitCode"] != null) report.AppendLine("Código de salida: " + error.Data["SetupExitCode"]);
            if (error.Data["SetupOriginals"] != null) report.AppendLine("Archivos por revisar: " + Sanitize(error.Data["SetupOriginals"].ToString(), paths));
            var message = Regex.Split((error.Data["SetupDetail"] as string) ?? error.Message, @"\r?\n\s*Registro:", RegexOptions.IgnoreCase)[0].Trim();
            var safeMessage = Sanitize(message, paths);
            report.AppendLine().AppendLine(Limit(safeMessage, 600));
            var log = error.Data["SetupLog"] as string;
            if (log != null) {
                try {
                    using (var input = new FileStream(log, FileMode.Open, FileAccess.Read, FileShare.ReadWrite)) {
                        input.Seek(Math.Max(0, input.Length - 8192), SeekOrigin.Begin);
                        using (var reader = new StreamReader(input, Encoding.UTF8)) {
                            if (input.Position != 0) reader.ReadLine(); // The first line may be truncated.
                            var lines = reader.ReadToEnd().Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries)
                                .Where(line => Regex.IsMatch(line, @"error|fail|denied|invalid|mismatch|cannot|no (se|coincide|contiene)|CRC|default\.xex|BLAKE3", RegexOptions.IgnoreCase))
                                .Select(line => Sanitize(line.Trim(), paths)).Where(line => !safeMessage.Contains(line)).Distinct().Reverse().Take(4).Reverse().ToArray();
                            if (lines.Length > 0) report.AppendLine().AppendLine("Registro:").Append(Limit(String.Join("\r\n", lines), 650));
                        }
                    }
                } catch (IOException) { }
                catch (UnauthorizedAccessException) { }
            }
            return report.ToString();
        }

        static string Limit(string text, int length) {
            return text.Length <= length ? text : text.Substring(0, length - 1) + "…";
        }
    }
}
