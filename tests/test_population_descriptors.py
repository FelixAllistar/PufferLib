import copy
import unittest
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from pufferlib.population import distance, select, validate


def record(name, x, scores):
    return {'id':name,'descriptors':{'mix':x,'timing':.5},
            'payoffs':{str(i):{'score':s,'games':100000} for i,s in enumerate(scores)}}


def bundle(*records):
    return {'version':1,'schema_id':'test-v1','panel_id':'fixed-panel',
            'schema':{'mix':{'kind':'distribution','size':2},'timing':{'kind':'scalar'}},
            'records':list(records)}


class PopulationTests(unittest.TestCase):
    def test_distance(self):
        a,b=record('a',[2,0],[.5]),record('b',[0,2],[.5])
        data=bundle(a,b)
        self.assertEqual(distance(a,a,data['schema']),0)
        self.assertAlmostEqual(distance(a,b,data['schema']),2**-.5)
        b['descriptors']['mix']=[9,0]
        self.assertEqual(distance(a,b,data['schema']),0)

    def test_redundant_recipe_not_diversity(self):
        data=bundle(record('old',[1,0],[.8]),record('new',[1,0],[.7]))
        data['records'][1]['personality_weights']=[99,33]
        self.assertEqual(len(select(data)['selected']),1)

    def test_novel_quality_gate(self):
        data=bundle(record('best',[1,0],[.8]),record('novel',[0,1],[.75]),record('weak',[.5,.5],[.1]))
        self.assertEqual({x['id'] for x in select(data)['selected']},{'best','novel'})

    def test_counter_preserved_despite_low_average_and_identical_descriptor(self):
        data=bundle(record('general',[1,0],[.9,.9,.1]),record('counter',[1,0],[.1,.1,.9]))
        chosen=select(data)['selected']
        self.assertEqual(chosen[1]['reason'],'matchup_coverage')
        self.assertEqual(len(select(data,competitive=False)['selected']),1)

    def test_uncertain_counter_not_certified(self):
        data=bundle(record('a',[1,0],[.9,.1]),record('b',[1,0],[.1,.9]))
        for r in data['records']:
            for p in r['payoffs'].values(): p['games']=2
        self.assertEqual(len(select(data)['selected']),1)

    def test_capacity_and_order(self):
        data=bundle(record('a',[1,0],[.8]),record('b',[0,1],[.79]))
        a=select(data,capacity=1)
        data['records'].reverse()
        self.assertEqual(a,select(data,capacity=1))

    def test_reject_bad_contract(self):
        original=bundle(record('a',[1,0],[.5]),record('b',[0,1],[.5]))
        for field,value in [('mix',[0,0]),('mix',[1]),('mix',[float('nan'),1]),('timing',float('inf')),('timing',-1)]:
            data=copy.deepcopy(original);data['records'][0]['descriptors'][field]=value
            with self.assertRaises(ValueError): validate(data)
        data=copy.deepcopy(original);data['records'][1]['payoffs']={}
        with self.assertRaises(ValueError): validate(data)
        data=copy.deepcopy(original);data['records'][1]['id']='a'
        with self.assertRaises(ValueError): validate(data)

    def test_environment_independent_scalar_schema(self):
        data={'version':1,'schema_id':'farm-v1','panel_id':'weather-panel',
              'schema':{'reinvestment':{'kind':'scalar'},'livestock_income':{'kind':'scalar'}},
              'records':[{'id':name,'descriptors':{'reinvestment':x,'livestock_income':x},
                          'payoffs':{'weather':{'score':.8,'games':1000}}}
                         for name,x in [('crop',0),('animals',1)]]}
        self.assertEqual(len(select(data,competitive=False)['selected']),2)

    def test_cli_panel_mismatch_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as tmp:
            a,b,out=[Path(tmp)/name for name in ('a.json','b.json','out.json')]
            a.write_text(json.dumps(bundle(record('a',[1,0],[.8]))))
            other=bundle(record('b',[0,1],[.7]));other['panel_id']='different'
            b.write_text(json.dumps(other))
            command=[sys.executable,'-m','pufferlib.population']
            run=subprocess.run(command+[str(a),str(b)],capture_output=True)
            self.assertNotEqual(run.returncode,0)
            self.assertIn(b'cannot mix evaluation panels',run.stderr)
            out.write_text('preserve this')
            run=subprocess.run(command+[str(a),'--output',str(out)],capture_output=True)
            self.assertNotEqual(run.returncode,0)
            self.assertEqual(out.read_text(),'preserve this')


if __name__ == '__main__': unittest.main()
