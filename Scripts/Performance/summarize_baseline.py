"""Summarize fresh CSV captures, retaining inclusive scope semantics."""
import csv,json,math,statistics,argparse
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('files',nargs='+',type=Path);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
def stats(values):
 values=sorted(values);n=len(values)
 return {'frames':n,'mean_ms':statistics.mean(values),'p50_ms':values[math.ceil(n*.5)-1],'p95_ms':values[math.ceil(n*.95)-1],'p99_ms':values[math.ceil(n*.99)-1],'max_ms':values[-1]}
reports=[]
for path in a.files:
 with path.open(encoding='utf-8-sig') as f:
  reader=csv.DictReader(f);keys=[k for k in reader.fieldnames if k in ['FrameTime','GameThreadTime','RenderThreadTime','GPUTime'] or k.startswith('MonsterPerf/')]
  data={k:[] for k in keys}
  for row in reader:
   try:frame=float(row['FrameTime'])
   except (ValueError,TypeError,KeyError):continue
   if frame<=0:continue
   for k in keys:
    try:data[k].append(float(row[k]))
    except (ValueError,TypeError):pass
 assert any(k.startswith('MonsterPerf/') for k in data),'Missing custom scopes: '+str(path)
 reports.append({'file':str(path),'metrics':{k:stats(v) for k,v in data.items() if v},'over_16_667_ms_percent':100*sum(v>1000/60 for v in data['FrameTime'])/len(data['FrameTime'])})
a.out.write_text(json.dumps(reports,indent=2));print(json.dumps(reports,indent=2))
