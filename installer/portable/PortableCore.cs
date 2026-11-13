using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using SASetup;
using Microsoft.Win32;

namespace SAPortable {
    public sealed class Package {
        public string id, title, filename, url, resolver, sha256, format, parent, member, password;
        public bool optional, opt_in;
        public string[] page_hosts, download_hosts;
        public string[] auto_click_selectors;
        public string download_button_id;
        public long bytes;
        public long max_bytes;
        public bool member_pins_only;
        public bool security_blocked;
    }
    public sealed class Recipe {
        public string path, sha256, kind, source, source_sha256, package, member, patch;
        public long bytes;
        public long source_bytes;
        public string[] components;
        // Other verified originals for a steam or steam_delta recipe: the
        // same game file as another edition ships it (PC 1.0, German R2...).
        // With a patch it reaches the recipe output; without one it is used
        // unchanged and its own hash becomes the verified output.
        public RecipeSource[] alternatives;
    }
    public sealed class RecipeSource {
        public string name, source, source_sha256, patch, sha256;
        public long source_bytes, bytes;
    }
    public static class Originals {
        internal const string VorbisFile = "vorbisFile.dll";
        internal static readonly string[] VorbisAliases = { "vorbisHooked.dll", "vorbisFileHooked.dll" };

        // Every accepted original of a game-file recipe, primary first,
        // with source, patch and verified output made explicit.
        public static List<RecipeSource> Candidates(Recipe f) {
            var list=new List<RecipeSource>();
            if(f.kind!="steam" && f.kind!="steam_delta")return list;
            bool delta=f.kind=="steam_delta";
            list.Add(new RecipeSource{name="",source=f.source,source_sha256=delta?f.source_sha256:f.sha256,source_bytes=delta?f.source_bytes:f.bytes,
                patch=delta?f.patch:null,sha256=f.sha256,bytes=f.bytes});
            if(f.alternatives!=null)foreach(var a in f.alternatives) {
                bool patched=!String.IsNullOrEmpty(a.patch);
                list.Add(new RecipeSource{name=a.name,source=String.IsNullOrEmpty(a.source)?f.source:a.source,source_sha256=a.source_sha256,source_bytes=a.source_bytes,
                    patch=patched?a.patch:null,
                    sha256=!String.IsNullOrEmpty(a.sha256)?a.sha256:patched?f.sha256:a.source_sha256,
                    bytes=a.bytes>0?a.bytes:patched?f.bytes:a.source_bytes});
            }
            return list;
        }
    }
    public enum SetupPhase { Validation, Packages, Files, Resources, Install, Restore }

