import argparse,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('log');p.add_argument('--output',required=True);a=p.parse_args()
b=Path(a.log).read_bytes();assert len(b)<=4*1024*1024
rows=[json.loads(x) for x in b.decode('utf-8-sig').splitlines()];assert len(rows)<=256
out=dict(schema='d18-dx11-debug-summary-v1',records=len(rows),at_limit=len(rows)>=256,queue_available=any(x['event']=='info_queue_status' and x['code']==0 for x in rows),layer_unavailable=any(x['event']=='debug_layer_unavailable' for x in rows),messages=[x for x in rows if x['event']=='info_queue_message'],creation=[x for x in rows if x['event']!='info_queue_message'],boundary='Warning/error messages and creation status only; absent messages do not establish API or gameplay correctness')
Path(a.output).write_text(json.dumps(out,indent=2),encoding='utf-8')
