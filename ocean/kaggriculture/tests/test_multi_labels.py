import unittest
import numpy as np
from test_entity_labels import labels, native
import multi_bc_labels as multi


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
