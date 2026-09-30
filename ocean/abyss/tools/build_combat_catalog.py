#!/usr/bin/env python3
"""Generate T1 data and the bot's matching catalog from pinned local snapshots."""
import sys, json, copy, math, hashlib
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
BASE = ROOT / 'ocean/abyss'
OUT = BASE
sys.path.insert(0, str(BASE / 'tools'))
from build_scenario_catalog import build, f

# Channels: optimal, falloff, tracking, velocity, signature, lock range,
# scan resolution, scram points. Guidance data is retained but irrelevant to
# the supported turret-only player fit.
EFFECTS = [
 ('behaviorEnergyNeutralizer', {'energyNeutralizerAmount': 8}),
 ('npcTrackingDisruptor', {'maxRangeBonus':0,'falloffBonus':1,'trackingSpeedBonus':2}),
 ('behaviorWebifier', {'speedFactor':3}),
 ('behaviorTargetPainter', {'signatureRadiusBonus':4}),
 ('behaviorSensorDampener', {'maxTargetRangeBonus':5,'scanResolutionBonus':6}),
 ('behaviorWarpScramble', {'behaviorWarpScrambleStrength':7}),
 ('npcGuidanceDisruptor', {}),
]

def generate():
 (OUT/"data").mkdir(parents=True,exist_ok=True)
 data=json.loads((BASE/'data/npc_types_qsna.json').read_text())
 spawn=json.loads((BASE/'data/spawn_stats_qsna.json').read_text())
 catalog=json.loads((BASE/'data/npc_catalog.json').read_text())
 attrs=data['dogmaAttributes']
 byname={v['name']: (int(k),{attrs[a]['name']:x for a,x in v['dogma_attributes'].items()}) for k,v in data['data'].items()}
 # Refresh the auxiliary from dogma; its CSV orbit speed is absent.
 name='Vila Swarmer'; tid,a=byname[name]
 drone=copy.deepcopy(catalog[0]); drone.update(name=name,hull_class='Drone')
 for key in drone:
  if isinstance(drone[key],(int,float)): drone[key]=0
  elif isinstance(drone[key],list): drone[key]=[0]*len(drone[key])
 for key,attr in [('signature_radius_m','signatureRadius'),('shield_hp','shieldCapacity'),('armor_hp','armorHP'),('structure_hp','hp'),('max_speed_mps','maxVelocity'),('orbit_range_m','npcBehaviorMaximumCombatOrbitRange'),('turret_optimal_m','maxRange'),('turret_falloff_m','falloff'),('turret_tracking','trackingSpeed')]: drone[key]=a[attr]
 drone.update(orbit_speed_mps=1000,turret_cycle_s=a['speed']/1000,local_repair_layer=-1,remote_repair_layer=-1)
 mix=[a.get(x+'Damage',0) for x in ['em','thermal','kinetic','explosive']]
 drone['turret_dps']=drone['dps']=sum(mix)*a['damageMultiplier']/drone['turret_cycle_s']
 drone['turret_damage_mix']=drone['damage_mix']=[v/sum(mix) for v in mix]
 for key,prefix in [('shield_resists','shield'),('armor_resists','armor'),('structure_resists','')]:
  drone[key]=[1-a.get(prefix+(x.capitalize() if prefix else x)+'DamageResonance',1) for x in ['em','thermal','kinetic','explosive']]
 catalog=[r for r in catalog if r['name']!='Vila Swarmer']
 catalog.append(drone)
 extras={}
 for row in catalog:
  tid,a=byname[row['name']]
  # Use the pinned dogma snapshot for turret cycles and damage: the older CSV
  # disagrees for some types (e.g. Hunter). Keep legacy T0 tables unchanged.
  raw=[a.get(x+'Damage',0) for x in ['em','thermal','kinetic','explosive']]
  if a.get('speed',0)>0 and sum(raw)>0:
   row['turret_cycle_s']=a['speed']/1000
   row['turret_dps']=sum(raw)*a.get('damageMultiplier',1)/row['turret_cycle_s']
   row['turret_damage_mix']=[v/sum(raw) for v in raw]
   for key,attr in [('turret_optimal_m','maxRange'),('turret_falloff_m','falloff'),('turret_tracking','trackingSpeed')]:row[key]=a.get(attr,0)
   row['dps']=row['turret_dps']+row['missile_dps']
  # Blank aggregate weapon DPS is not blank missile DPS (Marshal = 427.5).
  if row['missile_dps']:
   row['missile_explosion_radius_m']*=a.get('missileEntityAoeCloudSizeMultiplier',1)
   row['missile_explosion_velocity_mps']*=a.get('missileEntityAoeVelocityMultiplier',1)
  effects=[]
  for prefix,channels in EFFECTS:
   cycle=a.get(prefix+'Duration',0)/1000
   if cycle<=0: continue
   values=[0.]*9
   for attr,ch in channels.items(): values[ch]=a.get(attr,0)/(100 if ch<7 else 1)
   effects.append(dict(cycle=cycle,optimal=a.get(prefix+'Range',0),falloff=a.get(prefix+'Falloff',0),values=values,kind=prefix))
  # shieldCharge=0 is a ubiquitous dogma default, NOT an instruction to spawn
  # every NPC without shields. Apply damaged-state fields only where damage is
  # explicit. Raw shield charge is then the encounter's remaining shield HP.
  damaged=a.get('armorDamage',0)>0 or a.get('damage',0)>0
  initial=[min(row['shield_hp'],a.get('shieldCharge',row['shield_hp'])) if damaged else row['shield_hp'], max(0,row['armor_hp']-a.get('armorDamage',0)),max(0,row['structure_hp']-a.get('damage',0))]
  extras[row['name']]=dict(type_id=tid,effects=effects,initial=initial,maximum=[row['shield_hp'],row['armor_hp'],row['structure_hp']],cycle=row['turret_cycle_s'] or a.get('speed',1000)/1000,spool_step=a.get('damageMultiplierBonusPerCycle',0),spool_max=1+a.get('damageMultiplierBonusMax',0),drone_active=int(a.get('npcDroneBandwidth',0)),drone_total=int(a.get('npcDroneCapacity',0)),vorton_radius=a.get('aoeCloudSize',0) if a.get('VortonArcTargets',0) else 0,vorton_velocity=a.get('aoeVelocity',0),vorton_drf=a.get('aoeDamageReductionFactor',0))
  if len(effects)>4: raise ValueError('too many effects '+row['name'])
 (OUT/'data/combat_catalog.json').write_text(json.dumps(catalog,indent=2)+'\n')
 build(BASE/'data/recorded/episodes.json',OUT/'data/combat_catalog.json',BASE/'data/trajectory_calibration.json',OUT/'generated_combat_catalog.h',include_all=True)
 # Same ordering as legacy generator, preserve original 18 identifiers.
 episodes=json.loads((BASE/'data/recorded/episodes.json').read_text())['episodes']
 old=sorted({e['name'] for ep in episodes for r in ep['rooms'] for e in r['entities'] if e['role']=='HostileNpc'})
 names=old+sorted({r['name'] for r in catalog}-set(old)); indexes={n:i for i,n in enumerate(names)}
 rows={r['name']:r for r in catalog}
 lines=['// Generated. Channels defined in tools/generate.py.', 'typedef struct { float cycle, optimal, falloff, values[9]; } NpcEffect;', 'typedef struct { float initial[3], cycle, spool_step, spool_max; int drone_active, drone_total; float vorton_radius, vorton_velocity, vorton_drf; int effect_count; NpcEffect effects[4]; } NpcMechanics;', 'static const NpcMechanics AB_NPC_MECHANICS[GENERATED_NPC_COUNT] = {']
 for n in names:
  x=extras[n];effects=['{'+','.join([f(e['cycle']),f(e['optimal']),f(e['falloff']),'{'+','.join(map(f,e['values']))+'}'])+'}' for e in x['effects']]
  lines.append(' {'+'{'+','.join(map(f,x['initial']))+'},'+','.join(map(f,[x['cycle'],x['spool_step'],x['spool_max']]))+f",{x['drone_active']},{x['drone_total']},"+','.join(map(f,[x['vorton_radius'],x['vorton_velocity'],x['vorton_drf']]))+f",{len(effects)},"+'{'+','.join(effects or ['{0,0,0,{0}}'])+'}}, // '+n)
 lines+=['};',f'#define AB_SWARMER_INDEX {indexes["Vila Swarmer"]}', 'typedef struct { int npc, min, max; float chance, mean, weights[9]; } CalmSpawnChoice;', 'typedef struct { const char* name; int samples, min, max, count; CalmSpawnChoice choices[20]; } CalmArchetype;', 'static const CalmArchetype CALM_ARCHETYPES[] = {']
 report=[]
 for name,s in spawn['tiers']['calm']['spawns'].items():
  if not s['roomsCount']:continue
  choices=[]
  for tid,v in s['enemies'].items():
   if not v['chance']:continue
   n=data['data'][tid]['name']; row=rows[n]
   if row['turret_dps']+row['missile_dps']<=0: raise ValueError('unresolved damage: '+n)
   lo,hi=int(v['min']),int(v['max'])
   if hi>8: raise ValueError('count support exceeds 8')
   mean=min(hi,max(lo,v['avg']/v['chance']))
   left,right=-40.,40.
   for _ in range(80):
    slope=(left+right)/2
    ws=[math.exp(slope*(k-(hi if slope>0 else lo))) for k in range(lo,hi+1)]
    m=sum(k*w for k,w in zip(range(lo,hi+1),ws))/sum(ws)
    if m<mean:left=slope
    else:right=slope
   weights=[0.]*9
   for k,w in zip(range(lo,hi+1),ws):weights[k]=w/sum(ws)
   choices.append('{'+f"{indexes[n]},{lo},{hi},{f(v['chance'])},{f(v['avg'])},"+'{'+','.join(map(f,weights))+'}}')
  if len(choices)>20:raise ValueError('choice capacity')
  lines.append(' {'+json.dumps(name)+f",{s['roomsCount']},{s['minimumEnemiesPerRoom']},{s['maximumEnemiesPerRoom']},{len(choices)},"+'{'+','.join(choices)+'}},')
  report.append(dict(name=name,samples=s['roomsCount'],min=s['minimumEnemiesPerRoom'],max=s['maximumEnemiesPerRoom']))
 lines+=['};','#define CALM_ARCHETYPE_COUNT (sizeof(CALM_ARCHETYPES)/sizeof(CALM_ARCHETYPES[0]))']
 (OUT/'generated_mechanics.h').write_text('\n'.join(lines)+'\n')
 bot=[dict(name=n,index=indexes[n],signature=rows[n]['signature_radius_m'],**extras[n]) for n in names]
 (OUT/'data/bot_catalog.json').write_text(json.dumps(bot,indent=2)+'\n')
 source_hashes={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [BASE/'data/npc_stats.csv',BASE/'data/npc_types_qsna.json',BASE/'data/spawn_stats_qsna.json']}
 (OUT/'data/tier_coverage.json').write_text(json.dumps(dict(archetypes=report,npcs=len(names),source_hashes=source_hashes,source='pinned QSNA dogma + CSV; synthetic constrained compositions'),indent=2)+'\n')
 print(f'Generated {len(names)} NPCs, {len(report)} T1 archetypes')
if __name__=='__main__':generate()
