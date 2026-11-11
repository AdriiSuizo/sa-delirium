using System;
using System.ComponentModel;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Reflection;
using System.Threading;
using System.Windows.Forms;
using SASetup;

namespace SAPortable {
    public static class Program {
        public const string Version="0.3.2";
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
            if(File.Exists(journal) && Json.Read<Receipt>(journal).Mode=="standalone")return Standalone.Restore(target);
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
                                      string destination=null,string[] shortcuts=null,bool forceStandalone=false,bool hdModels=false) {
            if(manifest.lab_only && !lab)throw new IOException("Candidato local de validación; no es una distribución publicable.");
            var launcher=Resource("SteamLauncher.exe");var initial=new Engine(manifest.profile,launcher);
            var edition=Identify(manifest,steam);
            if(!edition.accepted)throw new IOException(edition.message);
            bool steamMode=SteamMode(manifest,edition,lab,forceStandalone);
            if(steamMode && !lab)initial.CheckSteamTarget(steam);
            using(var mutex=new Mutex(false,"Local\\SA-Delirium-Install-"+Engine.Hash(System.Text.Encoding.UTF8.GetBytes(Paths.Full(steam).ToLowerInvariant())).Substring(0,24))) {
                if(!mutex.WaitOne(0))throw new IOException("Ya hay otro instalador trabajando en esta instalación de Steam.");
                try {
                    var helper=Helpers(cache);
                    var composer=new Composer(manifest,helper,cache){Progress=progress,Cancelled=cancelled,Unattended=unattended,HdModels=hdModels};
                    string prepared;
                    var index=composer.Prepare(steam,iso,out prepared);
                    destination=destination??Standalone.DefaultDestination;
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
            try {
                var manifest=ReadManifest();
                if(cli) {
                    object result;var report=Option(args,"--report");var cache=Option(args,"--cache",DefaultCache);
                    switch(args[0]) {
                        case "--detect":result=new{version=Version,steam=Discovery.SteamGames(),editions=Discovery.SteamGames().Select(g=>Identify(manifest,g)).ToArray(),iso_required=true,development_source_required=false,omissions=manifest.omissions};break;
                        case "--soften":{var kept=new List<ProfileFile>();result=new{models=SoftModels.Write(Option(args,"--target"),kept,delegate{}),files=kept};break;}
                        case "--identify":result=Identify(manifest,Option(args,"--target"));break;
                        case "--install":result=Install(manifest,Option(args,"--target"),Option(args,"--iso"),cache,delegate{},delegate{return false;},args.Contains("--unattended"),args.Contains("--lab"),
                            Option(args,"--destination",Standalone.DefaultDestination),args.Contains("--no-shortcuts")?new string[0]:null,args.Contains("--standalone"),args.Contains("--hd-models"));break;
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
                            foreach(Control control in form.Controls){var handle=control.Handle;control.DrawToBitmap(bitmap,new Rectangle(control.Left+dx,control.Top+dy,control.Width,control.Height));}
                            bitmap.Save(Option(args,"--render"));
                        }
                    }
                }
                return 0;
            } catch(Exception error) {
                if(cli){try{Json.Write(Option(args,"--report"),new{status="failed",error=error.Message});}catch{}}
                else MessageBox.Show(error.Message,"SA Delirium",MessageBoxButtons.OK,MessageBoxIcon.Error);
                return 1;
            }
        }
    }
    public sealed class SetupForm:Form {
        readonly Manifest manifest;
        readonly TextBox steam=new TextBox(),iso=new TextBox(),destination=new TextBox();
        readonly Button install=new Button(),restore=new Button(),cancel=new Button();
        readonly RadioButton originalModels=new RadioButton(),hdModels=new RadioButton();
        readonly Label status=new Label();
        readonly ProgressBar progress=new ProgressBar { Maximum = 1000 };
        readonly BackgroundWorker worker=new BackgroundWorker {WorkerSupportsCancellation=true};
        DateTime lastProgress=DateTime.MinValue;bool busy;
        EditionReport edition;string identified;
        public SetupForm(Manifest value) {
            manifest=value;Text="SA Delirium · Instalar";ClientSize=new Size(760,661);
            FormBorderStyle=FormBorderStyle.FixedDialog;MaximizeBox=false;StartPosition=FormStartPosition.CenterScreen;
            Font=new Font("Segoe UI",10);BackColor=Color.FromArgb(246,248,251);
            Controls.Add(new Label {Text="San Andreas + Delirium",Font=new Font("Segoe UI",18,FontStyle.Bold),Location=new Point(28,24),Size=new Size(710,42)});
            Controls.Add(new Label {Text="Prepara el juego e instala los mods. Con Steam se usa su botón Jugar; con otra copia, un acceso directo.\nSolo necesitas tu San Andreas clásico y la ISO de GTA IV para Xbox 360.",Location=new Point(30,78),Size=new Size(700,52)});
            AddPath("Tu GTA San Andreas clásico (Steam o PC 1.0)",steam,148,false);
            AddPath("ISO de GTA IV para Xbox 360",iso,244,true);
            destination.Text=Standalone.DefaultDestination;
            AddPath("Carpeta del mod, si tu copia no es de Steam",destination,340,false,"Carpeta donde se instalará SA Delirium",true);
            Controls.Add(new Label {Text="Incluye trainer, fixes, aspecto PS2, Delirium e Izzy 1.6. Urbanize se descarga de su autor.\nSin packs de armas, texturas IV, RoSA ni HUD GTA V. Partidas y original respaldados.",Location=new Point(31,410),Size=new Size(700,50),ForeColor=Color.FromArgb(65,77,96)});
            Controls.Add(new Label{Text="Modelos de personajes",Location=new Point(31,466),Size=new Size(170,25)});
            originalModels.Text="Originales del juego, con codos y rodillas suavizados";originalModels.Checked=true;originalModels.SetBounds(205,464,520,25);Controls.Add(originalModels);
            hdModels.Text="HD Skin Pack 299, de V@DO$ (se descargan 278 MB de GTAInside)";hdModels.SetBounds(205,490,520,25);Controls.Add(hdModels);
            progress.SetBounds(31,532,698,15);Controls.Add(progress);
            status.SetBounds(31,558,698,40);Controls.Add(status);
            restore.Text="Restaurar";restore.SetBounds(31,611,160,34);restore.Click+=delegate{BeginWork(true);};Controls.Add(restore);
            cancel.Text="Cancelar";cancel.SetBounds(450,611,115,34);cancel.Click+=delegate{if(busy)worker.CancelAsync();else Close();};Controls.Add(cancel);
            install.Text="Instalar";install.SetBounds(581,611,148,34);install.BackColor=Color.FromArgb(32,91,210);install.ForeColor=Color.White;install.FlatStyle=FlatStyle.Flat;install.Click+=delegate{BeginWork(false);};Controls.Add(install);
            steam.TextChanged+=delegate{IdentifySteam();UpdateButtons();};iso.TextChanged+=delegate{UpdateButtons();};destination.TextChanged+=delegate{UpdateButtons();};
            FormClosing+=delegate(object sender,FormClosingEventArgs e){if(busy){worker.CancelAsync();e.Cancel=true;}};
            worker.RunWorkerCompleted+=delegate(object sender,RunWorkerCompletedEventArgs e){
                busy=false;steam.Enabled=iso.Enabled=destination.Enabled=true;identified=null;IdentifySteam(false);UpdateButtons();
                if(e.Error!=null) {
                    progress.Value=0;
                    if(e.Error is OperationCanceledException) {status.Text="Operación cancelada.";return;}
                    status.Text="La operación no se completó.";
                    MessageBox.Show(this,e.Error.Message,"SA Delirium",MessageBoxButtons.OK,MessageBoxIcon.Error);
                    return;
                }
                var receipt=(Receipt)e.Result;bool own=receipt.Mode=="standalone";progress.Value=progress.Maximum;
                var carried=receipt.SavesFrom!=null?" Se han copiado las partidas de tu instalación anterior.":"";
                status.Text=receipt.State=="installed"?(own?"Instalado en "+receipt.Target+". Abre SA Delirium desde el acceso directo del escritorio.":"Instalado. Abre San Andreas con Jugar en Steam.")+carried:
                    (own?"Desinstalado. Tu juego no se tocó; el perfil y sus partidas se conservan en el respaldo.":"Steam restaurado. El perfil y sus partidas se conservan en el respaldo.");
            };
            var games=Discovery.SteamGames();
            if(games.Count==1)steam.Text=games[0];
            var isos=games.SelectMany(g=>Directory.GetFiles(g,"*.iso",SearchOption.TopDirectoryOnly)).ToArray();
            if(isos.Length==1)iso.Text=isos[0];
            IdentifySteam();
            status.Text=manifest.lab_only?"Candidato local de validación. NO PUBLICAR: fuentes y permisos pendientes.":
                edition!=null&&!edition.accepted?edition.message:
                (games.Count==1?"Steam detectado. Selecciona la ISO para comenzar.":"Selecciona la carpeta de tu San Andreas clásico, de Steam o de PC.");
            UpdateButtons();
        }
        // Shows which edition the chosen folder holds before anything starts.
        void IdentifySteam(bool show=true) {
            var folder=steam.Text;
            if(folder==identified)return;
            identified=folder;edition=null;
            if(busy || !Directory.Exists(folder))return;
            try {edition=Program.Identify(manifest,folder);}
            catch(Exception e){edition=new EditionReport{folder=folder,message=e.Message};}
            if(show)status.Text=edition.accepted?edition.message+" Selecciona la ISO para comenzar.":edition.message;
        }
        void AddPath(string label,TextBox field,int y,bool image,string description="Carpeta de tu GTA San Andreas clásico",bool newFolder=false) {
            Controls.Add(new Label{Text=label,Location=new Point(31,y),Size=new Size(695,25)});
            field.SetBounds(31,y+31,570,29);Controls.Add(field);
            var button=new Button{Text="Elegir…",Location=new Point(613,y+29),Size=new Size(115,32)};
            button.Click+=delegate {
                if(busy)return;
                if(image){using(var dialog=new OpenFileDialog {Filter="ISO de GTA IV (Xbox 360)|*.iso",CheckFileExists=true,Title="Selecciona tu ISO de GTA IV para Xbox 360"})if(dialog.ShowDialog(this)==DialogResult.OK)field.Text=dialog.FileName;}
                else {using(var dialog=new FolderBrowserDialog {Description=description,ShowNewFolderButton=newFolder})if(dialog.ShowDialog(this)==DialogResult.OK)field.Text=dialog.SelectedPath;}
            };Controls.Add(button);
        }
        void UpdateButtons(){install.Enabled=!manifest.lab_only&&!busy&&File.Exists(iso.Text)&&edition!=null&&edition.accepted&&edition.folder.Equals(Paths.Full(steam.Text),StringComparison.OrdinalIgnoreCase);restore.Enabled=!busy&&(RestoreTarget()!=null);}
        // A journal in the chosen folder (Steam install) or in the own folder.
        string RestoreTarget() {
            try {
                if(!String.IsNullOrWhiteSpace(steam.Text)&&File.Exists(Path.Combine(steam.Text,Engine.ReceiptName)))return steam.Text;
                if(Standalone.Installed(destination.Text))return destination.Text;
            } catch(Exception) { }
            return null;
        }
        void ShowProgress(long completed, long total, string item) {
            double fraction = total > 0 ? Math.Min(1.0, Math.Max(0.0, (double)completed / total)) : 0.0;
            progress.Value = (int)(fraction * progress.Maximum);
            status.Text = item;
        }
        void BeginWork(bool undo) {
            var target=undo?RestoreTarget():steam.Text;var image=iso.Text;var own=destination.Text;var hd=hdModels.Checked;busy=true;UpdateButtons();steam.Enabled=iso.Enabled=destination.Enabled=false;status.Text=undo?"Restaurando…":"Comprobando tu juego y preparando el mod…";
            DoWorkEventHandler action=null;
            action=delegate(object sender,DoWorkEventArgs e) {
                try {
                    if(undo)e.Result=Program.Restore(manifest,target);
                    else e.Result=Program.Install(manifest,target,image,Program.DefaultCache,delegate(long completed,long total,string item){
                        if((DateTime.UtcNow-lastProgress).TotalMilliseconds<150)return;lastProgress=DateTime.UtcNow;
                        BeginInvoke((Action)delegate { ShowProgress(completed, total, item); });
                    },delegate{return worker.CancellationPending;},false,false,own,null,false,hd);
                } finally {worker.DoWork-=action;}
            };
            worker.DoWork+=action;worker.RunWorkerAsync();
        }
    }
}
