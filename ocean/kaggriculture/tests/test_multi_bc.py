import ctypes as C
import os
import pathlib
import sys
import unittest
import gzip
import json
import numpy as np

HERE = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(HERE))
import replay_native as native
import multi_bc_labels as multi
import build_entity_bc_dataset as dataset
import entity_bc_labels as labels


class ProjectionTests(unittest.TestCase):
    def project(self, action, positions, conflict=None, facts=None):
        target = native.c_action(action)
        mask = np.ones(labels.MASK,np.uint8)
        def teacher_mask(heads,out):
            out[:] = 1
            if conflict is not None:
                out[multi.OFFSETS[conflict]+int(heads[conflict])] = 0
        return multi.project(target,mask,lambda _: target,positions,teacher_mask,facts=facts)

    def test_concurrent_production_and_ordered_market(self):
        result, history, report = self.project({"farmer":["PLANT","WHEAT"],
            "hands":[["PLANT","MELON"],["BUILD_PASTURE"]],
            "market":[["SELL","WHEAT",14],["HIRE"],["BUY_ANIMAL","COW",3]]},
            [(0,0),(5,0),(0,5)])
        np.testing.assert_array_equal(result[:9],[1,0,1,5,0,2,7,0,3])
        np.testing.assert_array_equal(result[17:26],[1,10,13,1,19,-1,1,8,2])
        self.assertEqual(result[26],0)
        self.assertTrue(report['market_queue_exact_label'])

    def test_movement_does_not_mean_no_production(self):
        result,_,_ = self.project({"farmer":["EAST"],"market":[["HIRE"]]},[(4,4)])
        self.assertTrue(np.all(result[:15] == -1))
        self.assertEqual(result[17],1) # market-only row is useful BC

    def test_unrepresentable_quantity_never_becomes_stop(self):
        result,_,report = self.project({"market":[["BUY_SEED","WHEAT",101],["HIRE"]]},[(4,4)])
        self.assertTrue(np.all(result[17:] == -1))
        self.assertFalse(report['market_queue_exact_label'])

    def test_invalid_prefix_suppresses_dependent_suffix(self):
        result,_,report = self.project({"market":[["HIRE"],["HIRE"]]},[(4,4)],conflict=18)
        self.assertTrue(np.all(result[17:] == -1))
        self.assertEqual(report['market_mask_conflict'],1)

    def test_production_groups_never_silently_truncated(self):
        result,_,report = self.project({"farmer":["PLANT","WHEAT"],
            "hands":[["PLANT","WHEAT"],["PLANT","MELON"],["PLANT","MELON"],
                     ["BUILD_COOP"],["BUILD_PASTURE"]]},[(0,0),(5,0),(0,0),(5,0),(0,0),(0,0)])
        self.assertTrue(np.all(result[:15] == -1))
        self.assertEqual(report['production_overflow'],1)

    def test_effective_harvest_delivery_and_duplicate_filter(self):
        result,_,report = self.project({"farmer":["HARVEST"],
            "hands":[["PLACE","MILK",3],["FERTILIZE"],["DROP"]]},
            [(0,0),(5,4),(0,0),(4,4)],
            facts=np.array([[0,1,0,1],[-1,0,3,1],[0,0,0,1],[-1,0,7,1]]))
        np.testing.assert_array_equal(result[:9],[11,0,1,22,0,2,28,0,1])
        self.assertEqual(report['ineffective_production_omitted'],1)
        self.assertEqual(report['delivery_labeled'],2)
        self.assertEqual(report['crop_harvest_labeled'],1)

    def test_overflow_compaction_requires_native_effect_agreement(self):
        import collections
        groups = collections.Counter({(1,1):1,(2,1):1,(6,1):1,(7,1):1,(9,1):1,(11,1):1})
        action = native.c_action({'farmer':['PLANT','WHEAT'],
            'hands':[['PLANT','CARROT'],['BUILD_COOP'],['BUILD_PASTURE'],['FERTILIZE'],['HARVEST']]})
        positions = [(0,0)]*6
        facts = np.array([[-1,1,0,1]]*5+[[0,1,0,1]])
        result,_ = multi.compact_groups(groups,lambda _:action,lambda _:facts,positions,10)
        self.assertEqual(len(result),5)
        self.assertNotIn((11,1),result) # automatic ripe harvest was proved equivalent
        result,_ = multi.compact_groups(groups,lambda _:native.c_action({}),lambda _:facts,positions,10)
        self.assertEqual(result,groups) # never silently omit unrepresented work

    def test_wheat_reservation_is_supervised_for_valid_partial_queue(self):
        for suffix in ([],[['BUY_SEED','WHEAT',101]]):
            with self.subTest(suffix=suffix):
                action = native.c_action({'farmer':['EAST'],'market':[['SELL','WHEAT',4],*suffix]})
                mask = np.ones(labels.MASK,np.uint8)
                def teacher_mask(heads,out):
                    out[:] = 1
                    if heads[15] != 2: out[multi.OFFSETS[19]+3] = 0
                result,history,_ = multi.project(action,mask,lambda _:action,[(4,4)],teacher_mask)
                self.assertEqual(result[15],2)
                self.assertEqual(result[19],3)
                self.assertEqual(history[15],2)


