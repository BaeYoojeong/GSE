"""Read-only GLB/manifest compatibility audit. Does not run Blender or the game."""
import argparse, hashlib, json, math, struct
from pathlib import Path

COMPONENTS={5120:('b',1),5121:('B',1),5122:('h',2),5123:('H',2),5125:('I',4),5126:('f',4)}
COUNTS={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}

def glb(path):
    data=path.read_bytes()
    assert 12<=len(data)<=64*1024*1024,'file size'
    magic,version,length=struct.unpack_from('<III',data)
    assert magic==0x46546c67 and version==2 and length==len(data),'GLB header'
    chunks={};position=12
    while position<len(data):
        assert position+8<=len(data),'truncated chunk'
        size,kind=struct.unpack_from('<II',data,position);position+=8
        assert size%4==0 and position+size<=len(data) and kind not in chunks,'chunk bounds'
        chunks[kind]=data[position:position+size];position+=size
    document=json.loads(chunks[0x4e4f534a]);binary=chunks[0x004e4942]
    assert len(document['buffers'])==1 and 'uri' not in document['buffers'][0],'embedded buffer required'
    assert not document.get('extensionsRequired'),'required extension'
    return document,binary,hashlib.sha256(data).hexdigest()

def read(document,binary,index):
    accessor=document['accessors'][index]
    assert not accessor.get('sparse'),'sparse accessor'
    view=document['bufferViews'][accessor['bufferView']]
    code,size=COMPONENTS[accessor['componentType']];count=COUNTS[accessor['type']]
    stride=view.get('byteStride',size*count)
    offset=view.get('byteOffset',0)+accessor.get('byteOffset',0)
    assert stride>=size*count and accessor['count']>0,'accessor shape'
    result=[]
    for element in range(accessor['count']):
        at=offset+element*stride
        assert at+size*count<=view.get('byteOffset',0)+view['byteLength']<=len(binary),'accessor bounds'
        values=struct.unpack_from('<'+code*count,binary,at)
        assert all(math.isfinite(value) for value in values),'non-finite number'
        if accessor.get('normalized'):
            denominator={5120:127,5121:255,5122:32767,5123:65535}[accessor['componentType']]
            values=tuple(max(-1,value/denominator) for value in values)
        result.append(values)
    return result

