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
        public const string Version="0.3.1";
        public static byte[] Resource(string name) {
            using(var input=Assembly.GetExecutingAssembly().GetManifestResourceStream(name)) {
                if(input==null)throw new IOException("Falta un recurso del instalador: "+name);
                using(var output=new MemoryStream()){input.CopyTo(output);return output.ToArray();}
            }
        }
        public static Manifest ReadManifest() { return Json.Serializer().Deserialize<Manifest>(System.Text.Encoding.UTF8.GetString(Resource("portable-manifest.json"))); }
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
        public static Receipt Install(Manifest manifest,string steam,string iso,string cache,Action<long,long,string> progress,Func<bool> cancelled,bool unattended,bool lab) {
            if(manifest.lab_only && !lab)throw new IOException("Candidato local de validación; no es una distribución publicable.");
            var launcher=Resource("SteamLauncher.exe");var initial=new Engine(manifest.profile,launcher);
            if(!lab)initial.CheckSteamTarget(steam);
            using(var mutex=new Mutex(false,"Local\\SA-Delirium-Install-"+Engine.Hash(System.Text.Encoding.UTF8.GetBytes(Paths.Full(steam).ToLowerInvariant())).Substring(0,24))) {
                if(!mutex.WaitOne(0))throw new IOException("Ya hay otro instalador trabajando en esta instalación de Steam.");
                try {
                    var helper=Helpers(cache);
                    var composer=new Composer(manifest,helper,cache){Progress=progress,Cancelled=cancelled,Unattended=unattended};
                    string prepared;
                    var index=composer.Prepare(steam,iso,out prepared);
                    var engine=new Engine(index,launcher){Progress=progress,Cancelled=cancelled};
                    return engine.Install(engine.MakePlan(prepared,steam,false));
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
                        case "--detect":result=new{version=Version,steam=Discovery.SteamGames(),iso_required=true,development_source_required=false,omissions=manifest.omissions};break;
                        case "--install":result=Install(manifest,Option(args,"--target"),Option(args,"--iso"),cache,delegate{},delegate{return false;},args.Contains("--unattended"),args.Contains("--lab"));break;
                        case "--prepare":
                            var composer=new Composer(manifest,Helpers(cache),cache){Unattended=args.Contains("--unattended")};string prepared;
                            var index=composer.Prepare(Option(args,"--target"),Option(args,"--iso"),out prepared);
                            result=new{status="prepared",prepared_root=prepared,index};break;
                        case "--restore":result=new Engine(manifest.profile,Resource("SteamLauncher.exe")).Restore(Option(args,"--target"));break;
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
        readonly TextBox steam=new TextBox(),iso=new TextBox();
        readonly Button install=new Button(),restore=new Button(),cancel=new Button();
        readonly Label status=new Label();readonly ProgressBar progress=new ProgressBar();
        readonly BackgroundWorker worker=new BackgroundWorker {WorkerSupportsCancellation=true};
        DateTime lastProgress=DateTime.MinValue;bool busy;
        public SetupForm(Manifest value) {
            manifest=value;Text="SA Delirium · Instalar en Steam";ClientSize=new Size(760,555);
            FormBorderStyle=FormBorderStyle.FixedDialog;MaximizeBox=false;StartPosition=FormStartPosition.CenterScreen;
            Font=new Font("Segoe UI",10);BackColor=Color.FromArgb(246,248,251);
            Controls.Add(new Label {Text="San Andreas + Delirium, desde Steam",Font=new Font("Segoe UI",18,FontStyle.Bold),Location=new Point(28,24),Size=new Size(710,42)});
            Controls.Add(new Label {Text="El asistente prepara el juego, instala los mods y configura Jugar en Steam.\nSolo necesitas tu San Andreas clásico y la ISO de GTA IV para Xbox 360.",Location=new Point(30,78),Size=new Size(700,52)});
            AddPath("San Andreas clásico de Steam",steam,148,false);
            AddPath("ISO de GTA IV para Xbox 360",iso,244,true);
            Controls.Add(new Label {Text="Conserva extras HD, trainer, fixes, aspecto PS2, Delirium e Izzy 1.6.\nUrbanize se descarga automáticamente del autor; los demás mods vienen incluidos.\nSin packs de armas, texturas IV, RoSA ni HUD GTA V. Partidas y original respaldados.",Location=new Point(31,344),Size=new Size(700,72),ForeColor=Color.FromArgb(65,77,96)});
            progress.SetBounds(31,426,698,15);Controls.Add(progress);
            status.SetBounds(31,452,698,40);Controls.Add(status);
            restore.Text="Restaurar Steam";restore.SetBounds(31,505,160,34);restore.Click+=delegate{BeginWork(true);};Controls.Add(restore);
            cancel.Text="Cancelar";cancel.SetBounds(450,505,115,34);cancel.Click+=delegate{if(busy)worker.CancelAsync();else Close();};Controls.Add(cancel);
            install.Text="Instalar";install.SetBounds(581,505,148,34);install.BackColor=Color.FromArgb(32,91,210);install.ForeColor=Color.White;install.FlatStyle=FlatStyle.Flat;install.Click+=delegate{BeginWork(false);};Controls.Add(install);
            steam.TextChanged+=delegate{UpdateButtons();};iso.TextChanged+=delegate{UpdateButtons();};
            FormClosing+=delegate(object sender,FormClosingEventArgs e){if(busy){worker.CancelAsync();e.Cancel=true;}};
            worker.RunWorkerCompleted+=delegate(object sender,RunWorkerCompletedEventArgs e){
                busy=false;steam.Enabled=iso.Enabled=true;UpdateButtons();
                if(e.Error!=null){status.Text=e.Error.Message;progress.Value=0;return;}
                progress.Value=1000;status.Text=((Receipt)e.Result).State=="installed"?"Instalado. Abre San Andreas con Jugar en Steam.":"Steam restaurado. El perfil y sus partidas se conservan en el respaldo.";
            };
            var games=Discovery.SteamGames();
            if(games.Count==1)steam.Text=games[0];
            var isos=games.SelectMany(g=>Directory.GetFiles(g,"*.iso",SearchOption.TopDirectoryOnly)).ToArray();
            if(isos.Length==1)iso.Text=isos[0];
            status.Text=manifest.lab_only?"Candidato local de validación. NO PUBLICAR: fuentes y permisos pendientes.":(games.Count==1?"Steam detectado. Selecciona la ISO para comenzar.":"Selecciona tu instalación de San Andreas clásico en Steam.");
            UpdateButtons();
        }
        void AddPath(string label,TextBox field,int y,bool image) {
            Controls.Add(new Label{Text=label,Location=new Point(31,y),Size=new Size(695,25)});
            field.SetBounds(31,y+31,570,29);Controls.Add(field);
            var button=new Button{Text="Elegir…",Location=new Point(613,y+29),Size=new Size(115,32)};
            button.Click+=delegate {
                if(busy)return;
                if(image){using(var dialog=new OpenFileDialog {Filter="ISO de GTA IV (Xbox 360)|*.iso",CheckFileExists=true,Title="Selecciona tu ISO de GTA IV para Xbox 360"})if(dialog.ShowDialog(this)==DialogResult.OK)field.Text=dialog.FileName;}
                else {using(var dialog=new FolderBrowserDialog {Description="San Andreas clásico instalado en Steam",ShowNewFolderButton=false})if(dialog.ShowDialog(this)==DialogResult.OK)field.Text=dialog.SelectedPath;}
            };Controls.Add(button);
        }
        void UpdateButtons(){install.Enabled=!manifest.lab_only&&!busy&&File.Exists(iso.Text)&&File.Exists(Path.Combine(steam.Text,"gta-sa.exe"));restore.Enabled=!busy&&File.Exists(Path.Combine(steam.Text,Engine.ReceiptName));}
        void BeginWork(bool undo) {
            var target=steam.Text;var image=iso.Text;busy=true;UpdateButtons();steam.Enabled=iso.Enabled=false;status.Text=undo?"Restaurando Steam…":"Comprobando y preparando el mod…";
            DoWorkEventHandler action=null;
            action=delegate(object sender,DoWorkEventArgs e) {
                try {
                    if(undo)e.Result=new Engine(manifest.profile,Program.Resource("SteamLauncher.exe")).Restore(target);
                    else e.Result=Program.Install(manifest,target,image,Program.DefaultCache,delegate(long completed,long total,string item){
                        if((DateTime.UtcNow-lastProgress).TotalMilliseconds<150)return;lastProgress=DateTime.UtcNow;
                        BeginInvoke((Action)delegate{progress.Value=total>0?(int)Math.Min(1000,completed*1000/total):0;status.Text=item;});
                    },delegate{return worker.CancellationPending;},false,false);
                } finally {worker.DoWork-=action;}
            };
            worker.DoWork+=action;worker.RunWorkerAsync();
        }
    }
}
