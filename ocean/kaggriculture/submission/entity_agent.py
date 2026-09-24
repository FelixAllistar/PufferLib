"""Policy-v5 / entity-v3 / mode-2 executor-2 Kaggle agent.

The native library contains the production observation/mask/executor code.
Only the received public state and our own private inventory are imported.
"""
import ctypes as C
import json
import os
import sys
from pathlib import Path
os.environ['OPENBLAS_NUM_THREADS']='1'
os.environ['OMP_NUM_THREADS']='1'
import numpy as np

# Kaggle execs source without __file__, but supplies its filename to compile().
ROOT = Path(sys._getframe().f_code.co_filename).resolve().parent
CROPS = ('WHEAT','CARROT','TOMATO','STRAWBERRY','MELON')
PRODUCTS = CROPS + ('EGG','MILK','WOOL','FERTILIZER')
ANIMALS = ('GOOSE','COW','SHEEP')
ITEMS = PRODUCTS + ANIMALS
QUADS = ('NW','NE','SW','SE')
TILES = ('EMPTY','LOCKED','WEED','COOP','PASTURE','PLANT','ANIMAL')

class Unit(C.Structure):
    _fields_ = [('op',C.c_int),('arg',C.c_int),('n',C.c_int)]
class Order(C.Structure):
    _fields_ = [('op',C.c_int),('item',C.c_int),('n',C.c_int)]
class Action(C.Structure):
    _fields_ = [('farmer',Unit),('hands',Unit*240),('hand_count',C.c_int),
                ('market',Order*32),('market_count',C.c_int)]

def ints(values):
    return np.ascontiguousarray(values,dtype=np.int32)

def turn_number(observation):
    # Direct environment tests do not expand the shared `step` field for seat 1.
    return int(observation.get('step',24*int(observation['day'])+int(observation['hour'])))

