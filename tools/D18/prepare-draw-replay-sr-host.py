from pathlib import Path
r=Path('E:/DLSSNR/builds/D18_Wildlands_DrawReplayV7_20260913')
p=r/'sr-host.cpp';s=p.read_text()
s=s.replace('const bool transition=', 'const bool draw6=wcscmp(argv[3],L"draw6")==0;\n const bool transition=',1)
s=s.replace('const char vsSource[]=', 'const char* vsSource=draw6?"struct O{float4 p:SV_Position;float2 uv:TEXCOORD0;}; O main(float3 p:POSITION,float2 uv:TEXCOORD0){O o;o.p=float4(p,1);o.uv=uv;return o;}":',1)
s=s.replace('D3DCompile(vsSource,sizeof(vsSource)-1','D3DCompile(vsSource,strlen(vsSource)')
needle='c->VSSetShader(vertex.Get(),nullptr,0);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);'
insert='''ComPtr<ID3D11Buffer> geometryBuffer;ComPtr<ID3D11InputLayout> geometryLayout;
 if(draw6){
  float vertices[][5]={{-1,1,0,0,0},{1,1,0,1,0},{-1,-1,0,0,1},{-1,-1,0,0,1},{1,1,0,1,0},{1,-1,0,1,1}};
  D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(vertices);bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;D3D11_SUBRESOURCE_DATA init{vertices,0,0};
  if(FAILED(d->CreateBuffer(&bd,&init,&geometryBuffer)))return 70;
  D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0}};
  if(FAILED(d->CreateInputLayout(elements,2,vsCode->GetBufferPointer(),vsCode->GetBufferSize(),&geometryLayout)))return 71;
  auto v=geometryBuffer.Get();UINT stride=20,offset=0;c->IASetVertexBuffers(0,1,&v,&stride,&offset);c->IASetInputLayout(geometryLayout.Get());
 }
 '''
assert needle in s;s=s.replace(needle,insert+needle,1)
s=s.replace('}else c->Draw(3,0);','}else c->Draw(draw6?6:3,0);')
p.write_text(s)
p=r/'test-sr.py';s=p.read_text();s=s.replace("('normal','dynamic','4k'","('draw6','normal','dynamic','4k'")
s=s.replace("assert not any(e.get('code',0) for e in events),events[-8:]", "assert not any(e.get('code',0) for e in events),events[-8:]\n if mode=='draw6':\n  trace=[json.loads(l) for l in (d/'D18ExecutionTrace.jsonl').read_text().splitlines()]\n  assert any(e['stage']=='sr_replay_command' and e['a']==1 and e['b']==6<<32 for e in trace)")
p.write_text(s)
