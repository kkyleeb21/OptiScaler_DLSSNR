"""Report missing or changed baseline files; never silently accept later edits."""
import argparse, hashlib, json
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('manifest',type=Path)
p.add_argument('candidate',type=Path)
a=p.parse_args()
manifest=json.loads(a.manifest.read_text(encoding='utf-8'))
changes=[]
for row in manifest['files']:
    target=a.candidate/row['path']
    if not target.is_file(): changes.append({'path':row['path'],'status':'missing'}); continue
    if hashlib.sha256(target.read_bytes()).hexdigest()!=row['sha256']:
        changes.append({'path':row['path'],'status':'changed_review_required'})
print(json.dumps({'checked':len(manifest['files']),'changes':changes},indent=2))
raise SystemExit(bool(changes))