    public sealed class Manifest {
        public int schema;
        public string version;
        public bool lab_only;
        public bool unified;
        public bool choose_compatible_renderer;
        public bool prepare_legacy_directx;
        public string tools_sha256;
        public ProfileIndex profile;
        public Package[] packages;
        public Recipe[] files;
        public string[] omissions;
        public string webview_bootstrapper_package;
    }
    public sealed class Composer {
        public readonly Manifest Manifest;
        readonly string tools, cache;
        readonly Dictionary<string,string> resolved = new Dictionary<string,string>();
        readonly HashSet<string> resolving = new HashSet<string>();
        readonly Dictionary<string,string> archivePasswords = new Dictionary<string,string>();
        public Action<long,long,string> Progress = delegate {};
        public Action<SetupPhase> Phase = delegate {};
        public Func<bool> Cancelled = delegate {return false;};
        public bool Unattended, HdModels;
        public readonly List<string> Skipped=new List<string>();
        void CheckCancellation() { if (Cancelled()) throw new OperationCanceledException(); }
        public Composer(Manifest manifest, string helperDirectory, string cacheDirectory) {
            Manifest=manifest;tools=Paths.Full(helperDirectory);cache=Paths.Full(cacheDirectory);
            Paths.NoLinks(tools);Paths.NoLinks(cache);Editions.Complete(manifest);
            if (manifest.schema!=1 || manifest.files==null || manifest.packages==null || manifest.profile==null)
                throw new IOException("Manifiesto portátil incompleto.");
            var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach(var f in manifest.files) {
                Paths.Inside(cache,f.path);
                if(!names.Add(f.path) || f.bytes<0 || !Regex.IsMatch(f.sha256??"","^[a-f0-9]{64}$")) throw new IOException("Receta no válida.");
                if(!new[]{"steam","steam_delta","package","package_delta","payload"}.Contains(f.kind))throw new IOException("Tipo de receta no válido.");
                if(f.kind.EndsWith("_delta",StringComparison.Ordinal)) {
                    Paths.Inside(tools,"patches/"+f.patch);
                    if(!Regex.IsMatch(f.source_sha256??"","^[a-f0-9]{64}$") || f.source_bytes<0)throw new IOException("Parche sin origen verificado.");
                }
                if(f.alternatives!=null) {
                    if(f.kind!="steam" && f.kind!="steam_delta")throw new IOException("Solo los archivos del juego admiten originales alternativos.");
                    var candidates=Originals.Candidates(f);
                    foreach(var a in candidates) {
                        Paths.Inside(cache,a.source);
                        if(a.patch!=null)Paths.Inside(tools,"patches/"+a.patch);
                        if(!Regex.IsMatch(a.source_sha256??"","^[a-f0-9]{64}$") || !Regex.IsMatch(a.sha256??"","^[a-f0-9]{64}$") || a.source_bytes<0 || a.bytes<0)
                            throw new IOException("Original alternativo sin huella verificada.");
                    }
                    if(candidates.Select(a=>a.source.ToLowerInvariant()+"|"+a.source_sha256).Distinct().Count()!=candidates.Count)throw new IOException("Original alternativo repetido.");
                }
                if(f.kind.StartsWith("steam",StringComparison.Ordinal) || f.kind=="payload")Paths.Inside(cache,f.source);
                if(f.path.StartsWith("modloader/RoSA ",StringComparison.OrdinalIgnoreCase) ||
                    f.path.StartsWith("modloader/Proper Player Retex/",StringComparison.OrdinalIgnoreCase) ||
                    f.path.IndexOf("V_HUD_by_DK22Pac",StringComparison.OrdinalIgnoreCase)>=0)
                    throw new IOException("El manifiesto incluye un componente retirado.");
            }
            if(manifest.packages.Select(p=>p.id).Distinct().Count()!=manifest.packages.Length) throw new IOException("Paquetes repetidos.");
            if(manifest.packages.Any(p=>p.security_blocked&&manifest.files.Any(f=>f.package==p.id)))
                throw new IOException("El paquete incluye un componente pendiente de revisión de seguridad. No se iniciará la instalación.");
        }
        static string Quote(string value) {
            if(value.Contains('"') || value.EndsWith("\\")) throw new IOException("Argumento de herramienta no válido.");
            return "\""+value+"\"";
        }
        static Exception ToolFailure(Exception error, string log, string operation, int? exitCode = null, string detail = null) {
            error.Data["SetupLog"] = log;
            error.Data["SetupTool"] = operation;
            if (exitCode.HasValue) error.Data["SetupExitCode"] = exitCode.Value;
            if (detail != null) error.Data["SetupDetail"] = detail;
            return error;
        }
        void Run(string filename, string arguments, string log, string operation, bool allowRestart=false, int timeoutMilliseconds=0, bool showToolError=false) {
            CheckCancellation();
            var start = new ProcessStartInfo(filename,arguments) {UseShellExecute=false,CreateNoWindow=true,
                WorkingDirectory=Path.GetDirectoryName(Path.GetFullPath(log)),
                RedirectStandardOutput=true,RedirectStandardError=true,StandardOutputEncoding=Encoding.UTF8,StandardErrorEncoding=Encoding.UTF8};
            using(var output=new StreamWriter(log,false,Encoding.UTF8)) using(var process=new Process {StartInfo=start}) {
                var failure="Falló "+operation+".";
                var logLocation="\r\n\r\nRegistro: "+log;
                try {process.Start();}
                catch(System.ComponentModel.Win32Exception error) {
                    output.WriteLine(error.Message);
                    throw ToolFailure(new System.ComponentModel.Win32Exception(error.NativeErrorCode,failure+" "+error.Message+logLocation),log,operation);
                }
                var watch=Stopwatch.StartNew();
                object sync=new object();
                string toolError=null;
                DataReceivedEventHandler receive=delegate(object s,DataReceivedEventArgs e){if(e.Data!=null)lock(sync)output.WriteLine(e.Data);};
                process.OutputDataReceived+=receive;
                process.ErrorDataReceived+=delegate(object s,DataReceivedEventArgs e){
                    receive(s,e);
                    if(showToolError && !String.IsNullOrWhiteSpace(e.Data))toolError=e.Data;
                };
                process.BeginOutputReadLine();process.BeginErrorReadLine();
                while(!process.WaitForExit(200)) {
                    if(Cancelled()) {process.Kill();process.WaitForExit();throw new OperationCanceledException();}
                    if(timeoutMilliseconds>0&&watch.ElapsedMilliseconds>timeoutMilliseconds){process.Kill();process.WaitForExit();throw ToolFailure(new TimeoutException("Tiempo agotado en "+operation+"."+logLocation),log,operation);}
                }
                process.WaitForExit();
                if(process.ExitCode!=0 && !(allowRestart&&process.ExitCode==3010)) {
                    var detail=toolError==null?"":"\r\n"+toolError.Substring(0,Math.Min(toolError.Length,400));
                    throw ToolFailure(new IOException(failure+" Código "+process.ExitCode+"."+detail+logLocation),log,operation,process.ExitCode,toolError);
                }
            }
        }
        void Verify(string path,string hash,long size=-1) {
            Paths.NoLinks(path);
            if(!File.Exists(path) || (size>=0 && new FileInfo(path).Length!=size) || Engine.Hash(path)!=hash)
                throw new IOException("El archivo no coincide con su versión verificada: "+Path.GetFileName(path));
        }
        string Resolve(string id) {
            string ready;
            if(resolved.TryGetValue(id,out ready))return ready;
            if(!resolving.Add(id))throw new IOException("Dependencia circular de paquetes.");
            var p=Manifest.packages.Single(x=>x.id==id);
            if(String.IsNullOrEmpty(p.sha256)?!p.member_pins_only || p.max_bytes<=0:p.sha256.Length!=64 || p.bytes<=0)
                throw new IOException("Paquete sin huellas verificadas: "+id);
            var identity=p.sha256??Engine.Hash(Encoding.UTF8.GetBytes(Json.Serializer().Serialize(p)+Json.Serializer().Serialize(Manifest.files.Where(f=>f.package==p.id).OrderBy(f=>f.path).ToArray())));
            var directory=Paths.Inside(cache,"downloads/"+identity);
            Directory.CreateDirectory(directory);
            var path=Paths.Inside(directory,p.filename);
            if(File.Exists(path))VerifyArchive(path,p);
            else if(p.resolver=="bundled") {
                var asset=Paths.Inside(tools,"packages/"+p.sha256+"/"+p.filename);
                Verify(asset,p.sha256,p.bytes);File.Copy(asset,path);
            }
            else if(!String.IsNullOrEmpty(p.parent)) {
                var parent=Resolve(p.parent);
                var extraction=Extract(parent,new[]{p.member},"nested-"+p.sha256);
                var member=Paths.Inside(extraction,p.member);Verify(member,p.sha256,p.bytes);
                File.Copy(member,path);
            } else if(p.resolver=="browser") {
                EnsureBrowserRuntime();
                var report=Paths.Inside(directory,"browser-report.json");
                var request=Paths.Inside(directory,"browser-request.json");
                Json.Write(request,new {title=p.title??p.id,page=p.url,destination=path,p.sha256,p.bytes,p.max_bytes,p.member_pins_only,p.page_hosts,p.download_hosts,p.download_button_id,p.auto_click_selectors,
                    browser_profile=Paths.Inside(cache,"browser-profile")});
                try {Run(Path.Combine(tools,"BrowserDownload.exe"),Quote(request)+" "+Quote(report)+(Unattended?" --unattended":""),Paths.Inside(directory,"browser.log"),"la descarga de "+(p.title??p.id));}
                catch(IOException) {
                    if(File.Exists(report)) {
                        var result=Json.Read<Dictionary<string,object>>(report);
                        if(result.ContainsKey("status")&&(string)result["status"]=="security_blocked")
                            throw new IOException("Windows ha bloqueado una descarga por seguridad. La instalación se ha detenido: "+(p.title??p.id));
                    }
                    throw;
                }
                var status=Json.Read<Dictionary<string,object>>(report);
                if(!status.ContainsKey("status") || ((string)status["status"]!="verified" && !(p.member_pins_only && (string)status["status"]=="archive_received_requires_member_verification")))throw new IOException("La descarga original necesita atención: "+p.id);
                VerifyArchive(path,p);
            } else {
                Uri url;
                if(!Uri.TryCreate(p.url,UriKind.Absolute,out url) || url.Scheme!="https")throw new IOException("No hay una distribución original válida: "+p.id);
                var partial=path+".partial-"+Guid.NewGuid().ToString("N");
                try {
                    var request=(HttpWebRequest)WebRequest.Create(url);
                    request.UserAgent="SA-Delirium-Setup/0.3";request.Timeout=120000;request.ReadWriteTimeout=120000;
                    ServicePointManager.SecurityProtocol=SecurityProtocolType.Tls12;
                    using(var response=(HttpWebResponse)request.GetResponse()) {
                        if(response.ResponseUri.Scheme!="https" || (response.ContentType??"").StartsWith("text/html",StringComparison.OrdinalIgnoreCase))
                            throw new IOException("El proveedor ha devuelto una página: "+p.id);
                        using(var input=response.GetResponseStream()) using(var output=new FileStream(partial,FileMode.CreateNew,FileAccess.Write,FileShare.None)) {
                            var buffer=new byte[1024*1024];int count;long total=0;
                            while((count=input.Read(buffer,0,buffer.Length))!=0){CheckCancellation();total+=count;if(total>(p.bytes>0?p.bytes:p.max_bytes))throw new IOException("Descarga mayor de lo esperado.");output.Write(buffer,0,count);Progress(total,p.bytes,"Descargando "+(p.title??p.id));}
                            output.Flush(true);
                        }
                    }
                    VerifyArchive(partial,p);File.Move(partial,path);
                } finally { if(File.Exists(partial))File.Delete(partial); }
            }
            if(!String.IsNullOrEmpty(p.password))archivePasswords[path]=p.password;
            resolving.Remove(id);resolved[id]=path;return path;
        }
        static bool BrowserRuntimePresent() {
            // Microsoft's documented Evergreen product key, both install modes.
            // https://learn.microsoft.com/microsoft-edge/webview2/concepts/distribution
            foreach(var hive in new[]{RegistryHive.CurrentUser,RegistryHive.LocalMachine})
                foreach(var view in new[]{RegistryView.Registry32,RegistryView.Registry64})
                    using(var registry=RegistryKey.OpenBaseKey(hive,view))
                    using(var key=registry.OpenSubKey(@"Software\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}")) {
                        Version version;
                        if(key!=null && Version.TryParse(key.GetValue("pv") as string,out version) && version>new Version(0,0,0,0))return true;
                    }
            return false;
        }
        void EnsureBrowserRuntime() {
            if(BrowserRuntimePresent())return;
            var id=Manifest.webview_bootstrapper_package;
            var package=Manifest.packages.SingleOrDefault(p=>p.id==id);
            if(package==null || String.IsNullOrEmpty(package.sha256) || package.member_pins_only || package.resolver=="browser")
                throw new IOException("Falta el instalador verificado del componente de descargas.");
            Progress(0,0,"Preparando el componente de descargas de Microsoft…");
            var installer=Resolve(id);Verify(installer,package.sha256,package.bytes);
            var log=Paths.Inside(cache,"webview2-runtime-install.log");
            Run(installer,"/silent /install",log,"la instalación de WebView2",true);
            if(!BrowserRuntimePresent())throw new IOException("No se ha podido preparar el componente de descargas de Microsoft. Registro: "+log);
        }
        void VerifyArchive(string path,Package p) {
            if(!String.IsNullOrEmpty(p.sha256)){Verify(path,p.sha256,p.bytes);return;}
            Paths.NoLinks(path);
            if(!p.member_pins_only || !File.Exists(path) || new FileInfo(path).Length<=0 || new FileInfo(path).Length>p.max_bytes)
                throw new IOException("Archivo original fuera de los límites previstos.");
            // This is only an acquired container. Every selected input below
            // must still match its immutable SHA256 before any DLL is installed.
        }
        sealed class ArchiveMember {public string path;public long bytes;public bool link;}
        string PasswordArgument(string archive) {
            string value;
            return archivePasswords.TryGetValue(archive,out value)?" -p"+Quote(value):" -p-";
        }
        List<ArchiveMember> ArchiveMembers(string archive) {
            var control=Paths.Inside(cache,"extraction-control");Directory.CreateDirectory(control);
            var log=Paths.Inside(control,Guid.NewGuid().ToString("N")+".listing.txt");
            Run(Path.Combine(tools,"7z.exe"),"l -slt -sccUTF-8"+PasswordArgument(archive)+" "+Quote(archive),log,"la lectura del paquete "+Path.GetFileName(archive));
            if(new FileInfo(log).Length>64L*1024*1024)throw new IOException("Índice de archivo demasiado grande.");
            var result=new List<ArchiveMember>();var fields=new Dictionary<string,string>();bool started=false;
            Action finish=delegate {
                string name,size,attributes,folder;
                if(fields.TryGetValue("Path",out name) && fields.TryGetValue("Size",out size) &&
                    !(fields.TryGetValue("Folder",out folder)&&folder=="+") &&
                    !(fields.TryGetValue("Attributes",out attributes)&&attributes.StartsWith("D",StringComparison.OrdinalIgnoreCase))) {
                    long count;if(!Int64.TryParse(size,out count)||count<0)throw new IOException("Tamaño de archivo no válido.");
                    bool link=fields.Any(k=>((k.Key=="Symbolic Link" || k.Key=="Hard Link")&&!String.IsNullOrEmpty(k.Value)) ||
                        (k.Key=="Characteristics"&&Regex.IsMatch(k.Value,"SymLink|Symbolic Link|HardLink|Hard Link",RegexOptions.IgnoreCase)));
                    string mode;if(fields.TryGetValue("Mode",out mode)&&mode.StartsWith("l",StringComparison.Ordinal))link=true;
                    if(fields.TryGetValue("Attributes",out attributes)&&Regex.IsMatch(attributes,@"(?:^|\s)l[rwxstST-]{9}(?:\s|$)"))link=true;
                    result.Add(new ArchiveMember {path=name.Replace('\\','/'),bytes=count,link=link});
                    if(result.Count>200000)throw new IOException("Demasiados miembros en el archivo.");
                }
                fields.Clear();
            };
            foreach(var line in File.ReadLines(log,Encoding.UTF8)) {
                if(line.Trim()=="----------"){started=true;continue;}if(!started)continue;
                if(String.IsNullOrWhiteSpace(line)){finish();continue;}
                int at=line.IndexOf(" = ",StringComparison.Ordinal);if(at>0)fields[line.Substring(0,at)]=line.Substring(at+3);
            }
            finish();if(!started)throw new IOException("Índice de archivo no reconocido.");return result;
        }
        string Extract(string archive,IEnumerable<string> names,string label) {
            var root=Paths.Inside(cache,"extract/"+label+"-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(root);
            var selected=names.Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
            foreach(var name in selected)Paths.Inside(root,name);
            var members=ArchiveMembers(archive);
            foreach(var name in selected) {
                var matches=members.Where(x=>x.path.Equals(name,StringComparison.OrdinalIgnoreCase)).ToArray();
                if(matches.Length!=1 || matches[0].link)throw new IOException("Miembro de archivo ausente, repetido o enlazado: "+name);
            }
            var controls=Paths.Inside(cache,"extraction-control");Directory.CreateDirectory(controls);
            var list=Paths.Inside(controls,Guid.NewGuid().ToString("N")+".members.txt");
            File.WriteAllLines(list,selected,new UTF8Encoding(false));
            Run(Path.Combine(tools,"7z.exe"),"x -y -scsUTF-8 -sccUTF-8"+PasswordArgument(archive)+" "+Quote(archive)+" -o"+Quote(root)+" @"+Quote(list),list+".log","la extracción del paquete "+Path.GetFileName(archive));
            foreach(var name in selected)Paths.NoLinks(Paths.Inside(root,name));
            return root;
        }
        public ProfileIndex Prepare(string selection,string iso,out string preparedRoot) {
            Engine.NoRunningGame();
            var edition=Editions.Identify(selection,Manifest,null);
            if(!edition.accepted)throw new IOException(edition.message);
            var steam=edition.folder;
            iso=Path.GetFullPath(iso);Paths.NoLinks(steam);Paths.NoLinks(iso);
            if(!File.Exists(iso) || !Path.GetExtension(iso).Equals(".iso",StringComparison.OrdinalIgnoreCase))throw new IOException("Selecciona la ISO de GTA IV para Xbox 360.");
            var originals = ScanOriginals(Manifest, edition, Cancelled, Progress);
            if (originals.Missing.Count != 0) {
                bool vorbisMissing = originals.Missing.Contains(Originals.VorbisFile, StringComparer.OrdinalIgnoreCase);
                var message = vorbisMissing
                    ? "Falta una copia verificable de vorbisFile.dll. El asistente también ha buscado vorbisHooked.dll y vorbisFileHooked.dll. Verifica los archivos del juego o selecciona otra instalación."
                    : "Revisa los archivos de San Andreas. Falta un original compatible de " + originals.Missing[0] +
                        (originals.Missing.Count == 1 ? "." : " y " + (originals.Missing.Count - 1) + " más.") +
                        " Usa una copia limpia o verifica sus archivos en la tienda.";
                var error = new IOException(message);
                error.Data["SetupOriginals"] = String.Join(", ", originals.Missing.Take(20));
                throw error;
            }
            Directory.CreateDirectory(cache);
            var bootstrap=Path.Combine(tools,"sa-delirium-bootstrap.exe");
            Progress(0,0,"Comprobando la ISO de GTA IV…");
            Run(bootstrap,"--check-iso "+Quote(iso),Paths.Inside(cache,"iso-check-"+Guid.NewGuid().ToString("N")+".log"),"la comprobación de la ISO de GTA IV",showToolError:true);
            preparedRoot=Paths.Inside(cache,"prepared-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(preparedRoot);
            Phase(SetupPhase.Packages);
            var sources=new Dictionary<string,string>();
            // Urbanize comes from its author's Drive, whose daily quota runs out (07/11): the game installs without it.
            foreach(var chosen in Manifest.packages.Where(x=>x.opt_in&&!HdModels))
                Manifest.files=Manifest.files.Where(f=>f.package!=chosen.id).ToArray();
            foreach(var optional in Manifest.packages.Where(x=>x.optional&&Manifest.files.Any(f=>f.package==x.id)).ToArray()) {
                try {Resolve(optional.id);}
                catch(Exception error) {
                    if(!(error is IOException||error is System.Net.WebException))throw;
                    Manifest.files=Manifest.files.Where(f=>f.package!=optional.id).ToArray();
                    Skipped.Add(optional.title??optional.id);
                    Progress(0,0,"No se pudo descargar "+(optional.title??optional.id)+": se instala sin él.");
                }
            }
            foreach(var group in Manifest.files.Where(x=>x.kind=="package" || x.kind=="package_delta").GroupBy(x=>x.package)) {
                var package=Manifest.packages.Single(x=>x.id==group.Key);
                var archive=Resolve(group.Key);
                if(package.format=="raw") {
                    foreach(var f in group) {
                        Verify(archive,f.kind=="package"?f.sha256:f.source_sha256,f.kind=="package"?f.bytes:f.source_bytes);
                        sources.Add(f.path,archive);
                    }
                    continue;
                }
                var members=ArchiveMembers(archive);
                var candidates=new Dictionary<string,ArchiveMember[]>();
                foreach(var f in group) {
                    var name=(f.member??Path.GetFileName(f.path));
                    var expectedSize=f.kind=="package"?f.bytes:f.source_bytes;
                    var exact=members.Where(x=>x.path.Equals(name,StringComparison.OrdinalIgnoreCase)).ToArray();
                    var choices=exact.Length!=0?exact:members.Where(x=>Path.GetFileName(x.path).Equals(Path.GetFileName(name),StringComparison.OrdinalIgnoreCase) && x.bytes==expectedSize).ToArray();
                    if(choices.Length==0 || choices.Length>8 || choices.Any(x=>x.link || x.bytes!=expectedSize))
                        throw new IOException("La distribución original ha cambiado: "+f.path);
                    candidates.Add(f.path,choices);
                }
                var extraction=Extract(archive,candidates.Values.SelectMany(x=>x).Select(x=>x.path),Engine.Hash(Encoding.UTF8.GetBytes(package.id)));
                foreach(var f in group) {
                    var expected=f.kind=="package"?f.sha256:f.source_sha256;
                    var matching=candidates[f.path].Select(x=>Paths.Inside(extraction,x.path)).Where(x=>Engine.Hash(x)==expected).ToArray();
                    if(matching.Length==0)throw new IOException("El componente original no coincide: "+f.path);
                    sources.Add(f.path,matching[0]);
                }
            }
            Phase(SetupPhase.Files);
            long completed=0,total=Manifest.files.Sum(x=>x.bytes);
            var outputs=new Dictionary<string,RecipeSource>(StringComparer.OrdinalIgnoreCase);
            foreach(var f in Manifest.files) {
                CheckCancellation();var destination=Paths.Inside(preparedRoot,f.path);Directory.CreateDirectory(Path.GetDirectoryName(destination));
                string source;
                if(f.kind=="steam" || f.kind=="steam_delta") {
                    // The game folder may come from any edition: each file is
                    // matched by its measured hash, never by store or name.
                    var choice=originals.Files[f.path];outputs[f.path]=choice;
                    var executable = IsExecutableRecipe(f);
                    source = executable ? Editions.ExecutableInput(edition, preparedRoot) : Paths.Inside(steam,choice.source);
                    try {
                        if(choice.patch!=null)Run(Path.Combine(tools,"DeltaTool.exe"),"apply "+Quote(source)+" "+Quote(Paths.Inside(tools,"patches/"+choice.patch))+" "+Quote(destination),Paths.Inside(preparedRoot,"patch-"+Guid.NewGuid().ToString("N")+".log"),"la preparación de "+f.path);
                        else File.Copy(source,destination);
                    } finally {
                        if (executable && edition.NormalizeLargeAddressAware) File.Delete(source);
                    }
                    Verify(destination,choice.sha256,choice.bytes);completed+=f.bytes;Progress(completed,total,f.path);
                    continue;
                }
                switch(f.kind) {
                    case "package":case "package_delta":source=sources[f.path];break;
                    case "payload":source=Paths.Inside(tools,"payload/"+f.source);break;
                    default:throw new IOException("Receta desconocida: "+f.kind);
                }
                if(f.kind.EndsWith("_delta",StringComparison.Ordinal)) {
                    Verify(source,f.source_sha256,f.source_bytes);
                    Run(Path.Combine(tools,"DeltaTool.exe"),"apply "+Quote(source)+" "+Quote(Paths.Inside(tools,"patches/"+f.patch))+" "+Quote(destination),Paths.Inside(preparedRoot,"patch-"+Guid.NewGuid().ToString("N")+".log"),"la preparación de "+f.path);
                } else {Verify(source,f.sha256,f.bytes);File.Copy(source,destination);}
                Verify(destination,f.sha256,f.bytes);completed+=f.bytes;Progress(completed,total,f.path);
            }
            Phase(SetupPhase.Resources);
            Progress(0,0,"Preparando Delirium desde tu ISO…");
            Run(bootstrap,Quote(preparedRoot)+" --iso "+Quote(iso),Paths.Inside(preparedRoot,"iso-import.log"),"la importación desde la ISO de GTA IV",showToolError:true);
            if (Manifest.unified) ImportMeleeAudio(bootstrap, preparedRoot, iso);
            var files=Manifest.files.Select(f=>{RecipeSource o;var used=outputs.TryGetValue(f.path,out o);
                return new ProfileFile {path=f.path,sha256=used?o.sha256:f.sha256,bytes=used?o.bytes:f.bytes,components=f.components};}).ToList();
            if(Manifest.prepare_legacy_directx)EnsureLegacyRuntime(preparedRoot);
            if(Manifest.choose_compatible_renderer)SelectRenderer(preparedRoot,files);
            IndexResources(preparedRoot, files);
            if(!HdModels) {
                Progress(0,0,"Suavizando codos y rodillas de tus modelos…");
                SoftModels.Write(preparedRoot,files,x=>{Skipped.Add(x);Progress(0,0,x);});
            }
            var index=Json.Serializer().Deserialize<ProfileIndex>(Json.Serializer().Serialize(Manifest.profile));
            index.files=files.ToArray();index.total_bytes=files.Sum(f=>f.bytes);
            Json.Write(Paths.Inside(preparedRoot,"portable-preparation.json"),new{status="prepared",index,omissions=Manifest.omissions,iso_copied=false});
            return index;
        }

        public ProfileIndex PrepareSolo(EditionReport edition, string iso, Recipe[] recipes, out string prepared) {
            if (!Manifest.unified || !Solo.NativeCompatible(edition)) throw new IOException("Solo Delirium requiere un GTA SA 1.0 US con el ejecutable compatible.");
            iso = Path.GetFullPath(iso);
            Paths.NoLinks(iso);
            foreach (var path in new[] { "models/gta3.img", "data/melee.dat" })
                if (!File.Exists(Paths.Inside(edition.folder, path)))
                    throw new IOException("Solo Delirium necesita leer " + path + " de tu San Andreas.");
            var bootstrap = Paths.Inside(tools, "sa-delirium-bootstrap.exe");
            Directory.CreateDirectory(cache);
            Progress(0, 0, "Comprobando la ISO de GTA IV…");
            Run(bootstrap, "--check-iso " + Quote(iso), Paths.Inside(cache, "iso-check-" + Guid.NewGuid().ToString("N") + ".log"),
                "la comprobación de la ISO de GTA IV", showToolError:true);
            prepared = Paths.Inside(cache, "solo-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(prepared);
            Phase(SetupPhase.Packages);
            Phase(SetupPhase.Files);
            var files = new List<ProfileFile>();
            long copied = 0, total = recipes.Sum(f => f.bytes);
            foreach (var f in recipes) {
                CheckCancellation();
                var source = Paths.Inside(tools, "payload/" + f.source);
                var target = Paths.Inside(prepared, f.path);
                Directory.CreateDirectory(Path.GetDirectoryName(target));
                Verify(source, f.sha256, f.bytes);
                File.Copy(source, target);
                Verify(target, f.sha256, f.bytes);
                files.Add(new ProfileFile { path=f.path, sha256=f.sha256, bytes=f.bytes, components=f.components });
                copied += f.bytes;
                Progress(copied, total, f.path);
            }
            Phase(SetupPhase.Resources);
            Progress(0, 0, "Preparando recursos de GTA IV y animaciones de tu San Andreas…");
            Run(bootstrap, "--prepare-addon " + Quote(prepared) + " " + Quote(edition.folder) + " " + Quote(iso),
                Paths.Inside(prepared,"iso-import.log"), "la importación para Solo Delirium", showToolError:true);
            ImportMeleeAudio(bootstrap, prepared, iso);
            IndexResources(prepared, files);
            return new ProfileIndex { schema=1, profile="solo-delirium-" + Manifest.version,
                files=files.ToArray(), total_bytes=files.Sum(f => f.bytes) };
        }

        void ImportMeleeAudio(string bootstrap, string prepared, string iso) {
            var decoder = Paths.Inside(tools, "vgmstream/vgmstream-cli.exe");
            Progress(0, 0, "Importando sonidos de golpes y bate de tu ISO…");
            Run(bootstrap, "--import-melee " + Quote(prepared) + " " + Quote(iso) + " " + Quote(decoder),
                Paths.Inside(prepared,"iso-melee.log"), "la importación de sonidos de GTA IV", showToolError:true);
        }

        static void IndexResources(string root, List<ProfileFile> files) {
            var resources = Paths.Inside(root, Solo.Resources);
            if (!Directory.Exists(resources)) throw new IOException("No se ha generado el contenido de la ISO.");
            foreach (var path in Directory.GetFiles(resources, "*", SearchOption.AllDirectories)) {
                Paths.NoLinks(path);
                files.Add(new ProfileFile { path=Solo.Resources+"/"+path.Substring(resources.Length+1).Replace('\\','/'),
                    sha256=Engine.Hash(path), bytes=new FileInfo(path).Length, components=new[]{"delirium"} });
            }
        }
        internal sealed class OriginalScan {
            public readonly Dictionary<string, RecipeSource> Files = new Dictionary<string, RecipeSource>(StringComparer.OrdinalIgnoreCase);
            public readonly List<string> Missing = new List<string>();
        }

        static bool IsExecutableRecipe(Recipe file) {
            return file.path.Equals("gta_sa.exe", StringComparison.OrdinalIgnoreCase);
        }

        // These are copied verbatim from the owner's game without applying a binary
        // patch or interpreting their contents as executable code. Different retail
        // audio/configuration editions do not need identical hashes.
        // Keep executable, script, archive and patched inputs pinned to the manifest.
        internal static bool CanCopyUnrecognizedOriginal(Recipe file) {
            if (file.kind != "steam" || file.alternatives == null && String.IsNullOrEmpty(file.source))
                return false;
            var path = (file.source ?? "").Replace('\\', '/');
            return path.Equals("audio/sfx/SCRIPT", StringComparison.OrdinalIgnoreCase) ||
                   path.Equals("stream.ini", StringComparison.OrdinalIgnoreCase);
        }

        static RecipeSource UnrecognizedOriginal(Recipe file, string game) {
            if (!CanCopyUnrecognizedOriginal(file)) return null;
            var path = Paths.Inside(game, file.source);
            if (!File.Exists(path)) return null;
            Paths.NoLinks(path);
            var length = new FileInfo(path).Length;
            // Reject placeholders and absurd sizes, but do not constrain normal
            // localized or edition-specific audio banks to a single known hash.
            if (length <= 0 || length > 2L * 1024 * 1024 * 1024) return null;
            var sha256 = Engine.Hash(path);
            return new RecipeSource { name="Copia original no catalogada", source=file.source,
                source_sha256=sha256, source_bytes=length, sha256=sha256, bytes=length };
        }

        // Also used by --verify-source, which needs neither ISO nor helper binaries.
        internal static OriginalScan ScanOriginals(Manifest manifest, EditionReport edition, Func<bool> cancelled, Action<long,long,string> progress) {
            Paths.NoLinks(edition.folder);
            var result = new OriginalScan();
            var measured = new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);
            var required = manifest.files.Where(f => f.kind == "steam" || f.kind == "steam_delta").ToArray();
            for (int i = 0; i < required.Length; i++) {
                if (cancelled()) throw new OperationCanceledException();
                var file = required[i];
                progress(i, required.Length, "Comprobando " + file.source);
                var source = IsExecutableRecipe(file) ? edition.Recipe : FindOriginal(file, edition.folder, measured);
                if (source == null) source = UnrecognizedOriginal(file, edition.folder);
                if (source == null) result.Missing.Add(file.source);
                else result.Files.Add(file.path, source);
            }
            progress(required.Length, required.Length, "Archivos de San Andreas comprobados");
            return result;
        }

        static RecipeSource FindOriginal(Recipe file, string game, Dictionary<string,string> measured) {
            foreach (var candidate in Originals.Candidates(file)) {
                // ASI loaders commonly move the original library to one of
                // these names. Only reuse it when its full SHA-256 matches.
                var names = candidate.source.Equals(Originals.VorbisFile, StringComparison.OrdinalIgnoreCase)
                    ? new[] { candidate.source }.Concat(Originals.VorbisAliases)
                    : new[] { candidate.source };
                foreach (var name in names) {
                    var path = Paths.Inside(game, name);
                    if (!File.Exists(path) || new FileInfo(path).Length != candidate.source_bytes) continue;
                    string hash;
                    if (!measured.TryGetValue(path, out hash)) {
                        hash = Engine.Hash(path);
                        measured[path] = hash;
                    }
                    if (hash == candidate.source_sha256) {
                        // Candidates returns new objects, so the routing table
                        // remains untouched when we record the actual filename.
                        candidate.source = name;
                        return candidate;
                    }
                }
            }
            return null;
        }
        void SelectRenderer(string root,List<ProfileFile> files) {
            var dxvk=files.Single(f=>f.path.Equals("d3d9.dll",StringComparison.OrdinalIgnoreCase));
            var path=Paths.Inside(root,dxvk.path);Verify(path,dxvk.sha256,dxvk.bytes);
            var probe=Paths.Inside(tools,"RendererProbe.exe");
            if(!File.Exists(probe))throw new IOException("Falta la comprobación del dispositivo gráfico.");
            var log=Paths.Inside(root,"renderer-probe.log");
            bool supported=true;string reason="device_created";
            Progress(0,0,"Comprobando la compatibilidad gráfica…");
            try {Run(probe,Quote(path),log,"la comprobación del dispositivo gráfico",timeoutMilliseconds:20000);}
            catch(IOException e){supported=false;reason=e.Message;}
            catch(System.ComponentModel.Win32Exception e){supported=false;reason=e.Message;}
            catch(TimeoutException e){supported=false;reason=e.Message;}
            if(!supported) {
                var fallback=Paths.Inside(root,"optional/DXVK/d3d9.dll");Directory.CreateDirectory(Path.GetDirectoryName(fallback));
                File.Move(path,fallback);dxvk.path="optional/DXVK/d3d9.dll";
                var config=files.Single(f=>f.path=="scripts/RendererProfile.txt");
                var marker=Paths.Inside(root,config.path);File.WriteAllText(marker,"native-d3d9\n",new UTF8Encoding(false));
                config.sha256=Engine.Hash(marker);config.bytes=new FileInfo(marker).Length;
            }
            var report=Paths.Inside(root,"Documentation/renderer-selection.json");Directory.CreateDirectory(Path.GetDirectoryName(report));
            Json.Write(report,new{renderer=supported?"DXVK":"Windows Direct3D9",reason,dxvk_preserved_for_manual_configuration=!supported});
            files.Add(new ProfileFile{path="Documentation/renderer-selection.json",bytes=new FileInfo(report).Length,sha256=Engine.Hash(report),components=new[]{"render-backend"}});
        }
        void EnsureLegacyRuntime(string root) {
            var probe=Paths.Inside(tools,"RendererProbe.exe");
            var log=Paths.Inside(root,"legacy-runtime-probe.log");
            try {Run(probe,"--check-legacy-runtime",log,"la comprobación de DirectX",timeoutMilliseconds:15000);return;}
            catch(IOException) { }
            var installer=Paths.Inside(tools,"directx/DXSETUP.exe");
            Progress(0,0,"Preparando los componentes DirectX de Microsoft…");
            var start=new ProcessStartInfo(installer,"/silent"){UseShellExecute=true,Verb="runas",WorkingDirectory=Path.GetDirectoryName(installer),WindowStyle=ProcessWindowStyle.Hidden};
            // Only Microsoft's unchanged setup requests Windows elevation. The
            // application and all ordinary download/preparation steps stay in
            // the user's context. No Windows libraries are replaced manually.
            if(Unattended)throw new IOException("Faltan componentes DirectX. Ejecuta el asistente normal para que Windows solicite el permiso de instalación.");
            using(var process=Process.Start(start)) {
                process.WaitForExit();
                if(process.ExitCode!=0&&process.ExitCode!=3010)throw new IOException("Microsoft DirectX no pudo completar la instalación.");
            }
            Run(probe,"--check-legacy-runtime",log,"la comprobación de DirectX",timeoutMilliseconds:15000);
        }
    }
    // Softens the elbows and knees of the owner's own ped models: only skin
    // weights change, same size and same shape at rest. Nothing of these models
    // ships with the mod; they are written from the game being installed.
    public static class SoftModels {
        public const string Folder="modloader/Modelos Suavizados";
        static readonly int[,] Joints={{22,23,24},{32,33,34},{41,42,43},{51,52,53}};
        static readonly double[] Bands={.05,.05,.07,.07};
        sealed class Skin {public int Start,Vertices,Count,Used,MaxWeights;public long VertexData;}
        public static int Write(string root,List<ProfileFile> files,Action<string> skipped) {
            var image=Path.Combine(root,"models","gta3.img");
            if(!File.Exists(image)){skipped("Sin modelos suavizados: falta gta3.img.");return 0;}
            var folder=Paths.Inside(root,Folder);int done=0;
            using(var f=new FileStream(image,FileMode.Open,FileAccess.Read,FileShare.Read)) {
                var head=new byte[8];f.Read(head,0,8);
                if(Encoding.ASCII.GetString(head,0,4)!="VER2"){skipped("Sin modelos suavizados: gta3.img no reconocido.");return 0;}
                var directory=new byte[32*BitConverter.ToInt32(head,4)];f.Read(directory,0,directory.Length);
                for(int e=0;e+32<=directory.Length;e+=32) {
                    int end=Array.IndexOf(directory,(byte)0,e+8,24);
                    var name=Encoding.ASCII.GetString(directory,e+8,(end<0?e+32:end)-e-8);
                    if(!name.EndsWith(".dff",StringComparison.OrdinalIgnoreCase))continue;
                    long offset=BitConverter.ToUInt32(directory,e);int blocks=BitConverter.ToUInt16(directory,e+4);
                    if(blocks==0)blocks=BitConverter.ToUInt16(directory,e+6);
                    var data=new byte[blocks*2048L];f.Seek(offset*2048,SeekOrigin.Begin);f.Read(data,0,data.Length);
                    byte[] soft=null;
                    try {soft=Soften(data);}
                    catch(IndexOutOfRangeException){}
                    catch(ArgumentException){}
                    if(soft==null)continue;
                    Directory.CreateDirectory(folder);
                    var path=Path.Combine(folder,name.ToLowerInvariant());File.WriteAllBytes(path,soft);
                    files.Add(new ProfileFile{path=Folder+"/"+name.ToLowerInvariant(),sha256=Engine.Hash(path),bytes=soft.Length,components=new[]{"delirium"}});
                    done++;
                }
            }
            return done;
        }
        static void Walk(byte[] d,int a,int b,Skin geometry,List<Skin> found) {
            while(a+12<=b) {
                int kind=BitConverter.ToInt32(d,a),size=BitConverter.ToInt32(d,a+4),start=a+12,stop=start+size;
                if(size<0||stop>b||kind==0)return;
                if(kind==0xF) {
                    if(BitConverter.ToInt32(d,start)!=1)throw new ArgumentException("geometry");
                    int p=start+12,flags=BitConverter.ToInt32(d,p),triangles=BitConverter.ToInt32(d,p+4),vertices=BitConverter.ToInt32(d,p+8);p+=16;
                    if((flags&8)!=0)p+=4*vertices;
                    int sets=(flags>>16)&255;if(sets==0)sets=(flags&4)!=0?1:(flags&0x80)!=0?2:0;
                    p+=8*vertices*sets+8*triangles+16+8;
                    Walk(d,start,stop,new Skin{Vertices=vertices,VertexData=p},found);
                } else if(kind==0x10||kind==0x1A||kind==0x0E||kind==3||kind==0x14)Walk(d,start,stop,geometry,found);
                else if(kind==0x116&&geometry!=null) {
                    geometry.Start=start;geometry.Count=d[start];geometry.Used=d[start+1];geometry.MaxWeights=d[start+2];found.Add(geometry);
                }
                a=stop;
            }
        }
        static int[] BoneIds(byte[] d) {
            for(int i=0;i+32<d.Length;i++) {
                if(BitConverter.ToInt32(d,i)!=0x11E||BitConverter.ToInt32(d,i+4)<=20)continue;
                int count=BitConverter.ToInt32(d,i+20);
                if(BitConverter.ToInt32(d,i+12)!=0x100||count<=10||count>=128)continue;
                var ids=new int[count];for(int k=0;k<count;k++)ids[k]=BitConverter.ToInt32(d,i+32+12*k);
                return ids;
            }
            return null;
        }
        static double[] Vec(byte[] d,long at){return new double[]{BitConverter.ToSingle(d,(int)at),BitConverter.ToSingle(d,(int)at+4),BitConverter.ToSingle(d,(int)at+8)};}
        // Position of a bone: the translation of its inverse-bind matrix, inverted.
        static double[] BonePosition(byte[] d,int at) {
            var m=new double[3,3];for(int r=0;r<3;r++)for(int c=0;c<3;c++)m[r,c]=BitConverter.ToSingle(d,at+16*r+4*c);
            var t=Vec(d,at+48);
            double c00=m[1,1]*m[2,2]-m[1,2]*m[2,1],c01=m[0,2]*m[2,1]-m[0,1]*m[2,2],c02=m[0,1]*m[1,2]-m[0,2]*m[1,1];
            double c10=m[1,2]*m[2,0]-m[1,0]*m[2,2],c11=m[0,0]*m[2,2]-m[0,2]*m[2,0],c12=m[0,2]*m[1,0]-m[0,0]*m[1,2];
            double c20=m[1,0]*m[2,1]-m[1,1]*m[2,0],c21=m[0,1]*m[2,0]-m[0,0]*m[2,1],c22=m[0,0]*m[1,1]-m[0,1]*m[1,0];
            double det=m[0,0]*c00+m[0,1]*c10+m[0,2]*c20;
            return new double[]{-(t[0]*c00+t[1]*c10+t[2]*c20)/det,-(t[0]*c01+t[1]*c11+t[2]*c21)/det,-(t[0]*c02+t[1]*c12+t[2]*c22)/det};
        }
        public static byte[] Soften(byte[] blocks) {
            int size=BitConverter.ToInt32(blocks,4)+12;if(size<=12||size>blocks.Length)return null;
            var source=new byte[size];Array.Copy(blocks,source,size);
            var found=new List<Skin>();Walk(source,0,size,null,found);
            if(found.Count==0)return null;
            var g=found.OrderByDescending(x=>x.Vertices).First();var ids=BoneIds(source);
            if(ids==null||g.MaxWeights<2)return null;
            var output=(byte[])source.Clone();
            int indices=g.Start+4+g.Used,weights=indices+4*g.Vertices,matrices=weights+16*g.Vertices;
            int stride=g.Used!=0?64:68,first=g.Used!=0?0:4,changed=0;
            Func<int,bool> palette=x=>{if(g.Used==0)return x<g.Count;for(int k=0;k<g.Used;k++)if(source[g.Start+4+k]==x)return true;return false;};
            for(int j=0;j<4;j++) {
                int upper=Array.IndexOf(ids,Joints[j,0]),lower=Array.IndexOf(ids,Joints[j,1]),hand=Array.IndexOf(ids,Joints[j,2]);
                if(upper<0||lower<0||hand<0||!palette(upper)||!palette(lower))continue;
                var a=BonePosition(source,matrices+upper*stride+first);var el=BonePosition(source,matrices+lower*stride+first);var wr=BonePosition(source,matrices+hand*stride+first);
                double ax=wr[0]-a[0],ay=wr[1]-a[1],az=wr[2]-a[2],n=Math.Sqrt(ax*ax+ay*ay+az*az);ax/=n;ay/=n;az/=n;
                for(int i=0;i<g.Vertices;i++) {
                    bool live=false,only=true;
                    for(int k=0;k<4;k++)if(BitConverter.ToSingle(source,weights+16*i+4*k)>1e-4f) {
                        int bone=source[indices+4*i+k];live=true;if(bone!=upper&&bone!=lower)only=false;
                    }
                    if(!live||!only)continue;
                    var v=Vec(source,g.VertexData+12L*i);
                    double s=(v[0]-el[0])*ax+(v[1]-el[1])*ay+(v[2]-el[2])*az,band=Bands[j];
                    if(Math.Abs(s)>=band)continue;
                    double t=(s/band+1)/2,w=t*t*(3-2*t);
                    output[indices+4*i]=(byte)upper;output[indices+4*i+1]=(byte)lower;output[indices+4*i+2]=0;output[indices+4*i+3]=0;
                    Array.Copy(BitConverter.GetBytes((float)(1-w)),0,output,weights+16*i,4);Array.Copy(BitConverter.GetBytes((float)w),0,output,weights+16*i+4,4);
                    Array.Clear(output,weights+16*i+8,8);changed++;
                }
            }
            return changed==0?null:output;
        }
    }
}