class EntityModel:
    def __init__(self,path,hidden=256,layers=2,alignment=8):
        if hidden<8 or hidden%8 or layers<1 or alignment not in (4,8):
            raise ValueError('Invalid entity policy shape')
        flat=np.fromfile(path,dtype=np.float32)
        if not np.isfinite(flat).all():raise ValueError('Nonfinite checkpoint')
        self.hidden=hidden;self.layers_n=layers;pos=0
        def take(rows,cols):
            nonlocal pos
            pos=(pos+alignment-1)&~(alignment-1)
            a=flat[pos:pos+rows*cols].reshape(rows,cols);pos+=rows*cols
            return a
        specs=[(184,64,64),(56,32,32),(24,32,32),(32,16,16),
               (880,hidden,hidden),(hidden,hidden//2,748),
               (hidden,hidden//2,1230),(hidden,hidden//2,1)]
        self.mlps=[]
        for ni,nm,no in specs:
            a=take(nm,(ni+8)&~7);b=take(no,(nm+8)&~7)
            self.mlps.append((a[:,:ni],a[:,ni],b[:,:nm],b[:,nm]))
        self.layers=[take(3*hidden,hidden) for _ in range(layers)]
        if pos!=flat.size:raise ValueError(f'Checkpoint shape mismatch: {pos} != {flat.size}')
        self.state=np.zeros((layers,hidden),np.float32)
    def reset(self):self.state.fill(0)
    @staticmethod
    def sigmoid(x):return 1/(1+np.exp(-np.clip(x,-80,80)))
    def mlp(self,i,x,relu=True):
        a,ab,b,bb=self.mlps[i];y=np.maximum(x@a.T+ab,0)@b.T+bb
        return np.maximum(y,0) if relu else y
    def forward(self,obs):
        z=[self.mlp(0,np.concatenate((obs[:128],obs[1368:]))),
           self.mlp(1,obs[128:632].reshape(9,56)).ravel(),
           self.mlp(2,obs[632:824].reshape(8,24)).ravel(),
           self.mlp(3,obs[824:1368].reshape(17,32)).ravel()]
        x=self.mlp(4,np.concatenate(z))
        for i,w in enumerate(self.layers):
            a,g,hw=np.split(w@x,3)
            candidate=np.where(a>=0,a+.5,self.sigmoid(a))
            state=self.state[i]+self.sigmoid(g)*(candidate-self.state[i])
            highway=self.sigmoid(hw);x=highway*state+(1-highway)*x
            self.state[i]=state
        return np.concatenate([self.mlp(i,x,False) for i in (5,6,7)]).astype(np.float32)

class Controller:
    def __init__(self,library):
        self.lib=C.CDLL(str(library));l=self.lib
        ptr=C.c_void_p;i=C.c_int
        signatures={'export_create':([],ptr),'export_free':([ptr],None),
          'export_begin':([ptr,i,i,i],None),'export_farm':([ptr,i,ptr],None),
          'export_tile':([ptr,i,i,ptr],None),'export_private':([ptr,i,ptr,ptr],None),
          'export_inventory':([ptr,i,i,i,i],None),'export_market':([ptr,ptr,ptr,ptr,i],None),
          'export_view':([ptr,i,ptr],i),'export_action':([ptr,i,ptr,i,C.POINTER(Action)],None),
          'export_debug':([ptr,ptr,ptr],None),
          'export_rng':([ptr],C.c_uint32)}
        for name,(args,result) in signatures.items():
            f=getattr(l,name);f.argtypes=args;f.restype=result
        self.ctx=l.export_create();self.obs=np.empty(1424,np.float32)
        if not self.ctx:raise MemoryError('Cannot allocate controller')
    def close(self):
        if self.ctx:self.lib.export_free(self.ctx);self.ctx=None
    def observe(self,o):
        l=self.lib;x=self.ctx;p=int(o['player'])
        if p not in (0,1) or len(o['farms'])!=2:raise ValueError('Invalid player/farms')
        l.export_begin(x,turn_number(o),int(o['day']),int(o['hour']))
        for pid,f in enumerate(o['farms']):
            positions=[f['farmer']]+f['hands']
            if len(positions)>241:raise ValueError('Too many workers')
            if len(f['tiles'])!=10 or any(len(row)!=10 for row in f['tiles']):
                raise ValueError('Expected 10x10 board')
            a=ints([int(f['money']),sum(1<<QUADS.index(q) for q in f['unlocked_quadrants']),
                f['hires_today'],len(f['hands'])]+[v for xy in positions for v in xy])
            l.export_farm(x,pid,a.ctypes.data)
            for y,row in enumerate(f['tiles']):
                for xx,t in enumerate(row):
                    t={} if t is None else {'kind':t} if isinstance(t,str) else t
                    a=ints([TILES.index(t.get('kind','EMPTY')),
                        CROPS.index(t['crop']) if t.get('crop') else -1,
                        ANIMALS.index(t['animal']) if t.get('animal') else -1,
                        t.get('planted_day',0),t.get('placed_day',0),t.get('fertilized_until_day',-1),
                        t.get('watered_today',0),t.get('consecutive_unwatered',0),t.get('yield_units',0),
                        t.get('max_lifespan_step',0),t.get('consecutive_unfed',0),t.get('fed_today',0),
                        t.get('cared_today',0),t.get('fertilizer_available',0),t.get('pending_care_bonus',0)])
                    l.export_tile(x,pid,y*10+xx,a.ctypes.data)
        private=o['private'];shed=ints([private['shed'].get(k,0) for k in ITEMS])
        seeds=ints([private['seeds'].get(k,0) for k in CROPS])
        l.export_private(x,p,shed.ctypes.data,seeds.ctypes.data)
        if len(private['inventories'])!=1+len(o['farms'][p]['hands']):
            raise ValueError('Worker inventory count mismatch')
        for u,inv in enumerate(private['inventories']):
            for item,n in inv.items():
                if n:l.export_inventory(x,p,u,ITEMS.index(item),int(n))
        inv=ints([o['market']['inventory'][k] for k in PRODUCTS])
        prices=ints([o['market']['prices'][k] for k in PRODUCTS])
        shops=ints([SHOPS.index(k) for k in o['town']['unlocked_shops']])
        l.export_market(x,inv.ctypes.data,prices.ctypes.data,shops.ctypes.data,len(shops))
        if not l.export_view(x,p,self.obs.ctypes.data):raise ValueError('Missing episode history')
        return self.obs
    def act(self,p,logits,deterministic=True):
        logits=np.ascontiguousarray(logits,np.float32)
        if logits.shape!=(1979,) or not np.isfinite(logits).all():raise ValueError('Invalid logits')
        a=Action();self.lib.export_action(self.ctx,p,logits.ctypes.data,int(deterministic),C.byref(a))
        return action_json(a)

def action_json(a):
    def unit(u):
        op=UNIT_OPS[u.op]
        if op=='PLANT':return [op,CROPS[u.arg]]
        if op in ('PICKUP','PLACE'):return [op,ITEMS[u.arg],u.n]
        return [op]
    market=[]
    for o in a.market[:a.market_count]:
        op=MARKET_OPS[o.op]
        market.append([op] if op in ('HIRE','BUY_LAND') else
                      [op,(CROPS if op=='BUY_SEED' else ITEMS)[o.item],o.n])
    return {'farmer':unit(a.farmer),'hands':[unit(u) for u in a.hands[:a.hand_count]],'market':market}

# Numeric enums are checked against the native header by the export tests.
UNIT_OPS=('PASS','NORTH','SOUTH','EAST','WEST','PICKUP','DROP','PLANT','WATER',
          'HARVEST','FERTILIZE','BUILD_COOP','BUILD_PASTURE','DIG','PLACE','FEED','COLLECT_FERTILIZER','CARE')
MARKET_OPS=('BUY_SEED','BUY_PRODUCT','BUY_ANIMAL','SELL','HIRE','BUY_LAND')
SHOPS=('BAKERY','BRUNCH_SPOT','FARMERS_MARKET','ICE_CREAM_SHOP','PET_CAFE','PIZZA_SHOP','SMOOTHIE_SHOP','YARN_STORE')
_MODEL=None;_CONTROLLER=None;_LAST_STEP=-1;_LAST_ACTION=None;_DETERMINISTIC=True

def agent(observation,configuration=None):
    global _MODEL,_CONTROLLER,_LAST_STEP,_LAST_ACTION,_DETERMINISTIC
    step=turn_number(observation)
    if _MODEL is None:
        meta=json.loads((ROOT/'policy_metadata.json').read_text())
        if (meta['policy_version'],meta['observation_version'],meta['macro_mode'],meta['macro_executor_version'])!=(5,3,2,2):
            raise ValueError('Unsupported policy contract')
        _MODEL=EntityModel(ROOT/'model.bin',meta['hidden_size'],meta['num_layers'],meta['param_alignment'])
        _DETERMINISTIC=meta['deterministic']
    if _CONTROLLER is None or step==0 and _LAST_STEP!=0 or step<_LAST_STEP:
        if _CONTROLLER:_CONTROLLER.close()
        _CONTROLLER=Controller(ROOT/'entity_bridge.so');_MODEL.reset();_LAST_STEP=-1
    if step==_LAST_STEP:return _LAST_ACTION
    obs=_CONTROLLER.observe(observation)
    _LAST_ACTION=_CONTROLLER.act(int(observation['player']),_MODEL.forward(obs),_DETERMINISTIC)
    _LAST_STEP=step
    return _LAST_ACTION
