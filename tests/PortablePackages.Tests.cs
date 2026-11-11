// Package boundary tests only. Synthetic bytes are not gameplay evidence.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Reflection;
using System.Text;
using SASetup;
using SAPortable;

static class PortablePackageTests {
    static void Assert(bool value,string message){if(!value)throw new Exception(message);}
    static void CopyBootstrapFixture(string tools) {
        File.Copy(Assembly.GetExecutingAssembly().Location,Path.Combine(tools,"sa-delirium-bootstrap.exe"));
    }
    static bool ImportBoundary(Exception failure) {
        return failure is IOException && failure.Message.Contains("la importación desde la ISO");
    }
    static void Case(string root,string name,bool contentPin,bool corrupt,bool traversal,bool link,bool duplicate,bool cancel,bool encrypted=false,bool wrongPassword=false) {
        var folder=Path.Combine(root,name);Directory.CreateDirectory(folder);
        var steam=Path.Combine(folder,"steam");Directory.CreateDirectory(steam);
        var exe=Path.Combine(steam,"gta-sa.exe");File.WriteAllText(exe,"owner-stock-sentinel");var stock=Engine.Hash(exe);
        var iso=Path.Combine(folder,"synthetic.iso");File.WriteAllText(iso,"unit fixture, not an owned disc");
        var cache=Path.Combine(folder,"cache");Directory.CreateDirectory(cache);
        var zip=Path.Combine(folder,"source.zip");var data=Encoding.UTF8.GetBytes(link?"../../../steam/gta-sa.exe":"immutable-member");
        var member=traversal?"../../../steam/gta-sa.exe":"nested/part.dat";
        using(var output=File.Create(zip))using(var z=new ZipArchive(output,ZipArchiveMode.Create)) {
            for(int i=0;i<(duplicate?2:1);i++){
                var entry=z.CreateEntry(member);
                if(link)entry.ExternalAttributes=unchecked((int)(0xA1FF0000u));
                using(var stream=entry.Open())stream.Write(data,0,data.Length);
            }
        }
        if(encrypted) {
            File.Delete(zip);
            var input=Path.Combine(folder,"plaintext");Directory.CreateDirectory(Path.Combine(input,"nested"));File.WriteAllBytes(Path.Combine(input,"nested","part.dat"),data);
            using(var process=Process.Start(new ProcessStartInfo(@"C:\Program Files\7-Zip\7z.exe","a -tzip -mem=AES256 -plibertycity \""+zip+"\" nested/part.dat") {UseShellExecute=false,CreateNoWindow=true,WorkingDirectory=input,RedirectStandardOutput=true})) {
                process.StandardOutput.ReadToEnd();process.WaitForExit();Assert(process.ExitCode==0,"Could not build synthetic encrypted fixture");
            }
        }
        var recipe=new Recipe {path="mods/part.dat",sha256=Engine.Hash(data),bytes=data.Length,components=new[]{"fixture"},kind="package",package="original",member=member};
        var package=new Package {id="original",filename="source.zip",sha256=contentPin?null:Engine.Hash(zip),bytes=contentPin?0:new FileInfo(zip).Length,
            member_pins_only=contentPin,max_bytes=1024*1024,url="https://example.invalid/never-contacted"};
        if(encrypted)package.password=wrongPassword?"incorrect-public-password":"libertycity";
        if(corrupt)recipe.sha256=Engine.Hash(Encoding.UTF8.GetBytes("a different immutable input"));
        var manifest=new Manifest {schema=1,packages=new[]{package},files=new[]{recipe},profile=new ProfileIndex {schema=1,profile="synthetic package boundary test",stock_steam_executable_sha256=stock}};
        var identity=package.sha256??Engine.Hash(Encoding.UTF8.GetBytes(Json.Serializer().Serialize(package)+Json.Serializer().Serialize(manifest.files.Where(f=>f.package==package.id).OrderBy(f=>f.path).ToArray())));
        var cached=Path.Combine(cache,"downloads",identity);Directory.CreateDirectory(cached);File.Copy(zip,Path.Combine(cached,"source.zip"));
        var tools=Path.Combine(folder,"tools");Directory.CreateDirectory(tools);
        foreach(var file in new[]{"7z.exe","7z.dll"})File.Copy(Path.Combine(@"C:\Program Files\7-Zip",file),Path.Combine(tools,file));
        CopyBootstrapFixture(tools);
        bool reachedIso=false;Exception failure=null;string prepared=null;
        var composer=new Composer(manifest,tools,cache) {Cancelled=delegate{return cancel;},Progress=delegate(long a,long b,string message){if(message.StartsWith("Preparando Delirium"))reachedIso=true;}};
        try{composer.Prepare(steam,iso,out prepared);}catch(Exception e){failure=e;}
        Assert(Engine.Hash(exe)==stock,name+": target Steam bytes changed");
        Assert(failure!=null,name+": fixture must stop before a real ISO importer");
        if(!corrupt&&!traversal&&!link&&!duplicate&&!cancel&&!wrongPassword) {
            Assert(reachedIso && ImportBoundary(failure),name+": ordinary package did not reach the ISO boundary: "+failure);
            Assert(Engine.Hash(Path.Combine(prepared,"mods","part.dat"))==recipe.sha256,name+": selected member not reproduced");
        } else {
            Assert(!reachedIso,name+": rejected input reached runtime preparation");
            Assert(cancel?failure is OperationCanceledException:failure is IOException,name+": unexpected failure: "+failure);
        }
    }
    static byte[] Bytes(string seed,int length) {
        var data=new byte[length];var random=new Random(seed.Length*7919+seed[0]);random.NextBytes(data);
        var text=Encoding.ASCII.GetBytes(seed);Array.Copy(text,data,Math.Min(text.Length,length));return data;
    }
    static void Edition(string root,string name,Manifest manifest,string launcher,Action<string> fill,string kind,bool accepted) {
        var folder=Path.Combine(root,"editions",name);Directory.CreateDirectory(folder);fill(folder);
        var report=Editions.Identify(folder,manifest,launcher);
        Assert(report.kind==kind && report.accepted==accepted && !String.IsNullOrEmpty(report.message),name+": identified as "+report.kind+" accepted="+report.accepted+" ("+report.message+")");
    }
    static void EditionCases(string root,List<string> results) {
        var stock=Bytes("stock-steam",4096);var launcher=Engine.Hash(Bytes("own-launcher",2048));var future=Bytes("future-steam",4096);
        var manifest=new Manifest {schema=1,packages=new Package[0],files=new Recipe[0],profile=new ProfileIndex {schema=1,profile="edition identification",stock_steam_executable_sha256=Engine.Hash(stock)}};
        Edition(root,"stock",manifest,launcher,d=>File.WriteAllBytes(Path.Combine(d,"gta-sa.exe"),stock),"unknown",true);
        Edition(root,"unknown-build",manifest,launcher,d=>File.WriteAllBytes(Path.Combine(d,"gta-sa.exe"),future),"unknown",false);
        Edition(root,"launcher-already-installed",manifest,launcher,d=>File.WriteAllBytes(Path.Combine(d,"gta-sa.exe"),Bytes("own-launcher",2048)),"installed",false);
        Edition(root,"pending-journal",manifest,launcher,d=>{File.WriteAllBytes(Path.Combine(d,"gta-sa.exe"),stock);File.WriteAllText(Path.Combine(d,Engine.ReceiptName),"{}");},"installed",false);
        Edition(root,"definitive-edition",manifest,launcher,d=>{Directory.CreateDirectory(Path.Combine(d,"Gameface","Binaries","Win64"));File.WriteAllText(Path.Combine(d,"Gameface","Binaries","Win64","SanAndreas.exe"),"de");},"definitive",false);
        Edition(root,"no-executable",manifest,launcher,d=>{},"missing",false);
        // The stock bytes renamed to gta_sa.exe are not a Steam folder.
        Edition(root,"classic-name-only",manifest,launcher,d=>File.WriteAllBytes(Path.Combine(d,"gta_sa.exe"),stock),"unknown",false);
        manifest.profile.accepted_steam_executable_sha256=new[]{Engine.Hash(future)};
        Edition(root,"listed-future-build",manifest,launcher,d=>File.WriteAllBytes(Path.Combine(d,"gta-sa.exe"),future),"unknown",true);
        results.Add("edition-identification");
    }
    static void MakeDelta(string tool,string source,string target,string patch) {
        using(var process=Process.Start(new ProcessStartInfo(tool,"make \""+source+"\" \""+target+"\" \""+patch+"\""){UseShellExecute=false,CreateNoWindow=true})){
            process.WaitForExit();Assert(process.ExitCode==0,"Fixture delta not built");
        }
    }
    // One output reached from two Steam builds through two verified deltas.
    static void AlternativeDelta(string root,string deltaTool,string name,bool listed,bool withAlternative,bool sourceIsAlternative,bool expectPrepared) {
        var folder=Path.Combine(root,name);Directory.CreateDirectory(folder);
        var tools=Path.Combine(folder,"tools");Directory.CreateDirectory(Path.Combine(tools,"patches"));
        File.Copy(deltaTool,Path.Combine(tools,"DeltaTool.exe"));
        CopyBootstrapFixture(tools);
        var inputs=Path.Combine(folder,"inputs");Directory.CreateDirectory(inputs);
        var stock=Path.Combine(inputs,"stock.bin");var other=Path.Combine(inputs,"other.bin");var output=Path.Combine(inputs,"one-zero.bin");
        var target=Bytes("one-zero-output",65536);var stockBytes=Bytes("stock-steam-build",49152);var otherBytes=(byte[])stockBytes.Clone();
        for(int i=4096;i<8192;i++)otherBytes[i]^=0x5a;
        File.WriteAllBytes(stock,stockBytes);File.WriteAllBytes(other,otherBytes);File.WriteAllBytes(output,target);
        MakeDelta(deltaTool,stock,output,Path.Combine(tools,"patches","stock.pa30"));
        MakeDelta(deltaTool,other,output,Path.Combine(tools,"patches","other.pa30"));
        var recipe=new Recipe {path="gta_sa.exe",kind="steam_delta",source="gta-sa.exe",source_sha256=Engine.Hash(stock),source_bytes=stockBytes.Length,patch="stock.pa30",sha256=Engine.Hash(output),bytes=target.Length,components=new string[0]};
        if(withAlternative)recipe.alternatives=new[]{new RecipeSource {name="Steam alternativa (prueba)",source_sha256=Engine.Hash(other),source_bytes=otherBytes.Length,patch="other.pa30"}};
        var manifest=new Manifest {schema=1,packages=new Package[0],files=new[]{recipe},profile=new ProfileIndex {schema=1,profile="alternative delta",stock_steam_executable_sha256=Engine.Hash(stock)}};
        if(listed)manifest.profile.accepted_steam_executable_sha256=new[]{Engine.Hash(other)};
        var steam=Path.Combine(folder,"steam");Directory.CreateDirectory(steam);
        var exe=Path.Combine(steam,"gta-sa.exe");File.Copy(sourceIsAlternative?other:stock,exe);var before=Engine.Hash(exe);
        var iso=Path.Combine(folder,"synthetic.iso");File.WriteAllText(iso,"unit fixture, not an owned disc");
        var cache=Path.Combine(folder,"cache");Directory.CreateDirectory(cache);
        bool reachedIso=false;Exception failure=null;string prepared=null;
        var composer=new Composer(manifest,tools,cache){Progress=delegate(long a,long b,string message){if(message.StartsWith("Preparando Delirium"))reachedIso=true;}};
        try{composer.Prepare(steam,iso,out prepared);}catch(Exception e){failure=e;}
        Assert(Engine.Hash(exe)==before,name+": Steam executable changed");
        if(expectPrepared) {
            Assert(reachedIso && ImportBoundary(failure),name+": did not reach the ISO boundary: "+failure);
            Assert(Engine.Hash(Path.Combine(prepared,"gta_sa.exe"))==recipe.sha256,name+": verified output not reproduced");
        } else Assert(!reachedIso && failure is IOException,name+": unexpected result: "+failure);
    }
    // A PC 1.0 copy: different file names and bytes, one game. Files equal
    // in both editions need nothing; differing ones are copied or patched.
    static void ClassicCopy(string root,string deltaTool,List<string> results) {
        var folder=Path.Combine(root,"classic-copy");Directory.CreateDirectory(folder);
        var tools=Path.Combine(folder,"tools");Directory.CreateDirectory(Path.Combine(tools,"patches"));
        File.Copy(deltaTool,Path.Combine(tools,"DeltaTool.exe"));
        CopyBootstrapFixture(tools);
        var inputs=Path.Combine(folder,"inputs");Directory.CreateDirectory(inputs);
        var steamExe=Bytes("steam-exe",40960);var classicExe=Bytes("classic-exe",65536);var output=(byte[])classicExe.Clone();output[60]^=0x20;
        var steamAudio=Bytes("steam-cut-radio",8192);var classicAudio=Bytes("classic-full-radio",12288);var shared=Bytes("shared-map",4096);
        File.WriteAllBytes(Path.Combine(inputs,"steam.exe"),steamExe);File.WriteAllBytes(Path.Combine(inputs,"classic.exe"),classicExe);File.WriteAllBytes(Path.Combine(inputs,"out.exe"),output);
        MakeDelta(deltaTool,Path.Combine(inputs,"steam.exe"),Path.Combine(inputs,"out.exe"),Path.Combine(tools,"patches","steam.pa30"));
        MakeDelta(deltaTool,Path.Combine(inputs,"classic.exe"),Path.Combine(inputs,"out.exe"),Path.Combine(tools,"patches","classic.pa30"));
        Func<byte[],string> h=delegate(byte[] b){return Engine.Hash(b);};
        var exe=new Recipe {path="gta_sa.exe",kind="steam_delta",source="gta-sa.exe",source_sha256=h(steamExe),source_bytes=steamExe.Length,patch="steam.pa30",sha256=h(output),bytes=output.Length,components=new string[0],
            alternatives=new[]{
                new RecipeSource{source="gta_sa.exe",source_sha256=h(classicExe),source_bytes=classicExe.Length,patch="classic.pa30"},
                new RecipeSource{source="gta-sa.exe",source_sha256=h(classicExe),source_bytes=classicExe.Length,patch="classic.pa30"},
                new RecipeSource{source="gta_sa.exe",source_sha256=h(output),source_bytes=output.Length}}};
        var audio=new Recipe {path="audio/streams/CH",kind="steam",source="audio/streams/CH",sha256=h(steamAudio),bytes=steamAudio.Length,components=new string[0],
            alternatives=new[]{new RecipeSource{name="PC 1.0 US",source_sha256=h(classicAudio),source_bytes=classicAudio.Length}}};
        var map=new Recipe {path="data/maps/shared.ipl",kind="steam",source="data/maps/shared.ipl",sha256=h(shared),bytes=shared.Length,components=new string[0]};
        var manifest=new Manifest {schema=1,packages=new Package[0],files=new[]{exe,audio,map},profile=new ProfileIndex {schema=1,profile="classic copy",stock_steam_executable_sha256=h(steamExe)}};
        Action<string,Action<string>,bool,string,string> run=delegate(string name,Action<string> fill,bool expect,string exeOut,string audioOut) {
            var game=Path.Combine(folder,name);Directory.CreateDirectory(Path.Combine(game,"audio","streams"));Directory.CreateDirectory(Path.Combine(game,"data","maps"));
            File.WriteAllBytes(Path.Combine(game,"data","maps","shared.ipl"),shared);fill(game);
            var before=Directory.GetFiles(game,"*",SearchOption.AllDirectories).ToDictionary(x=>x,x=>Engine.Hash(x));
            var report=Editions.Identify(game,manifest,null,delegate(string x){return false;});
            var cache=Path.Combine(folder,name+"-cache");Directory.CreateDirectory(cache);
            var iso=Path.Combine(folder,"synthetic.iso");if(!File.Exists(iso))File.WriteAllText(iso,"unit fixture, not an owned disc");
            bool reachedIso=false;Exception failure=null;string prepared=null;
            var composer=new Composer(manifest,tools,cache){Progress=delegate(long a,long b,string m){if(m.StartsWith("Preparando Delirium"))reachedIso=true;}};
            try{composer.Prepare(game,iso,out prepared);}catch(Exception e){failure=e;}
            foreach(var item in before)Assert(Engine.Hash(item.Key)==item.Value,name+": game file changed "+item.Key);
            if(expect) {
                Assert(report.accepted && report.mode=="standalone",name+": not accepted as standalone: "+report.message);
                Assert(reachedIso && ImportBoundary(failure),name+": did not reach the ISO boundary: "+failure);
                Assert(Engine.Hash(Path.Combine(prepared,"gta_sa.exe"))==exeOut,name+": executable output");
                Assert(Engine.Hash(Path.Combine(prepared,"audio","streams","CH"))==audioOut,name+": audio kept as the owner's edition");
                Assert(Engine.Hash(Path.Combine(prepared,"data","maps","shared.ipl"))==h(shared),name+": shared file");
            } else Assert(!reachedIso && failure is IOException,name+": unexpected: "+failure);
            results.Add(name);
        };
        run("classic-pc-copy",g=>{File.WriteAllBytes(Path.Combine(g,"gta_sa.exe"),classicExe);File.WriteAllBytes(Path.Combine(g,"audio","streams","CH"),classicAudio);},true,h(output),h(classicAudio));
        run("classic-already-output",g=>{File.WriteAllBytes(Path.Combine(g,"gta_sa.exe"),output);File.WriteAllBytes(Path.Combine(g,"audio","streams","CH"),classicAudio);},true,h(output),h(classicAudio));
        run("mixed-steam-data-classic-exe",g=>{File.WriteAllBytes(Path.Combine(g,"gta_sa.exe"),classicExe);File.WriteAllBytes(Path.Combine(g,"audio","streams","CH"),steamAudio);},true,h(output),h(steamAudio));
        run("modified-data-file",g=>{File.WriteAllBytes(Path.Combine(g,"gta_sa.exe"),classicExe);File.WriteAllBytes(Path.Combine(g,"audio","streams","CH"),Bytes("someone-else-radio",12288));},false,null,null);
        run("missing-data-file",g=>{File.WriteAllBytes(Path.Combine(g,"gta_sa.exe"),classicExe);},false,null,null);
        // Downgraded Steam folder: 1.0 bytes under the Steam name.
        var downgraded=Path.Combine(folder,"downgraded-steam");Directory.CreateDirectory(downgraded);File.WriteAllBytes(Path.Combine(downgraded,"gta-sa.exe"),classicExe);
        Assert(Editions.AcceptedExecutables(manifest).Contains(h(classicExe)),"Downgraded Steam executable can be journaled and restored");
        Assert(Editions.Identify(downgraded,manifest,null,delegate(string x){return true;}).mode=="steam","Downgraded copy inside a Steam library keeps the Steam button");
        results.Add("downgraded-steam-folder");
    }
    static void StandaloneCases(string root,List<string> results) {
        var folder=Path.Combine(root,"standalone");Directory.CreateDirectory(folder);
        var game=Path.Combine(folder,"game");Directory.CreateDirectory(game);File.WriteAllText(Path.Combine(game,"gta_sa.exe"),"owner game, only read");
        var prepared=Path.Combine(folder,"prepared");Directory.CreateDirectory(Path.Combine(prepared,"modloader"));
        File.WriteAllText(Path.Combine(prepared,"gta_sa.exe"),"prepared executable");File.WriteAllText(Path.Combine(prepared,"modloader","mod.asi"),"prepared mod");
        var index=new ProfileIndex {schema=1,profile="standalone fixture",native_save_path_limit_bytes=104,stock_steam_executable_sha256=new string('0',64),
            files=new[]{"gta_sa.exe","modloader/mod.asi"}.Select(x=>new ProfileFile{path=x,sha256=Engine.Hash(Path.Combine(prepared,x)),bytes=new FileInfo(Path.Combine(prepared,x)).Length,components=new string[0]}).ToArray()};
        index.total_bytes=index.files.Sum(x=>x.bytes);
        Directory.CreateDirectory(Path.Combine(prepared,"userfiles"));File.WriteAllText(Path.Combine(prepared,"userfiles","GTASAsf2.b"),"carried save");
        var links=Path.Combine(folder,"desktop");Directory.CreateDirectory(links);
        var destination=Path.Combine(folder,"SA-Delirium");
        var gameHash=Engine.Hash(Path.Combine(game,"gta_sa.exe"));
        bool refused=false;try{Standalone.Install(index,prepared,game,Path.Combine(game,"mod"),new[]{links},delegate{},delegate{return false;});}catch(IOException){refused=true;}
        Assert(refused,"Destination inside the game folder refused");
        var receipt=Standalone.Install(index,prepared,game,destination,new[]{links},delegate{},delegate{return false;});
        Assert(receipt.State=="installed" && receipt.Mode=="standalone","Standalone install committed");
        foreach(var f in index.files)Assert(Engine.Hash(Path.Combine(destination,"SA",f.path))==f.sha256,"Exact profile file "+f.path);
        Assert(File.Exists(Path.Combine(links,Standalone.ShortcutName)) && receipt.Shortcuts.Length==1,"Shortcut created");
        Assert(File.ReadAllText(Path.Combine(destination,"SA","userfiles","GTASAsf2.b"))=="carried save","Carried save installed with the profile");
        Assert(Engine.Hash(Path.Combine(game,"gta_sa.exe"))==gameHash && Directory.GetFiles(game).Length==1,"Game folder only read");
        refused=false;try{Standalone.Install(index,prepared,game,destination,new[]{links},delegate{},delegate{return false;});}catch(IOException){refused=true;}
        Assert(refused,"Second install over an existing one refused");
        File.WriteAllText(Path.Combine(destination,"SA","userfiles.txt"),"new progress");
        var restored=Standalone.Restore(destination);
        Assert(restored.State=="restored" && !File.Exists(Path.Combine(links,Standalone.ShortcutName)),"Restore removes our shortcut");
        Assert(File.ReadAllText(Path.Combine(restored.RetainedProfile,"userfiles.txt"))=="new progress","Restore keeps the profile and its progress");
        Assert(!Directory.Exists(Path.Combine(destination,"SA")) && !Standalone.Installed(destination),"Journal archived after restore");
        // A shortcut with the same name that is not ours is never touched.
        File.WriteAllText(Path.Combine(links,Standalone.ShortcutName),"not a shortcut of ours");
        var other=Path.Combine(folder,"SA-Delirium-2");
        refused=false;try{Standalone.Install(index,prepared,game,other,new[]{links},delegate{},delegate{return false;});}catch(Exception){refused=true;}
        Assert(File.ReadAllText(Path.Combine(links,Standalone.ShortcutName))=="not a shortcut of ours","Foreign file with our shortcut name untouched");
        results.Add("standalone-install-restore");
    }
    // Full preparation with test doubles for the ISO importer and probe:
    // the profile index must record the edition actually used, and a
    // missing DirectX runtime must stop an unattended install clearly.
    static void FullPreparation(string root,List<string> results) {
        foreach(var name in new[]{"full-preparation-index","directx-missing-unattended","invalid-iso-before-preparation"}) {
            var missing=name=="directx-missing-unattended";
            var invalid=name=="invalid-iso-before-preparation";
            var folder=Path.Combine(root,name);var tools=Path.Combine(folder,"tools");Directory.CreateDirectory(tools);
            CopyBootstrapFixture(tools);File.Copy(Assembly.GetExecutingAssembly().Location,Path.Combine(tools,"RendererProbe.exe"));
            var steamAudio=Bytes("steam-cut-radio",8192);var classicAudio=Bytes("classic-full-radio",12288);var exeBytes=Bytes("game-exe",20480);
            var exe=new Recipe{path="gta_sa.exe",kind="steam",source="gta-sa.exe",sha256=Engine.Hash(exeBytes),bytes=exeBytes.Length,components=new string[0],
                alternatives=new[]{new RecipeSource{source="gta_sa.exe",source_sha256=Engine.Hash(exeBytes),source_bytes=exeBytes.Length}}};
            var audio=new Recipe{path="audio/streams/CH",kind="steam",source="audio/streams/CH",sha256=Engine.Hash(steamAudio),bytes=steamAudio.Length,components=new string[0],
                alternatives=new[]{new RecipeSource{name="PC 1.0 US",source_sha256=Engine.Hash(classicAudio),source_bytes=classicAudio.Length}}};
            var manifest=new Manifest{schema=1,packages=new Package[0],files=new[]{exe,audio},prepare_legacy_directx=true,
                profile=new ProfileIndex{schema=1,profile="full preparation",stock_steam_executable_sha256=new string('a',64)}};
            var game=Path.Combine(folder,"game");Directory.CreateDirectory(Path.Combine(game,"audio","streams"));
            File.WriteAllBytes(Path.Combine(game,"gta_sa.exe"),exeBytes);File.WriteAllBytes(Path.Combine(game,"audio","streams","CH"),classicAudio);
            var iso=Path.Combine(folder,invalid?"rejected.iso":"synthetic.iso");File.WriteAllText(iso,"unit fixture");
            var cache=Path.Combine(folder,"cache");Directory.CreateDirectory(cache);
            Environment.SetEnvironmentVariable("FAKE_DIRECTX_MISSING",missing?"1":null);
            Environment.SetEnvironmentVariable("FAKE_BOOTSTRAP_IMPORT","1");
            Exception failure=null;string prepared=null;ProfileIndex index=null;
            try{index=new Composer(manifest,tools,cache){Unattended=true}.Prepare(game,iso,out prepared);}catch(Exception e){failure=e;}
            finally {
                Environment.SetEnvironmentVariable("FAKE_DIRECTX_MISSING",null);
                Environment.SetEnvironmentVariable("FAKE_BOOTSTRAP_IMPORT",null);
            }
            if(invalid) {
                Assert(failure is IOException && failure.Message.Contains("la comprobación de la ISO") &&
                    failure.Message.Contains("Imagen de prueba rechazada"),name+": missing ISO diagnostic: "+failure);
                Assert(!Directory.GetDirectories(cache,"prepared-*").Any(),name+": staged files before verifying the ISO");
                Assert(Directory.GetFiles(cache,"iso-check-*.log").Length==1,name+": missing log");
            }
            else if(missing) Assert(failure is IOException && failure.Message.Contains("DirectX"),name+": unexpected "+failure);
            else {
                Assert(failure==null,name+": "+failure);
                var ch=index.files.Single(f=>f.path=="audio/streams/CH");
                Assert(ch.sha256==Engine.Hash(classicAudio) && ch.bytes==classicAudio.Length,name+": index records the PC 1.0 audio actually prepared");
                Assert(index.files.Any(f=>f.path=="DeliriumSA-local-v3/generated.bin"),name+": ISO output indexed");
                var summary=File.ReadAllText(Path.Combine(prepared,"portable-preparation.json"));
                Assert(summary.Contains(Engine.Hash(classicAudio)),name+": preparation summary records the real output");
            }
            results.Add(name);
        }
    }
    static void SavesCarry(string root,List<string> results) {
        var folder=Path.Combine(root,"saves-carry");var backups=Path.Combine(folder,"SA-Delirium-backups");
        foreach(var item in new[]{Tuple.Create("20261101-100000-aaaaaaaa","old"),Tuple.Create("20261105-100000-bbbbbbbb","new")}) {
            var user=Path.Combine(backups,item.Item1,"retained-profile","userfiles");Directory.CreateDirectory(user);
            File.WriteAllText(Path.Combine(user,"GTASAsf1.b"),item.Item2+"-save");File.WriteAllText(Path.Combine(user,"notes.txt"),"not a save");
        }
        Directory.CreateDirectory(Path.Combine(backups,"20261106-100000-cccccccc","retained-profile","userfiles"));
        var prepared=Path.Combine(folder,"prepared");Directory.CreateDirectory(prepared);
        var from=Saves.Carry(folder,prepared);
        Assert(from!=null && from.Contains("20261105-100000-bbbbbbbb"),"Latest backup with saves chosen");
        Assert(File.ReadAllText(Path.Combine(prepared,"userfiles","GTASAsf1.b"))=="new-save","Save copied unchanged");
        Assert(!File.Exists(Path.Combine(prepared,"userfiles","notes.txt")),"Only save files copied");
        Assert(File.ReadAllText(Path.Combine(backups,"20261105-100000-bbbbbbbb","retained-profile","userfiles","GTASAsf1.b"))=="new-save","Backup left as it was");
        Assert(Saves.Carry(Path.Combine(folder,"none"),prepared)==null,"No backup, nothing carried");
        results.Add("saves-carry");
    }
    static int Main(string[] args) {
        Paths.EnableLongPaths();
        var executable=Path.GetFileName(Assembly.GetExecutingAssembly().Location);
        if(executable=="RendererProbe.exe")return Environment.GetEnvironmentVariable("FAKE_DIRECTX_MISSING")=="1"?1:0;
        if(executable=="sa-delirium-bootstrap.exe") {
            if(args[0]=="--check-iso") {
                if(Path.GetFileName(args[1])!="rejected.iso")return 0;
                Console.Error.WriteLine("Imagen de prueba rechazada");
                return 1;
            }
            if(Environment.GetEnvironmentVariable("FAKE_BOOTSTRAP_IMPORT")!="1")return 1;
            var generated=Path.Combine(args[0],"DeliriumSA-local-v3");
            Directory.CreateDirectory(generated);File.WriteAllText(Path.Combine(generated,"generated.bin"),"synthetic import");
            return 0;
        }
        string root=Path.GetFullPath(args[0]);Directory.CreateDirectory(root);var results=new List<string>();
        try {
            Action<string,bool,bool,bool,bool,bool,bool> run=delegate(string name,bool content,bool corrupt,bool traversal,bool link,bool duplicate,bool cancel){Case(root,name,content,corrupt,traversal,link,duplicate,cancel);results.Add(name);};
            run("pinned-original",false,false,false,false,false,false);
            run("member-pinned-original",true,false,false,false,false,false);
            run("changed-member",false,true,false,false,false,false);
            run("changed-member-optional-container-hash",true,true,false,false,false,false);
            run("parent-traversal",false,false,true,false,false,false);
            run("symbolic-link",false,false,false,true,false,false);
            run("duplicate-member",false,false,false,false,true,false);
            run("cancel-before-extraction",false,false,false,false,false,true);
            Case(root,"public-password-encrypted-original",false,false,false,false,false,false,true,false);results.Add("public-password-encrypted-original");
            Case(root,"wrong-public-password",false,false,false,false,false,false,true,true);results.Add("wrong-public-password");
            EditionCases(root,results);
            StandaloneCases(root,results);
            SavesCarry(root,results);
            FullPreparation(root,results);
            if(args.Length>1) {
                var tool=Path.GetFullPath(args[1]);
                Action<string,bool,bool,bool,bool> delta=delegate(string name,bool listed,bool alternative,bool fromAlternative,bool prepared){AlternativeDelta(root,tool,name,listed,alternative,fromAlternative,prepared);results.Add(name);};
                delta("stock-delta-with-alternative-present",true,true,false,true);
                delta("alternative-edition-delta",true,true,true,true);
                delta("alternative-accepted-by-its-recipe",false,true,true,true);
                delta("listed-without-delta",true,false,true,false);
                ClassicCopy(root,tool,results);
            }
            Json.Write(Path.Combine(root,"results.json"),new {status="passed",purpose="package_boundary_not_gameplay",cases=results});
            Console.WriteLine("Passed "+results.Count+" package boundary and edition cases");return 0;
        } catch(Exception error){Json.Write(Path.Combine(root,"results.json"),new{status="failed",passed=results,error=error.ToString()});Console.Error.WriteLine(error);return 1;}
    }
}
