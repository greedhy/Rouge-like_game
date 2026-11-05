"""Summarize diagnostic PIE captures, preserving their measurement limitations."""
import json,statistics
from pathlib import Path
root=Path(__file__).resolve().parents[2];folder=Path((root/'Saved/Performance/DamageNumbers/latest-baseline.txt').read_text().strip());r=json.loads((folder/'baseline.json').read_text());assert not r['error'],r['error'];assert len(r['cases'])==9
rows=[]
def percentile(v,p):
 a=sorted(v);return a[min(len(a)-1,int((len(a)-1)*p))]
for rate in [0,100,500]:
 cases=[x for x in r['cases'] if x['rate']==rate]
 frames=[v for x in cases for v in x['frame_ms']];dispatch=sum(sum(x['dispatch_ms']) for x in cases);n=sum(x['sample_sent'] for x in cases)
 rows.append({'requests_per_second':rate,'p50_callback_ms':percentile(frames,.5),'p95_callback_ms':percentile(frames,.95),'p99_callback_ms':percentile(frames,.99),'dispatch_us_per_request':dispatch*1000/n if n else 0,'created_requests_total':sum(x['total_sent'] for x in cases),'remaining_registered':[x['registered_after_drain'] for x in cases]})
(folder/'summary.json').write_text(json.dumps(rows,indent=2));print(json.dumps(rows,indent=2));print(folder)
