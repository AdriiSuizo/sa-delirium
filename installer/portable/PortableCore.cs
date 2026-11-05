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
    }
    public sealed class Manifest {
        public int schema;
        public bool lab_only;
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
        public Func<bool> Cancelled = delegate {return false;};
        public bool Unattended;
        void CheckCancellation() { if (Cancelled()) throw new OperationCanceledException(); }
        public Composer(Manifest manifest, string helperDirectory, string cacheDirectory) {
            Manifest=manifest;tools=Paths.Full(helperDirectory);cache=Paths.Full(cacheDirectory);
            Paths.NoLinks(tools);Paths.NoLinks(cache);
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
        void Run(string filename, string arguments, string log, bool allowRestart=false, int timeoutMilliseconds=0) {
            CheckCancellation();
            var start = new ProcessStartInfo(filename,arguments) {UseShellExecute=false,CreateNoWindow=true,
                WorkingDirectory=Path.GetDirectoryName(Path.GetFullPath(log)),
                RedirectStandardOutput=true,RedirectStandardError=true,StandardOutputEncoding=Encoding.UTF8,StandardErrorEncoding=Encoding.UTF8};
            using(var process=Process.Start(start)) using(var output=new StreamWriter(log,false,Encoding.UTF8)) {
                var watch=Stopwatch.StartNew();
                object sync=new object();
                DataReceivedEventHandler receive=delegate(object s,DataReceivedEventArgs e){if(e.Data!=null)lock(sync)output.WriteLine(e.Data);};
                process.OutputDataReceived+=receive;process.ErrorDataReceived+=receive;
                process.BeginOutputReadLine();process.BeginErrorReadLine();
                while(!process.WaitForExit(200)) {
                    if(Cancelled()) {process.Kill();process.WaitForExit();throw new OperationCanceledException();}
                    if(timeoutMilliseconds>0&&watch.ElapsedMilliseconds>timeoutMilliseconds){process.Kill();process.WaitForExit();throw new TimeoutException("Tiempo agotado comprobando el dispositivo gráfico.");}
                }
                process.WaitForExit();
                if(process.ExitCode!=0 && !(allowRestart&&process.ExitCode==3010)) throw new IOException("No se pudo preparar el contenido. Registro: "+log);
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
                try {Run(Path.Combine(tools,"BrowserDownload.exe"),Quote(request)+" "+Quote(report)+(Unattended?" --unattended":""),Paths.Inside(directory,"browser.log"));}
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
            Run(installer,"/silent /install",log,true);
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
            Run(Path.Combine(tools,"7z.exe"),"l -slt -sccUTF-8"+PasswordArgument(archive)+" "+Quote(archive),log);
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
            Run(Path.Combine(tools,"7z.exe"),"x -y -scsUTF-8 -sccUTF-8"+PasswordArgument(archive)+" "+Quote(archive)+" -o"+Quote(root)+" @"+Quote(list),list+".log");
            foreach(var name in selected)Paths.NoLinks(Paths.Inside(root,name));
            return root;
        }
        public ProfileIndex Prepare(string steam,string iso,out string preparedRoot) {
            Engine.NoRunningGame();
            steam=Paths.Full(steam);iso=Path.GetFullPath(iso);Paths.NoLinks(steam);Paths.NoLinks(iso);
            Verify(Paths.Inside(steam,"gta-sa.exe"),Manifest.profile.stock_steam_executable_sha256);
            if(!File.Exists(iso) || !Path.GetExtension(iso).Equals(".iso",StringComparison.OrdinalIgnoreCase))throw new IOException("Selecciona la ISO de GTA IV para Xbox 360.");
            Directory.CreateDirectory(cache);
            preparedRoot=Paths.Inside(cache,"prepared-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(preparedRoot);
            var sources=new Dictionary<string,string>();
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
            long completed=0,total=Manifest.files.Sum(x=>x.bytes);
            foreach(var f in Manifest.files) {
                CheckCancellation();var destination=Paths.Inside(preparedRoot,f.path);Directory.CreateDirectory(Path.GetDirectoryName(destination));
                string source;
                switch(f.kind) {
                    case "steam":case "steam_delta":source=Paths.Inside(steam,f.source);break;
                    case "package":case "package_delta":source=sources[f.path];break;
                    case "payload":source=Paths.Inside(tools,"payload/"+f.source);break;
                    default:throw new IOException("Receta desconocida: "+f.kind);
                }
                if(f.kind.EndsWith("_delta",StringComparison.Ordinal)) {
                    Verify(source,f.source_sha256);
                    Run(Path.Combine(tools,"DeltaTool.exe"),"apply "+Quote(source)+" "+Quote(Paths.Inside(tools,"patches/"+f.patch))+" "+Quote(destination),Paths.Inside(preparedRoot,"patch-"+Guid.NewGuid().ToString("N")+".log"));
                } else {Verify(source,f.sha256,f.bytes);File.Copy(source,destination);}
                Verify(destination,f.sha256,f.bytes);completed+=f.bytes;Progress(completed,total,f.path);
            }
            Progress(total,total,"Preparando Delirium desde tu ISO…");
            Run(Path.Combine(tools,"sa-delirium-bootstrap.exe"),Quote(preparedRoot)+" --iso "+Quote(iso),Paths.Inside(preparedRoot,"iso-import.log"));
            var files=Manifest.files.Select(f=>new ProfileFile {path=f.path,sha256=f.sha256,bytes=f.bytes,components=f.components}).ToList();
            if(Manifest.prepare_legacy_directx)EnsureLegacyRuntime(preparedRoot);
            if(Manifest.choose_compatible_renderer)SelectRenderer(preparedRoot,files);
            var resources=Paths.Inside(preparedRoot,"DeliriumSA-local-v3");
            if(!Directory.Exists(resources))throw new IOException("No se ha generado el contenido de la ISO.");
            foreach(var path in Directory.GetFiles(resources,"*",SearchOption.AllDirectories)) {
                Paths.NoLinks(path);
                files.Add(new ProfileFile {path="DeliriumSA-local-v3/"+path.Substring(resources.Length+1).Replace('\\','/'),sha256=Engine.Hash(path),bytes=new FileInfo(path).Length,components=new[]{"delirium"}});
            }
            var index=Json.Serializer().Deserialize<ProfileIndex>(Json.Serializer().Serialize(Manifest.profile));
            index.files=files.ToArray();index.total_bytes=files.Sum(f=>f.bytes);
            Json.Write(Paths.Inside(preparedRoot,"portable-preparation.json"),new{status="prepared",index,omissions=Manifest.omissions,iso_copied=false});
            return index;
        }
        void SelectRenderer(string root,List<ProfileFile> files) {
            var dxvk=files.Single(f=>f.path.Equals("d3d9.dll",StringComparison.OrdinalIgnoreCase));
            var path=Paths.Inside(root,dxvk.path);Verify(path,dxvk.sha256,dxvk.bytes);
            var probe=Paths.Inside(tools,"RendererProbe.exe");
            if(!File.Exists(probe))throw new IOException("Falta la comprobación del dispositivo gráfico.");
            var log=Paths.Inside(root,"renderer-probe.log");
            bool supported=true;string reason="device_created";
            Progress(0,0,"Comprobando la compatibilidad gráfica…");
            try {Run(probe,Quote(path),log,false,20000);}
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
            try {Run(probe,"--check-legacy-runtime",log,false,15000);return;}
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
            Run(probe,"--check-legacy-runtime",log,false,15000);
        }
    }
}
