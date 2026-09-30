import importlib.util,json,math,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
ENV=ROOT/'ocean/abyss'
class DataTests(unittest.TestCase):
 def test_reproducible_catalog_and_bot_identity(self):
  spec=importlib.util.spec_from_file_location('t1_generate',ENV/'tools/build_combat_catalog.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
  with tempfile.TemporaryDirectory() as tmp:
   m.OUT=Path(tmp);m.generate()
   for name in ['generated_combat_catalog.h','generated_mechanics.h','data/combat_catalog.json','data/bot_catalog.json','data/tier_coverage.json']:
    self.assertEqual((ENV/name).read_bytes(),(m.OUT/name).read_bytes(),name)
  catalog=json.loads((ENV/'data/bot_catalog.json').read_text());self.assertEqual(len(catalog),len({n['name'] for n in catalog}))
  self.assertEqual(list(range(len(catalog))),[n['index'] for n in catalog])
  old=json.loads((ROOT/'ocean/abyss/data/recorded/episodes.json').read_text())['episodes']
  names=sorted({n['name'] for e in old for r in e['rooms'] for n in r['entities'] if n['role']=='HostileNpc'})
  self.assertEqual(names,[n['name'] for n in catalog[:len(names)]])
 def test_special_damage_and_initial_state_are_not_silently_lost(self):
  rows={n['name']:n for n in json.loads((ENV/'data/combat_catalog.json').read_text())}
  meta={n['name']:n for n in json.loads((ENV/'data/bot_catalog.json').read_text())}
  self.assertEqual(427.5,rows['Attacker Marshal Disparu Troop']['missile_dps'])
  self.assertAlmostEqual(178.5,rows['Attacker Marshal Disparu Troop']['missile_explosion_velocity_mps'])
  self.assertEqual([0,0,4677],meta['Striking Leshak']['initial'])
  self.assertEqual([3356,431,10710],meta['Drifter Foothold Battleship']['initial'])
  self.assertGreater(meta['Skybreaker Disparu Troop']['vorton_radius'],0)
  self.assertEqual(3,meta['Striking Vila Damavik']['drone_active'])
  self.assertEqual(5,meta['Striking Vila Damavik']['drone_total'])
  self.assertEqual(8,rows['Vila Swarmer']['turret_dps'])
  self.assertEqual(2,meta['Anchoring Damavik']['effects'][0]['values'][7])
if __name__=='__main__':unittest.main()
