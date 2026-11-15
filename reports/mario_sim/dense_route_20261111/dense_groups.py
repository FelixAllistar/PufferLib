import struct,sys,glob
def load(p):
    b=open(p,'rb').read()
    magic,ver,count,gs=struct.unpack_from('<IIII',b,0)
    assert magic==0x54434831 and gs==96 and len(b)==40+count*96
    out=[]
    for k in range(count):
        key,area,level,section,elig,res,fast,slow,att,wins,deaths,dstamp,last,prob=struct.unpack_from('<QIiiii4xddQQdQQd',b,40+k*96)
        out.append(dict(key=key,area=area,level=level,section=section,res=res,att=att,wins=wins,prob=prob))
    return out
f=sorted(glob.glob('*.teacher'))[-1]
gs=load(f);name=lambda l:f"{l//4+1}-{l%4+1}"
dense=[g for g in gs if g['key']>>63];reg=[g for g in gs if not g['key']>>63]
print(f,'groups',len(gs),'dense',len(dense),'dense prob %.3f'%sum(g['prob'] for g in dense),'dense attempts',sum(g['att'] for g in dense),'wins',sum(g['wins'] for g in dense))
marks={15:{31629:'jump',31659:'hop off',31680:'floor'},31:{64993:'pipe1',65430:'pipe2',65795:'pipe3',66661:'water exit'},27:{}}
for lv in (15,27,31):
    ds=sorted([g for g in dense if g['level']==lv],key=lambda g:g['key']&0xffffffff)
    f0=ds[0]['key']&0xffffffff
    print('--',name(lv),'dense',len(ds),'frames',f0,'..',ds[-1]['key']&0xffffffff)
    line=[]
    B=10
    for i in range(0,len(ds),B):
        ch=ds[i:i+B];a=sum(g['att'] for g in ch);w=sum(g['wins'] for g in ch);fr=ch[0]['key']&0xffffffff
        tag=''.join(' <'+v+'>' for k,v in marks[lv].items() if fr<=k<fr+B*4)
        line.append(f"{fr}:{w/max(1,a):.2f}({a}){tag}")
    for i in range(0,len(line),6): print('   '+'  '.join(line[i:i+6]))
for lv in (15,27,31):
    rs=sorted([g for g in reg if g['level']==lv],key=lambda g:(g['area'],g['section']))
    print('regular',name(lv),' '.join(f"{g['area']&0xffff:04x}s{g['section']}:{g['wins']/max(1,g['att']):.2f}({g['att']})" for g in rs))