def inspect(path):
    doc,binary,digest=glb(path)
    nodes=doc.get('nodes',[]);skins=doc.get('skins',[]);clips=doc.get('animations',[])
    assert len(nodes)<=4096 and len(skins)<=64 and len(clips)<=128,'scene limits'
    parents={}
    for index,node in enumerate(nodes):
        for child in node.get('children',[]):
            assert 0<=child<len(nodes) and child not in parents,'multiple parents'
            parents[child]=index
    for index in range(len(nodes)):
        seen=set();current=index
        while current in parents:
            assert current not in seen and len(seen)<256,'cyclic/deep hierarchy'
            seen.add(current);current=parents[current]
    vertex_total=0;triangles=0;mesh_nodes=[]
    for node in nodes:
        if 'mesh' not in node:continue
        mesh_nodes.append(node.get('name',''))
        for primitive in doc['meshes'][node['mesh']]['primitives']:
            assert primitive.get('mode',4)==4 and not primitive.get('targets'),'triangle/morph mode'
            attrs=primitive['attributes'];positions=read(doc,binary,attrs['POSITION'])
            assert 'COLOR_0' not in attrs,'vertex color unsupported'
            vertex_total+=len(positions)
            indices=read(doc,binary,primitive['indices']) if 'indices' in primitive else [(i,) for i in range(len(positions))]
            assert len(indices)%3==0 and all(0<=x[0]<len(positions) for x in indices),'indices'
            triangles+=len(indices)//3
            if 'skin' in node:
                skin=skins[node['skin']]
                joints=read(doc,binary,attrs['JOINTS_0']);weights=read(doc,binary,attrs['WEIGHTS_0'])
                assert len(joints)==len(weights)==len(positions),'skin attribute length'
                assert 0<len(skin['joints'])<=256,'joint limit'
                for js,ws in zip(joints,weights):
                    assert len(js)==len(ws)==4 and all(0<=j<len(skin['joints']) for j in js),'joint index'
                    assert all(w>=0 for w in ws) and sum(ws)>.00001,'weights'
                if 'inverseBindMatrices' in skin:
                    assert len(read(doc,binary,skin['inverseBindMatrices']))==len(skin['joints']),'inverse bind count'
            if 'material' in primitive:
                material=doc['materials'][primitive['material']];pbr=material.get('pbrMetallicRoughness',{})
                assert not pbr.get('baseColorTexture') and not material.get('normalTexture') and not material.get('emissiveTexture'),'texture unsupported'
                assert material.get('alphaMode','OPAQUE')=='OPAQUE','alpha unsupported'
    assert vertex_total<=250000 and triangles*3<=750000,'geometry limit'
    key_count=0
    for clip in clips:
        destinations=set()
        for channel in clip['channels']:
            target=channel['target'];slot=(target['node'],target['path'])
            assert slot not in destinations and target['path'] in ['translation','rotation','scale'],'channel target'
            destinations.add(slot)
            assert 'matrix' not in nodes[target['node']],'matrix animation'
            sampler=clip['samplers'][channel['sampler']]
            times=read(doc,binary,sampler['input']);values=read(doc,binary,sampler['output'])
            assert times[0][0]>=0 and all(a[0]<b[0] for a,b in zip(times,times[1:])),'key order'
            multiple=3 if sampler.get('interpolation','LINEAR')=='CUBICSPLINE' else 1
            assert len(values)==len(times)*multiple,'key shape'
            key_count+=len(values)
    assert key_count<=1000000,'animation key limit'
    return {'file':path.name,'sha256':digest,'nodes':len(nodes),'skins':len(skins),'triangles':triangles,
            'clips':[a.get('name','') for a in clips],'mesh_nodes':mesh_nodes}

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--root',required=True);parser.add_argument('--report',required=True)
    args=parser.parse_args();root=Path(args.root);manifest=json.loads((root/'asset_manifest.json').read_text(encoding='utf-8-sig'))
    assert manifest['schema']=='village_assets_1.0' and manifest['units']=='meters','manifest schema'
    report={'root':str(root),'files':[],'failed':[],'source_documents':{}}
    entries={e['id']:root/e.get('file','GLB/'+e['id']+'.glb') for category in ['npc_presets','animals','environment'] for e in manifest[category]}
    entries['NPC_Modular_Library']=root/manifest.get('npc_library_file','GLB/NPC_Modular_Library.glb')
    for path in sorted((root/'GLB').glob('*.glb')):
        try:report['files'].append(inspect(path))
        except Exception as error:report['failed'].append({'file':path.name,'reason':str(error)})
    for asset,path in entries.items():
        if not path.is_file():report['failed'].append({'asset':asset,'reason':'declared file missing'})
    library=next((x for x in report['files'] if x['file']=='NPC_Modular_Library.glb'),None)
    if library:
        for part in manifest['npc_parts']:
            if part['mesh'] not in library['mesh_nodes']:report['failed'].append({'part':part,'reason':'part node missing'})
    for name in ['asset_manifest.json','README_KO.md','검증_요약.md','rig_schema.json']:
        path=root/name
        if path.is_file():report['source_documents'][name]={'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'mtime_ns':path.stat().st_mtime_ns}
    report['catalog_assets']=len(entries);report['passed']=not report['failed']
    Path(args.report).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'catalog_assets':len(entries),'glb_files':len(report['files']),'failed':report['failed']},ensure_ascii=False))
    if report['failed']:raise SystemExit(1)
if __name__=='__main__':main()
