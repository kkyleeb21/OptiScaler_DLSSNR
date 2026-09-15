"""Read-only project storage inventory. Never follows reparse points or deletes files."""
from pathlib import Path
import argparse,collections,datetime,heapq,json,os,stat
p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=Path('E:/DLSSNR'));p.add_argument('--output',type=Path,required=True);a=p.parse_args()
root=a.root.resolve();assert root==Path('E:/DLSSNR').resolve() and not a.output.exists()
groups=collections.defaultdict(lambda:dict(files=0,logical_bytes=0,unique_file_bytes=0));extensions=collections.Counter();seen=set();largest=[];skipped=[];errors=[];n=0
stack=[root]
while stack:
    directory=stack.pop()
    try:
        with os.scandir(directory) as entries:
            for e in entries:
                path=Path(e.path)
                try:
                    s=e.stat(follow_symlinks=False)
                    if s.st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT:
                        skipped.append(str(path));continue
                    if e.is_dir(follow_symlinks=False):stack.append(path);continue
                    if not e.is_file(follow_symlinks=False):continue
                    # Windows DirEntry's cached stat can report zero inode/link count.
                    s=os.stat(path,follow_symlinks=False)
                    relative=path.relative_to(root);n+=1;identity=(s.st_dev,s.st_ino);fresh=identity not in seen;seen.add(identity)
                    for depth in [1,2,3]:
                        if len(relative.parts)<depth:continue
                        key='/'.join(relative.parts[:depth]);g=groups[key];g['files']+=1;g['logical_bytes']+=s.st_size;g['unique_file_bytes']+=s.st_size if fresh else 0
                    extensions[path.suffix.lower() or '[none]']+=s.st_size
                    item=(s.st_size,relative.as_posix(),s.st_nlink,datetime.datetime.fromtimestamp(s.st_mtime,datetime.timezone.utc).isoformat())
                    if len(largest)<100:heapq.heappush(largest,item)
                    elif item>largest[0]:heapq.heapreplace(largest,item)
                except OSError as ex:errors.append(dict(path=str(path),error=str(ex)))
    except OSError as ex:errors.append(dict(path=str(directory),error=str(ex)))
usage=__import__('shutil').disk_usage(root)
result=dict(schema='d18-workspace-storage-audit-v1',root=str(root),created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=n,volume_free_bytes=usage.free,groups=dict(sorted(groups.items())),extensions=dict(extensions.most_common()),largest_files=[dict(bytes=s,path=p,hardlinks=l,modified_utc=t) for s,p,l,t in sorted(largest,reverse=True)],skipped_reparse_points=skipped,errors=errors,boundary='Unique-file bytes deduplicate hardlinks by file identity, not allocated clusters, compression, or guaranteed reclaimable bytes. Metadata inventory only; active parallel work can change while scanning.')
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2),encoding='utf-8')
shown=0
for k,v in sorted(groups.items(),key=lambda kv:kv[1]['logical_bytes'],reverse=True):
    if k.count('/')<=1 and shown<30:
        print(f"{v['logical_bytes']/2**30:9.2f} GiB {v['files']:7} files {k}");shown+=1
print('FILES',n,'ERRORS',len(errors),'SKIPPED_REPARSE',len(skipped),'FREE_GIB',round(usage.free/2**30,2))
