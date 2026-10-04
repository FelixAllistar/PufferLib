"""Public synthetic GLB fixtures; standard library only, no private art."""
import json
import math
import struct


def encode(document, binary):
    document['buffers'] = [{'byteLength': len(binary)}]
    text = json.dumps(document).encode()
    text += b' ' * (-len(text) % 4)
    binary += b'\0' * (-len(binary) % 4)
    return struct.pack('<III', 0x46546C67, 2, 28+len(text)+len(binary)) + struct.pack('<II',len(text),0x4E4F534A)+text+struct.pack('<II',len(binary),0x004E4942)+binary


def synthetic(mode='STEP', malformed=None):
    d={'asset':{'version':'2.0'}, 'bufferViews':[], 'accessors':[], 'nodes':[], 'skins':[], 'scenes':[{'nodes':[0,7,8,9]}], 'scene':0}
    b=bytearray()
    def add(values, kind, component=5126, normalized=False):
        width={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[kind]
        code={5126:'f',5125:'I',5123:'H',5121:'B'}[component]
        b.extend(b'\0'*(-len(b)%4)); offset=len(b); b.extend(struct.pack('<'+code*len(values),*values))
        vi=len(d['bufferViews']); d['bufferViews'].append({'buffer':0,'byteOffset':offset,'byteLength':len(b)-offset})
        ai=len(d['accessors']); d['accessors'].append({'bufferView':vi,'componentType':component,'type':kind,'count':len(values)//width})
        if normalized: d['accessors'][-1]['normalized']=True
        return ai
    # Deliberately shuffled joint IDs, non-identity inverse binds and a mesh-node
    # transform that must cancel in scene-space skinning.
    for i in range(7):
        d['nodes'].append({'name':f'joint/{i}','translation':[0,i*.2,0], 'scale':[2,1,1], **({'children':list(range(1,7))} if i==0 else {})})
    d['nodes'].extend([{'name':'skinned','mesh':0,'skin':0,'translation':[20,30,40]}, {'name':'prop','mesh':1,'translation':[2,0,0]}, {'name':'rigid','mesh':1,'matrix':[1,0,0,0,0,2,0,0,0,0,3,0,4,0,0,1]}])
    order=[6,2,5,0,4,1,3]
    inverse=[]
    for j in order:
        inverse.extend([1,0,0,0,0,1,0,0,0,0,1,0,0,-.1*j,0,1])
    ibm=add(inverse,'MAT4'); d['skins']=[{'joints':order,'inverseBindMatrices':ibm}]
    p=add([0,0,0,1,0,0,0,1,0],'VEC3'); n=add([1/math.sqrt(2),1/math.sqrt(2),0]*3,'VEC3')
    j0=add([0,1,2,3]*3,'VEC4',5121); j1=add([4,5,6,0]*3,'VEC4',5121)
    w0=add([1/7]*4*3,'VEC4'); w1=add(([1/7]*3+[0])*3,'VEC4'); ix=add([0,1,2],'SCALAR',5123)
    d['meshes']=[{'primitives':[{'attributes':{'POSITION':p,'NORMAL':n,'JOINTS_0':j0,'WEIGHTS_0':w0,'JOINTS_1':j1,'WEIGHTS_1':w1},'indices':ix}]}, {'primitives':[{'attributes':{'POSITION':p,'NORMAL':n},'indices':ix}]}]
    times=add([0,1],'SCALAR'); scales=add([0,0,0,1,1,1],'VEC3'); move=add([0,0,0,0,0,2],'VEC3'); rotate=add([0,0,0,1,0,0,1,0],'VEC4')
    cubic=add([0,0,0,0,0,0,2,0,0,0,0,0,1,0,0,0,0,0],'VEC3')
    d['animations']=[{'name':'Fixture','samplers':[{'input':times,'output':scales,'interpolation':mode},{'input':times,'output':move,'interpolation':'LINEAR'},{'input':times,'output':rotate,'interpolation':'LINEAR'},{'input':times,'output':cubic,'interpolation':'CUBICSPLINE'}], 'channels':[{'sampler':0,'target':{'node':8,'path':'scale'}},{'sampler':1,'target':{'node':8,'path':'translation'}},{'sampler':2,'target':{'node':1,'path':'rotation'}},{'sampler':3,'target':{'node':2,'path':'translation'}}]}]
    cubic_rotation=add([0,0,0,0,0,0,0,1,0,0,0,0, 0,0,0,0,math.sqrt(.5),0,0,math.sqrt(.5),0,0,0,0],'VEC4')
    d['animations'][0]['samplers'].append({'input':times,'output':cubic_rotation,'interpolation':'CUBICSPLINE'})
    d['animations'][0]['channels'].append({'sampler':4,'target':{'node':3,'path':'rotation'}})
    if malformed=='parents': d['nodes'][7]['children']=[1]
    if malformed=='duplicate_child': d['nodes'][0]['children'].append(1)
    if malformed=='emissive':
        d['materials']=[{'emissiveFactor':[1,0,0]}]
        d['meshes'][0]['primitives'][0]['material']=0
    if malformed=='affine': d['nodes'][9]['matrix'][3]=1
    if malformed=='material':
        d['materials']=[{'pbrMetallicRoughness':{'baseColorFactor':[2,1,1,1]}}]
        d['meshes'][0]['primitives'][0]['material']=0
    if malformed=='zero_cubic':
        v=d['bufferViews'][d['accessors'][cubic_rotation]['bufferView']]
        struct.pack_into('<4f',b,v['byteOffset']+16*4,0,0,0,-1)
    if malformed=='empty': d['accessors'][times]['count']=0
    if malformed=='bounds': d['accessors'][p]['byteOffset']=2**62
    if malformed=='cycle': d['nodes'][1]['children']=[0]
    if malformed=='joints': d['skins'][0]['joints']=order[:-1]
    if malformed=='pair': del d['meshes'][0]['primitives'][0]['attributes']['WEIGHTS_1']
    if malformed=='duplicate': d['animations'][0]['channels'].append(d['animations'][0]['channels'][0])
    if malformed=='matrix_channel': d['animations'][0]['channels'][0]['target']['node']=9
    if malformed=='external': d['images']=[{'uri':'../unrequested.png'}]
    if malformed=='weights':
        a=d['accessors'][w1]; v=d['bufferViews'][a['bufferView']]; struct.pack_into('<f',b,v['byteOffset'],.9)
    return encode(d,b)