@unittest.skipUnless((HERE/'build/multi_intent/libbc_replay.so').exists(), 'build multi bridge first')
class NativeMultiTests(unittest.TestCase):
    def test_new_profile_allows_immediate_repeat_land_purchase(self):
        lib = dataset.load_bridge(HERE/'build/multi_intent/libbc_replay.so')
        cfg = native.CConfig(); lib.kg_config_default(C.byref(cfg))
        ctx = lib.kag_bc_create(C.byref(cfg),str(HERE/'bc_2_2_pilot.ini').encode())
        try:
            heads = np.zeros(labels.HEADS,np.float32)
            heads[17:23] = [1,20,0,1,20,0]
            mask = np.empty(labels.MASK,np.uint8)
            lib.kag_bc_teacher_mask(ctx,0,heads.ctypes.data,mask.ctypes.data)
            self.assertTrue(mask[multi.OFFSETS[18]+20] and mask[multi.OFFSETS[21]+20])
            action = native.CAction(); lib.kag_bc_decode(ctx,0,heads.ctypes.data,C.byref(action))
            self.assertEqual(action.market_count,2)
            pair = (native.CAction*2)(action,native.c_action({}))
            changed = np.empty(native.KG_MAX_HANDS+1,np.int32)
            fills = np.empty(native.KG_MAX_MARKET_ORDERS,np.int32)
            lib.kag_bc_effects(ctx,0,pair,changed.ctypes.data,fills.ctypes.data,changed.size)
            np.testing.assert_array_equal(fills[:2],[1,1])
        finally:
            lib.kag_bc_destroy(ctx)

    def test_work_facts_and_simultaneous_fills_do_not_mutate_replay(self):
        lib = dataset.load_bridge(HERE/'build/multi_intent/libbc_replay.so')
        cfg = native.CConfig(); lib.kg_config_default(C.byref(cfg))
        ctx = lib.kag_bc_create(C.byref(cfg),str(HERE/'bc_2_2_pilot.ini').encode())
        try:
            before = native.c_snapshot(lib,lib.kag_bc_state(ctx))
            pair = (native.CAction*2)(native.c_action({'market':[['BUY_SEED','WHEAT',1]]}),native.c_action({}))
            facts = np.empty((native.KG_MAX_HANDS+1,4),np.int32)
            changed = np.empty(native.KG_MAX_HANDS+1,np.int32)
            fills = np.empty(native.KG_MAX_MARKET_ORDERS,np.int32)
            n = lib.kag_bc_work_facts(ctx,0,C.byref(pair[0]),facts.ctypes.data,facts.size)
            self.assertGreaterEqual(n,1)
            self.assertTrue(np.all(facts[:n,1:] == 0))
            self.assertEqual(lib.kag_bc_effects(ctx,0,pair,changed.ctypes.data,fills.ctypes.data,changed.size),1)
            self.assertEqual(fills[0],1)
            self.assertEqual(native.c_snapshot(lib,lib.kag_bc_state(ctx)),before)
            heads = np.zeros(labels.HEADS,np.float32); rewards = np.empty(2,np.float32)
            lib.kag_bc_step(ctx,pair,0,heads.ctypes.data,rewards.ctypes.data)
            after = native.c_snapshot(lib,lib.kag_bc_state(ctx))
            self.assertEqual(after['privates'][0]['seeds']['WHEAT']-before['privates'][0]['seeds']['WHEAT'],fills[0])
        finally:
            lib.kag_bc_destroy(ctx)

    def test_masked_hire_and_preview_purity(self):
        lib = dataset.load_bridge(HERE/'build/multi_intent/libbc_replay.so')
        cfg = native.CConfig(); lib.kg_config_default(C.byref(cfg))
        ctx = lib.kag_bc_create(C.byref(cfg),str(HERE/'bc_2_2_pilot.ini').encode())
        try:
            obs = np.empty(labels.OBS,np.float32); mask = np.empty(labels.MASK,np.uint8)
            lib.kag_bc_view(ctx,0,obs.ctypes.data,mask.ctypes.data)
            before = native.c_snapshot(lib,lib.kag_bc_state(ctx))
            def decode(heads):
                action = native.CAction(); lib.kag_bc_decode(ctx,0,heads.ctypes.data,C.byref(action)); return action
            def teacher_mask(heads,out):
                lib.kag_bc_teacher_mask(ctx,0,heads.ctypes.data,out.ctypes.data)
            action = native.c_action({"market":[["HIRE"],["HIRE"]]})
            result,history,report = multi.project(action,mask,decode,[(4,4)],teacher_mask)
            self.assertEqual(result[18],19)
            self.assertEqual(decode(history).market[0].op,4)
            self.assertEqual(native.c_snapshot(lib,lib.kag_bc_state(ctx)),before)
            for h,label in enumerate(result):
                if label >= 0: self.assertTrue(mask[multi.OFFSETS[h]+int(label)])
        finally:
            lib.kag_bc_destroy(ctx)


