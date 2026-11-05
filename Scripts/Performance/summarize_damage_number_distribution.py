import csv,json,statistics
from pathlib import Path
root=Path(__file__).resolve().parents[2];folder=Path((root/'Saved/Performance/DamageNumbers/latest-distribution.txt').read_text().strip());r=json.loads((folder/'distribution.json').read_text());assert not r['error'],r['error']
keys=['FrameTime','GameThreadTime','RenderThreadTime','GPUTime','Slate/GameThread/SObjectWidget_Tick','Slate/GameThread/DrawPrePass','Slate/GameThread/DrawWindows_Private','DrawCall/SlateUI']
result=[]
for name in dict.fromkeys(c['name'] for c in r['cases']):
 cases=[c for c in r['cases'] if c['name']==name];values={k:[] for k in keys}
 for case in cases:
  with open(case['csv'],encoding='utf-8-sig') as f:
   for row in csv.DictReader(f):
    for k in keys:
     try:values[k].append(float(row[k]))
     except (ValueError,TypeError,KeyError):pass
 stats={k:{'mean':statistics.mean(v),'p95':sorted(v)[int((len(v)-1)*.95)],'samples':len(v)} for k,v in values.items() if v}
 result.append({'name':name,'native':{k:sum(c['native'][k] for c in cases) for k in ['actor_inputs','requests','merged','dropped','pending_dropped']},'stats':stats})
(folder/'distribution-summary.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
