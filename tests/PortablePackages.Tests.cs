// Package boundary tests only. Synthetic bytes are not gameplay evidence.
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using SASetup;
using SAPortable;

static class PortablePackageTests {
    static void Assert(bool value,string message){if(!value)throw new Exception(message);}
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
        bool reachedIso=false;Exception failure=null;string prepared=null;
        var composer=new Composer(manifest,@"C:\Program Files\7-Zip",cache) {Cancelled=delegate{return cancel;},Progress=delegate(long a,long b,string message){if(message.StartsWith("Preparando Delirium"))reachedIso=true;}};
        try{composer.Prepare(steam,iso,out prepared);}catch(Exception e){failure=e;}
        Assert(Engine.Hash(exe)==stock,name+": target Steam bytes changed");
        Assert(failure!=null,name+": fixture must stop before a real ISO importer");
        if(!corrupt&&!traversal&&!link&&!duplicate&&!cancel&&!wrongPassword) {
            Assert(reachedIso && failure is Win32Exception,name+": ordinary package did not reach the ISO boundary: "+failure);
            Assert(Engine.Hash(Path.Combine(prepared,"mods","part.dat"))==recipe.sha256,name+": selected member not reproduced");
        } else {
            Assert(!reachedIso,name+": rejected input reached runtime preparation");
            Assert(cancel?failure is OperationCanceledException:failure is IOException,name+": unexpected failure: "+failure);
        }
    }
    static int Main(string[] args) {
        Paths.EnableLongPaths();
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
            Json.Write(Path.Combine(root,"results.json"),new {status="passed",purpose="package_boundary_not_gameplay",cases=results});
            Console.WriteLine("Passed "+results.Count+" package boundary cases");return 0;
        } catch(Exception error){Json.Write(Path.Combine(root,"results.json"),new{status="failed",passed=results,error=error.ToString()});Console.Error.WriteLine(error);return 1;}
    }
}