@unittest.skipUnless(os.environ.get('KAG_BC_BASELINE_LIB') and os.environ.get('KAG_BC_BASELINE_TAPE'),
                     'set baseline bridge/tape for cross-build 2/1 regression')
class FrozenBaselineTests(unittest.TestCase):
    def test_old_2_1_actions_observations_and_rewards_unchanged(self):
        with gzip.open(os.environ['KAG_BC_BASELINE_TAPE'],'rt') as stream:
            tape = json.load(stream)
        cfg = native.replay_config(tape)
        old = native.load_core(pathlib.Path(os.environ['KAG_BC_BASELINE_LIB']))
        # Original policy-v3 library deliberately has no current ABI query.
        old.kag_bc_create.argtypes = [C.POINTER(native.CConfig),C.c_char_p]
        old.kag_bc_create.restype = C.c_void_p
        old.kag_bc_destroy.argtypes = [C.c_void_p]
        old.kag_bc_view.argtypes = [C.c_void_p,C.c_int,C.c_void_p,C.c_void_p]
        old.kag_bc_decode.argtypes = [C.c_void_p,C.c_int,C.c_void_p,C.POINTER(native.CAction)]
        old.kag_bc_step.argtypes = [C.c_void_p,C.POINTER(native.CAction),C.c_int,C.c_void_p,C.c_void_p]
        new = dataset.load_bridge(HERE/'build/multi_intent/libbc_replay.so')
        libs = [old,new]
        contexts = [lib.kag_bc_create(C.byref(cfg),str(HERE/'bc_2_1_pilot.ini').encode()) for lib in libs]
        obs = [np.empty(labels.OBS,np.float32) for _ in libs]
        masks = [np.empty(size,np.uint8) for size in (1058,labels.MASK)]
        rewards = [np.empty(2,np.float32) for _ in libs]
        try:
            for step, raw in enumerate(tape['actions']):
                for lib,ctx,o,m in zip(libs,contexts,obs,masks):
                    lib.kag_bc_view(ctx,0,o.ctypes.data,m.ctypes.data)
                np.testing.assert_array_equal(obs[0],obs[1])
                np.testing.assert_array_equal(masks[0][:748],masks[1][:748])
                heads = np.zeros(labels.HEADS,np.float32)
                heads[:3] = (step%37,step%8,step%5)
                previews = [native.CAction(),native.CAction()]
                for lib,ctx,a in zip(libs,contexts,previews):
                    lib.kag_bc_decode(ctx,0,heads.ctypes.data,C.byref(a))
                self.assertTrue(labels.primitive_equal(*previews), f'step {step}')
                pair = (native.CAction*2)(*(native.c_action(a) for a in raw))
                for lib,ctx,r in zip(libs,contexts,rewards):
                    lib.kag_bc_step(ctx,pair,0,heads.ctypes.data,r.ctypes.data)
                np.testing.assert_array_equal(*rewards)
        finally:
            for lib,ctx in zip(libs,contexts): lib.kag_bc_destroy(ctx)

if __name__ == '__main__':
    unittest.main()
