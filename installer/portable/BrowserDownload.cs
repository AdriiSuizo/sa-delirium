// Normal provider-page downloads. Human validation remains visible to the user;
// this helper never solves, bypasses or conceals a provider challenge.
using System;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows.Forms;
using Microsoft.Web.WebView2.Core;
using Microsoft.Web.WebView2.WinForms;

namespace SAPortableDownload {
    public sealed class Request {
        public string title, page, destination, sha256, browser_profile, download_button_id;
        public long bytes;
        public long max_bytes;
        public bool member_pins_only;
        public bool debug_capture;
        public int timeout_seconds;
        public string[] page_hosts, download_hosts;
        public string[] auto_click_selectors;
    }
    public sealed class DownloadForm : Form {
        readonly Request request;
        readonly string report;
        readonly bool probe;
        readonly bool unattended;
        readonly WebView2 web = new WebView2();
        readonly Label status = new Label();
        bool completed, downloading;
        CoreWebView2DownloadOperation operation;
        readonly Timer actions=new Timer {Interval=1200};
        bool actionRunning;
        readonly System.Collections.Generic.HashSet<string> clicked=new System.Collections.Generic.HashSet<string>();
        protected override bool ShowWithoutActivation { get { return true; } }
        protected override CreateParams CreateParams { get {var value=base.CreateParams;value.ExStyle|=0x08000000;return value;} }
        static bool HostAllowed(string value, string[] hosts) {
            Uri uri;
            return Uri.TryCreate(value, UriKind.Absolute, out uri) && uri.Scheme == "https" &&
                hosts != null && hosts.Any(h => uri.DnsSafeHost.Equals(h, StringComparison.OrdinalIgnoreCase) ||
                    uri.DnsSafeHost.EndsWith("." + h, StringComparison.OrdinalIgnoreCase));
        }
        void Write(object result) {
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(report)));
            File.WriteAllText(report, new JavaScriptSerializer().Serialize(result));
        }
        public DownloadForm(Request value, string result, bool inspect, bool automaticTest) {
            request = value; report = result; probe = inspect; unattended = automaticTest;
            Text = "SA Delirium · " + request.title;
            ClientSize = new Size(1060, 760);
            Font = new Font("Segoe UI", 10);
            var instructions = new Label { Dock = DockStyle.Top, Height = 65, Padding = new Padding(14),
                Text = "Descarga original: " + request.title + ". Pulsa el botón de descarga de su autor.\n" +
                    "Si el proveedor pide una validación, complétala aquí. El asistente guardará y comprobará el archivo." };
            status.Dock = DockStyle.Bottom; status.Height = 44; status.Padding = new Padding(14);
            status.Text = "Abriendo la página oficial…";
            web.Dock = DockStyle.Fill;
            Controls.Add(web); Controls.Add(status); Controls.Add(instructions);
            if (probe || unattended) { StartPosition = FormStartPosition.Manual; Location = new Point(-32000,-32000); ShowInTaskbar = false; }
            else StartPosition = FormStartPosition.CenterParent;
            Shown += async delegate { await Initialize(); };
            actions.Tick+=async delegate {await OrdinaryDownloadAction();};
            FormClosing += delegate {
                actions.Stop();
                if (operation != null && operation.State == CoreWebView2DownloadState.InProgress) operation.Cancel();
                if (!completed) Write(new { status = "cancelled", downloaded = false });
            };
        }
        async Task Initialize() {
            try {
                if (!HostAllowed(request.page, request.page_hosts)) throw new IOException("Página no permitida.");
                if ((request.bytes <= 0 && (!request.member_pins_only || request.max_bytes<=0)) ||
                    (!request.member_pins_only && (request.sha256 == null || request.sha256.Length != 64)) ||
                    String.IsNullOrWhiteSpace(request.browser_profile)) throw new IOException("Manifiesto de descarga incompleto.");
                request.destination = Path.GetFullPath(request.destination);
                if (File.Exists(request.destination) || File.Exists(request.destination + ".partial"))
                    throw new IOException("La ruta de descarga ya existe.");
                Directory.CreateDirectory(Path.GetDirectoryName(request.destination));
                var environment = await CoreWebView2Environment.CreateAsync(null, request.browser_profile);
                await web.EnsureCoreWebView2Async(environment);
                var core = web.CoreWebView2;
                core.Settings.AreDevToolsEnabled = false;
                core.Settings.AreDefaultContextMenusEnabled = false;
                // The ordinary consent dialog can live in a provider iframe.
                // Reject optional tracking there as well as in the main page;
                // leave authentication and challenge frames untouched.
                await core.AddScriptToExecuteOnDocumentCreatedAsync(@"(function(){
                    var count=0;var timer=setInterval(function(){
                        if(++count>150){clearInterval(timer);return;}
                        if(!document.body||!/privacy|consent|cookies/i.test(document.body.innerText))return;
                        var b=Array.from(document.querySelectorAll('button,input[type=button],[role=button]')).find(function(b){
                            var r=b.getBoundingClientRect();return r.width>0&&r.height>0&&!b.disabled&&/^(reject all|do not consent|rechazar todo|alle ablehnen|alles ablehnen)$/i.test((b.innerText||b.value||'').trim());
                        });
                        if(b){var r=b.getBoundingClientRect();var t=document.elementFromPoint(r.left+r.width/2,r.top+r.height/2);if(t&&(t===b||b.contains(t))){b.click();clearInterval(timer);}}
                    },1200);
                })()");
                core.PermissionRequested += delegate(object s, CoreWebView2PermissionRequestedEventArgs e) { e.State = CoreWebView2PermissionState.Deny; };
                core.NavigationStarting += delegate(object s, CoreWebView2NavigationStartingEventArgs e) {
                    if (!HostAllowed(e.Uri, request.page_hosts) && !HostAllowed(e.Uri, request.download_hosts)) {
                        e.Cancel = true; status.Text = "Se ha bloqueado una página ajena a esta descarga.";
                    }
                };
                core.NewWindowRequested += delegate(object s, CoreWebView2NewWindowRequestedEventArgs e) {
                    e.Handled = true;
                    if (HostAllowed(e.Uri, request.page_hosts) || HostAllowed(e.Uri, request.download_hosts)) core.Navigate(e.Uri);
                };
                core.DownloadStarting += DownloadStarting;
                core.Navigate(request.page);
                if(!probe)actions.Start();
                if (unattended) {
                    await Task.Delay(Math.Max(30,Math.Min(180,request.timeout_seconds>0?request.timeout_seconds:90))*1000);
                    if (!downloading && !completed) {
                        if(request.debug_capture) {
                            var page=await core.ExecuteScriptAsync("({url:location.href,title:document.title,text:document.body.innerText.slice(0,8000),controls:Array.from(document.querySelectorAll('button,a,input,[role=button],[onclick]')).filter(b=>/reject|download|captcha/i.test(b.innerText||b.value||'')).map(b=>({tag:b.tagName,id:b.id,text:(b.innerText||b.value||'').slice(0,90),class:b.className})).slice(0,45)})");
                            File.WriteAllText(report+".page.json",new JavaScriptSerializer().Serialize(new {document=page,actions=clicked.ToArray(),status=status.Text}));
                            using(var file=File.Create(report+".png"))await core.CapturePreviewAsync(CoreWebView2CapturePreviewImageFormat.Png,file);
                        }
                        Fail("original_download_needs_interaction"); return;
                    }
                }
                if (probe) {
                    await Task.Delay(18000);
                    var document = await core.ExecuteScriptAsync("JSON.stringify({title:document.title,url:location.href,text:document.body?document.body.innerText.slice(0,4500):'',links:Array.from(document.querySelectorAll('a[href]')).map(a=>({text:a.innerText.slice(0,80),href:a.href})).filter(a=>a.text.toLowerCase().includes('download')).slice(0,20)})");
                    using (var stream = File.Create(report + ".png")) await core.CapturePreviewAsync(CoreWebView2CapturePreviewImageFormat.Png, stream);
                    Write(new { status = "page_observed_no_interaction", document_json = document });
                    completed = true; Close();
                }
            } catch (Exception error) { Fail(error.Message); }
        }
        async Task OrdinaryDownloadAction() {
            if(probe || completed || downloading || actionRunning || web.CoreWebView2==null)return;
            if(!HostAllowed(web.CoreWebView2.Source,request.page_hosts))return;
            actionRunning=true;
            try {
                var json=new JavaScriptSerializer();
                var selectors=json.Serialize(request.auto_click_selectors??new string[0]);
                var buttonId=json.Serialize(request.download_button_id);
                // Only visible ordinary UI controls, with hit testing. This does
                // not inspect captcha frames/tokens or manufacture trusted events.
                var script=@"(function(){
                    if(/wrong captcha|verify you are human|complete the captcha/i.test(document.body.innerText))return {action:'human_validation_required'};
                    function visible(b){var r=b.getBoundingClientRect();return r.width>0&&r.height>0&&!b.disabled&&getComputedStyle(b).visibility!=='hidden';}
                    function hit(b){var r=b.getBoundingClientRect();var x=r.left+r.width/2,y=r.top+r.height/2;var t=document.elementFromPoint(x,y);return x>=0&&y>=0&&x<innerWidth&&y<innerHeight&&t&&(t===b||b.contains(t));}
                    var reject=Array.from(document.querySelectorAll('button,input[type=button],a,[role=button],[onclick],.cmpboxbtnno')).find(b=>visible(b)&&/^(reject all|do not consent|rechazar todo|alle ablehnen|alles ablehnen)$/i.test((b.innerText||b.value||'').trim()));
                    if(reject&&hit(reject)&&/cookies|vendors|privacy|consent/i.test(document.body.innerText)){reject.click();return {action:'declined_optional_cookies'};}
                    var close=document.querySelector('#fullscreenAdOverlay #closeAdButton.socad-close-button');
                    if(close&&visible(close)&&hit(close)&&close.innerText.trim()==='×'){close.click();return {action:'closed_social_promotion'};}
                    window.__saSetupClicked=window.__saSetupClicked||new WeakSet();
                    window.__saSetupNodeIds=window.__saSetupNodeIds||new WeakMap();
                    window.__saSetupNextNodeId=window.__saSetupNextNodeId||1;
                    var candidates=[];var id=BUTTON_ID;if(id){var byId=document.getElementById(id);if(byId)candidates.push(byId);}
                    SELECTORS.forEach(s=>document.querySelectorAll(s).forEach(b=>candidates.push(b)));
                    function downloadLabel(b){
                        var lines=(b.innerText||b.value||'').trim().split(/\n+/).map(s=>s.trim()).filter(Boolean);
                        return lines.length>0&&/^(?:download(?:\s*\[?[\d.,]+\s*(?:kb|mb|gb)\]?)?|create download link|start download|download file|скачать|descargar)$/i.test(lines[0])&&
                            lines.slice(1).every(s=>/^(?:(?:size:\s*)?[\d.,]+\s*(?:kb|mb|gb)|password:\s*libertycity)$/i.test(s));
                    }
                    var b=candidates.find(b=>visible(b)&&downloadLabel(b)&&!window.__saSetupClicked.has(b));if(!b)return null;
                    if(!hit(b)){b.scrollIntoView({block:'center'});return null;}
                    if(!window.__saSetupNodeIds.has(b))window.__saSetupNodeIds.set(b,window.__saSetupNextNodeId++);
                    window.__saSetupDownloadCandidate=b;return {action:'candidate',node_id:window.__saSetupNodeIds.get(b),tag:b.tagName,text:(b.innerText||b.value||'').trim().slice(0,100),href:b.href||null,form:b.form?b.form.action:null};
                })()".Replace("BUTTON_ID",buttonId).Replace("SELECTORS",selectors);
                var raw=await web.CoreWebView2.ExecuteScriptAsync(script);
                var candidate=json.Deserialize<System.Collections.Generic.Dictionary<string,object>>(raw);
                if(candidate!=null && (string)candidate["action"]=="human_validation_required") {
                    actions.Stop();status.Text="El proveedor solicita una validación. Completa la descarga en esta página para continuar.";return;
                }
                if(candidate==null || (string)candidate["action"]!="candidate")return;
                var href=candidate["href"] as string;var form=candidate["form"] as string;
                if(!String.IsNullOrEmpty(href) && !HostAllowed(href,request.download_hosts) && !HostAllowed(href,request.page_hosts))return;
                if(!String.IsNullOrEmpty(form) && !HostAllowed(form,request.page_hosts))return;
                var key=web.CoreWebView2.Source+"|"+candidate["node_id"]+"|"+candidate["tag"]+"|"+candidate["text"]+"|"+href;
                if(!clicked.Add(key)) {
                    await web.CoreWebView2.ExecuteScriptAsync("if(window.__saSetupDownloadCandidate)window.__saSetupClicked.add(window.__saSetupDownloadCandidate)");
                    return;
                }
                var expectedHref=json.Serialize(href);
                var expectedForm=json.Serialize(form);
                var expectedText=json.Serialize(candidate["text"]);
                var didClick=await web.CoreWebView2.ExecuteScriptAsync("(function(){var b=window.__saSetupDownloadCandidate;if(b&&b.isConnected&&(b.innerText||b.value||'').trim().slice(0,100)==="+expectedText+"&&(b.href||null)==="+expectedHref+"&&(b.form?b.form.action:null)==="+expectedForm+"){window.__saSetupClicked.add(b);b.click();return true;}return false;})()");
                if(didClick!="true")clicked.Remove(key);
            } catch(Exception error) {
                status.Text="Puedes continuar la descarga desde esta página. "+error.Message;
            } finally {actionRunning=false;}
        }
        void DownloadStarting(object sender, CoreWebView2DownloadStartingEventArgs e) {
            if (probe || downloading || !HostAllowed(e.DownloadOperation.Uri, request.download_hosts) ||
                (e.DownloadOperation.TotalBytesToReceive.HasValue &&
                    (request.bytes>0?e.DownloadOperation.TotalBytesToReceive.Value!=(ulong)request.bytes:
                    e.DownloadOperation.TotalBytesToReceive.Value>(ulong)request.max_bytes))) {
                e.Cancel = true; status.Text = "Esta descarga no coincide con el paquete esperado."; return;
            }
            downloading = true; operation = e.DownloadOperation;
            e.ResultFilePath = request.destination + ".partial";
            e.Handled = true;
            operation.BytesReceivedChanged += delegate {
                if (request.bytes<=0 && operation.BytesReceived>request.max_bytes) { operation.Cancel(); return; }
                status.Text = request.bytes>0?String.Format("Descargando {0}: {1:N1} / {2:N1} MiB", request.title,
                    operation.BytesReceived / 1048576.0, request.bytes / 1048576.0):String.Format("Descargando {0}: {1:N1} MiB",request.title,operation.BytesReceived/1048576.0);
            };
            operation.StateChanged += async delegate {
                if (operation.State == CoreWebView2DownloadState.Interrupted) {
                    var reason=operation.InterruptReason.ToString();
                    if(reason=="FileMalicious" || reason=="FileVirusInfected") {
                        actions.Stop();web.Enabled=false;
                        completed=true;Write(new{status="security_blocked",error="Windows ha bloqueado esta descarga por seguridad. El archivo no se instalará.",reason});
                        if(!probe&&!unattended)MessageBox.Show(this,"Windows ha bloqueado esta descarga por seguridad. La instalación se ha detenido.","Descarga bloqueada",MessageBoxButtons.OK,MessageBoxIcon.Error);
                        Close();return;
                    }
                    Fail("Descarga interrumpida: " + operation.InterruptReason); return;
                }
                if (operation.State != CoreWebView2DownloadState.Completed) return;
                status.Text = "Comprobando el paquete original…";
                try {
                    var partial = request.destination + ".partial";
                    var digest = await Task.Run(delegate {
                        long size=new FileInfo(partial).Length;
                        if (request.bytes>0?size!=request.bytes:size>request.max_bytes) throw new IOException("Tamaño distinto del original.");
                        using (var file = File.OpenRead(partial)) using (var hash = SHA256.Create())
                            return BitConverter.ToString(hash.ComputeHash(file)).Replace("-", "").ToLowerInvariant();
                    });
                    if (!String.IsNullOrEmpty(request.sha256) && !digest.Equals(request.sha256, StringComparison.OrdinalIgnoreCase)) throw new IOException("SHA256 distinto del original; no se instalará este archivo.");
                    File.Move(partial, request.destination);
                    Write(new { status = String.IsNullOrEmpty(request.sha256)?"archive_received_requires_member_verification":"verified", sha256 = digest, bytes = new FileInfo(request.destination).Length });
                    completed = true; DialogResult = DialogResult.OK; Close();
                } catch (Exception error) { Fail(error.Message); }
            };
        }
        void Fail(string message) {
            completed = true; Write(new { status = "failed", error = message });
            status.Text = message;
            if (probe || unattended) Close();
            else MessageBox.Show(this, message, "Descarga original", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }
    static class Program {
        [STAThread] static int Main(string[] args) {
            if (args.Length < 2 || args.Length > 3) return 2;
            try {
                var request = new JavaScriptSerializer().Deserialize<Request>(File.ReadAllText(args[0]));
                Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
                using (var form = new DownloadForm(request, args[1], args.Length == 3 && args[2] == "--probe", args.Length == 3 && args[2] == "--unattended")) Application.Run(form);
                return File.Exists(request.destination) ? 0 : 1;
            } catch (Exception error) { File.WriteAllText(args[1], new JavaScriptSerializer().Serialize(new { status = "failed", error = error.Message })); return 1; }
        }
    }
}
