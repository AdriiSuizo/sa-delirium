"""Fix-TxdConflicts.py: repair map models left without textures by modloader.

Two texture mods can each ship their own copy of the same .txd. modloader loads
only the highest-priority copy, so a model whose .dff came from the other mod
asks for textures that are no longer there and draws flat white/beige (seen
06/11 in Jefferson: Jefferson Retextured's houses with Parallax Interiors'
jeffers4_lae.txd). For every map model this finds the .dff and .txd modloader
actually loads; each texture the model names that its txd (and txd parents)
lacks is taken from another copy of that same txd, or else from a txd of the
mod that supplied the .dff. The winning txd plus those textures is written to
modloader/<OUT> at a priority above every other mod, with a report.

Other mods are not changed. Delete the output folder (and its ini line) to undo.

    python -I tools/Fix-TxdConflicts.py [--game game] [--dry-run]
        [--only-mods-from MANIFEST --mods-only --out DIR]   (installer: its own
        mod set, and nothing that would carry original game textures)
"""
import argparse,collections,json,os,re,struct

OUT='Delirium Texture Fixes'
OUT_PRIORITY=99

ap=argparse.ArgumentParser()
ap.add_argument('--game',default=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','game'))
ap.add_argument('--dry-run',action='store_true')
ap.add_argument('--only-mods-from',help='Installer manifest: use only the mod folders it installs')
ap.add_argument('--mods-only',action='store_true',help='Skip txds that would carry original game textures (for redistribution)')
ap.add_argument('--out',help='Write the txds and report here instead of modloader/'+OUT+' (no ini change)')
args=ap.parse_args()
G=os.path.abspath(args.game);ML=os.path.join(G,'modloader');INI=os.path.join(ML,'modloader.ini')

def section(text,name):
    m=re.search(r'\['+re.escape(name)+r'\](.*?)(?:\n\[|\Z)',text,re.S)
    return [l.split(';')[0].strip() for l in (m.group(1).splitlines() if m else [])]
ini=open(INI,encoding='latin-1').read()
pri={}
for l in section(ini,'Profiles.Default.Priority'):
    if '=' in l:k,v=l.split('=',1);pri[k.strip().lower()]=int(v)
ignored={l.lower() for l in section(ini,'Profiles.Default.IgnoreMods') if l}
shipped=None
if args.only_mods_from:
    shipped={f['path'].split('/')[1].lower() for f in json.load(open(args.only_mods_from,encoding='utf-8'))['files'] if f['path'].lower().startswith('modloader/') and f['path'].count('/')>=2}

def imgdir(path):
    with open(path,'rb') as f:
        h=f.read(8)
        if h[:4]!=b'VER2':return {}
        d={}
        for _ in range(struct.unpack('<I',h[4:])[0]):
            off,sz,_,name=struct.unpack('<IHH24s',f.read(32))
            d[name.split(b'\0')[0].decode('latin-1').lower()]=(path,off*2048,sz*2048)
        return d
def rd(src):
    with open(src[0],'rb') as f:
        if len(src)==3:f.seek(src[1]);return f.read(src[2])
        return f.read()

# name -> [(priority, mod, source)]; vanilla archives at priority -1, mod ''.
cand=collections.defaultdict(list);ides=[]
for img in ('gta3.img','gta_int.img'):
    for k,v in imgdir(os.path.join(G,'models',img)).items():cand[k].append((-1,'',v))
for mod in sorted(os.listdir(ML)):
    mp=os.path.join(ML,mod)
    if not os.path.isdir(mp) or mod.lower() in ignored or mod==OUT or pri.get(mod.lower(),50)==0:continue
    if shipped is not None and mod.lower() not in shipped:continue
    p=pri.get(mod.lower(),50)
    for root,_,files in os.walk(mp):
        for fn in files:
            l=fn.lower();full=os.path.join(root,fn)
            if l.endswith(('.dff','.txd')):cand[l].append((p,mod,(full,)))
            elif l.endswith('.img'):
                for k,v in imgdir(full).items():cand[k].append((p,mod,v))
            elif l.endswith('.ide'):ides.append((p,mod,full))
def win(name):
    c=cand.get(name)
    return max(c,key=lambda x:x[0]) if c else None

def chunks(b,o,e):
    while o+12<=e:
        t,s,_=struct.unpack_from('<III',b,o);yield t,o,o+12,min(o+12+s,e);o+=12+s
def dfftex(b):
    out=set()
    def walk(o,e,depth):
        for t,_,a,z in chunks(b,o,e):
            if t==6:
                for t2,_,a2,z2 in chunks(b,a,z):
                    if t2==2:out.add(b[a2:z2].split(b'\0')[0].decode('latin-1').lower());break
            elif t in(0x10,0x1A,0x0F,0x08,0x07,0x14,0x0E,0x3) and depth<8:walk(a,z,depth+1)
    walk(0,len(b),0);out.discard('');return out
def natives(b):
    """texture name -> raw 0x15 chunk (header included)."""
    out={}
    for t,_,a,z in chunks(b,0,len(b)):
        if t!=0x16:continue
        for t2,h2,a2,z2 in chunks(b,a,z):
            if t2!=0x15:continue
            for t3,_,a3,_ in chunks(b,a2,z2):
                if t3==1:out.setdefault(b[a3+8:a3+40].split(b'\0')[0].decode('latin-1').lower(),b[h2:z2])
                break
    return out
ncache={}
def nat(src):
    if src not in ncache:ncache[src]=natives(rd(src))
    return ncache[src]

# Map IDEs: gta.dat ones, replaced by a same-named mod copy; other mod IDEs added.
idefiles={}
for l in open(os.path.join(G,'data','gta.dat'),encoding='latin-1'):
    l=l.strip()
    if l.upper().startswith('IDE '):p=os.path.join(G,l[4:].strip());idefiles[os.path.basename(p).lower()]=(-1,p)
for p,_,full in ides:
    k=os.path.basename(full).lower()
    if k not in idefiles or idefiles[k][0]<=p:idefiles[k]=(p,full)
models={};parent={}
for _,full in idefiles.values():
    s=None
    for l in open(full,encoding='latin-1',errors='replace'):
        l=l.split('#')[0].strip();lo=l.lower()
        if not l:continue
        if lo in('objs','tobj','anim','txdp','peds','cars','weap','hier','2dfx','path','tanm'):s=lo;continue
        if lo=='end':s=None;continue
        f=[x.strip().lower() for x in l.split(',')]
        if s in('objs','tobj','anim') and len(f)>=3:models[f[1]]=f[2]
        elif s=='txdp' and len(f)>=2:parent[f[0]]=f[1]
def chain(txd):
    out=[];seen=set()
    while txd and txd not in seen:seen.add(txd);out.append(txd);txd=parent.get(txd)
    return out
modtxds=collections.defaultdict(list)
for name,cs in cand.items():
    if name.endswith('.txd'):
        for c in cs:
            if c[1]:modtxds[c[1]].append((name,c))

# Textures to add per txd: txd -> texture -> (mod, source txd, raw chunk).
adds=collections.defaultdict(dict);report=[]
for m,txd in sorted(models.items()):
    w=win(m+'.dff')
    if not w:continue
    try:need=dfftex(rd(w[2]))
    except OSError:continue
    have=set(adds[txd])
    for t in chain(txd):
        tw=win(t+'.txd')
        if tw:have|=nat(tw[2]).keys()
    missing=sorted(need-have)
    if not missing:continue
    vanilla_missing=set()
    vd=next((c for c in cand.get(m+'.dff',[]) if c[0]<0),None)
    if vd:
        vanilla_missing=dfftex(rd(vd[2]))
        for t in chain(txd):
            vt=next((c for c in cand.get(t+'.txd',[]) if c[0]<0),None)
            if vt:vanilla_missing-=nat(vt[2]).keys()
    found={}
    for tex in missing:
        # Another copy of the model's own txd (or a parent), highest priority first.
        for t in chain(txd):
            c=next((c for c in sorted(cand.get(t+'.txd',[]),key=lambda x:-x[0]) if tex in nat(c[2])),None)
            if c:found[tex]=(c[1] or 'vanilla',t+'.txd',nat(c[2])[tex]);break
        # Else a txd shipped by the mod that supplied this dff, but only for a
        # texture the original game does not lack too (otherwise any same-named
        # texture of that mod would be a guess).
        if tex not in found and w[1] and tex not in vanilla_missing:
            hit=next(((n,c) for n,c in modtxds[w[1]] if tex in nat(c[2])),None)
            if hit:found[tex]=(w[1],hit[0],nat(hit[1][2])[tex])
    adds[txd].update(found)
    if found:report.append({'model':m,'dff':w[1] or 'vanilla','txd':txd+'.txd','txd_loaded':(win(txd+'.txd') or (0,'none'))[1] or 'vanilla',
                            'added':{t:f'{v[0]}/{v[1]}' for t,v in found.items()},'still_missing':[t for t in missing if t not in found]})

def build(base,extra):
    """Rebuild a TXD: the loaded copy's textures, then the added ones."""
    for t,h,a,z in chunks(base,0,len(base)):
        if t!=0x16:continue
        body=[];ext=b'';dev=0;ver=base[h+8:h+12]
        for t2,h2,a2,z2 in chunks(base,a,z):
            if t2==1:dev=struct.unpack_from('<H',base,a2+2)[0];ver=base[h2+8:h2+12]
            elif t2==0x15:body.append(base[h2:z2])
            elif t2==3:ext=base[h2:z2]
        body+=extra
        if not ext:ext=struct.pack('<II',3,0)+ver
        inner=struct.pack('<II',1,4)+ver+struct.pack('<HH',len(body),dev)+b''.join(body)+ext
        return struct.pack('<II',0x16,len(inner))+base[h+8:h+12]+inner
    raise ValueError('not a texture dictionary')

out=os.path.abspath(args.out) if args.out else os.path.join(ML,OUT);written=[]
for txd,extra in sorted(adds.items()):
    w=win(txd+'.txd')
    if not extra or not w:continue
    if args.mods_only and (not w[1] or any(v[0]=='vanilla' for v in extra.values())):continue
    data=build(rd(w[2]),[v[2] for v in extra.values()])
    assert set(natives(data))>=set(extra)
    written.append({'txd':txd+'.txd','base':w[1] or 'vanilla','added':{t:f'{v[0]}/{v[1]}' for t,v in extra.items()}})
    if not args.dry_run:
        os.makedirs(out,exist_ok=True)
        with open(os.path.join(out,txd+'.txd'),'wb') as f:f.write(data)
if not args.dry_run and written:
    with open(os.path.join(out,'Delirium-Texture-Fixes.json'),'w',encoding='utf-8') as f:
        json.dump({'txds':written,'models':report},f,indent=1)
    if not args.out and not re.search(r'^\s*'+re.escape(OUT)+r'\s*=',ini,re.M):
        ini=re.sub(r'(\[Profiles\.Default\.Priority\]\r?\n)',lambda m:m.group(1)+f'{OUT}={OUT_PRIORITY}\n',ini,count=1)
        with open(INI,'w',encoding='latin-1',newline='') as f:f.write(ini)
print(json.dumps({'models_fixed':len(report),'txds_written':len(written),
                  'models_still_missing_some':sum(1 for r in report if r['still_missing']),'dry_run':args.dry_run}))
for wr in written:print(' ',wr['txd'],'base',wr['base'],'+',len(wr['added']),sorted({v.split('/')[0] for v in wr['added'].values()}))
