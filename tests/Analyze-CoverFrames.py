"""Measure all recorded frames, not only frames where a pose changes."""
import argparse,json,math
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('case',type=Path);args=p.parse_args()
f=json.loads((args.case/'stability-frames.json').read_text());segments=json.loads((args.case/'stability-segments.json').read_text())
vec=lambda r,k:tuple(map(float,r[k].split(',')))
reports=[];failures=[]
for seg in segments:
    rows=[r for r in f if seg['start']<=int(r['frame'])<=seg['end']]
    if not rows:failures.append(seg['label']+': no frames');continue
    pairs=[(a,b) for a,b in zip(rows,rows[1:]) if int(b['frame'])==int(a['frame'])+1]
    keys=[k for k in ('root','head','pelvis','chest','shoulder','elbow','hand','left_hand') if k in rows[0]]
    peaks={k:max((math.dist(vec(a,k),vec(b,k)) for a,b in pairs),default=0) for k in keys}
    changes=[dict(frame=r['frame'],ms=r['ms'],pose=r['pose']) for i,r in enumerate(rows) if i==0 or r['pose']!=rows[i-1]['pose']]
    stable=[r for r in rows if int(r['ms'])>int(rows[0]['ms'])+1400]
    span={k:max(float(r[k]) for r in stable)-min(float(r[k]) for r in stable) for k in ('normal','tangent')} if stable else {}
    checks={}
    if '-aim' in seg['label']:
        checks['stationary_root']=bool(stable) and max(span.values())<.02
        checks['aim_held']=all(r['aiming']=='1' for r in stable)
        checks['one_aim_intro']=sum(c['pose']=='aim_intro' for c in changes)<=1
    if '-blind' in seg['label']:
        checks['native_ammo_spent']=seg['ammo_after']<seg['ammo_before']
        checks['no_repeated_lowering']=bool(stable) and all(r['pose']=='blind' for r in stable)
    checks['no_head_or_pelvis_snap']=bool(pairs) and all(max(math.dist(vec(a,k),vec(b,k)) for k in ('head','pelvis')) < .08*max(1,(int(b['ms'])-int(a['ms']))/20) for a,b in pairs)
    reports.append(dict(**seg,frames=len(rows),continuous_pairs=len(pairs),max_frame_step_m=peaks,stable_span_m=span,changes=changes,checks=checks))
    failures.extend(seg['label']+': '+k for k,v in checks.items() if not v)
result=dict(status='passed' if not failures else 'failed',failures=failures,segments=reports)
(args.case/'stability-analysis.json').write_text(json.dumps(result,indent=2))
for r in reports:print(r['label'],r['frames'],r['stable_span_m'],{k:round(v,4) for k,v in r['max_frame_step_m'].items()},r['checks'])
raise SystemExit(', '.join(failures) if failures else 0)
