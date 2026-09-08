"""Independently validate fixed-scene NR f32 readbacks and lifecycle events."""
import array,hashlib,json,math,pathlib,sys
out=pathlib.Path(sys.argv[1]).resolve()
result=json.loads((out/'result.json').read_text())
events=[json.loads(s) for s in (out/'events.jsonl').read_text().splitlines()]
assert result.get('returncode')==0 and not result.get('timeout')
for key,value,count in [('nr_create',1,1),('nr_evaluate',1,60),('nr_gpu_completion',0,60),('nr_release',1,1),('nr_backend_shutdown',0,1),('nr_dll_unload',1,1),('nr_lifecycle_complete',1,1)]:
    found=[e['value'] for e in events if e['event']==key]
    assert len(found)==count and all(v==value for v in found),(key,found)
frames=[e for e in events if e['event']=='nr_frame']
assert [e['frame'] for e in frames]==list(range(60))
stats=[]
for frame in range(60):
    raw=(out/f'nr-frame-{frame}.f32').read_bytes();assert len(raw)==256*256*4*4
    values=array.array('f');values.frombytes(raw)
    if sys.byteorder!='little':values.byteswap()
    assert all(math.isfinite(v) for v in values),frame
    assert all(v!=-1000 for v in values),frame
    means=[sum(values[ch::4])/(256*256) for ch in range(4)]
    diff=0
    for y in range(256):
        for x in range(256):
            expected=(0.1+0.6*x/256+(0.15 if (frame//10)%2 else 0),0.1+0.6*y/256,0.6 if (x//8+y//8)%2 else 0.2)
            diff+=sum(abs(values[(y*256+x)*4+c]-expected[c]) for c in range(3))
    diff/=256*256*3
    assert diff>0.0001,('possible passthrough',frame,diff)
    stats.append(dict(frame=frame,sha256=hashlib.sha256(raw).hexdigest(),rgba_means=means,mean_abs_input_difference=diff))
response=stats[10]['rgba_means'][0]-stats[0]['rgba_means'][0]
assert response>0.05,('input response missing',response)
for a,b in [(0,20),(0,40),(10,30),(10,50)]:
    assert stats[a]['sha256']==stats[b]['sha256'],('reset repeatability differs',a,b)
summary=dict(status='PASS',frames=60,shape=[256,256,4],format='RGBA32F',dispatches=frames[-1]['registered_dispatches'],all_channels_finite=True,no_sentinel=True,red_mean_change_for_input_step=response,reset_repeats_bit_identical=True,lifecycle_verified=True,limitations=['Native internal ABI research adapter; no public DX11 Init integration','Fixed device, resolution and scene only','No D3D12 differential reference or gameplay validation'],frames_detail=stats)
(out/'readback-validation.json').write_text(json.dumps(summary,indent=2))
print(json.dumps({k:v for k,v in summary.items() if k!='frames_detail'},indent=2))
