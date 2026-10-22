import copy
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'benchmarks'))
from private_view import PrivateView, PublicMechanics, heuristic_action


def observation():
    o=bytearray(648)
    o[0]=2;o[4]=1;o[5]=1;o[6]=1;o[7]=1
    for i,species in enumerate([121,103,112,113,143,128]):
        b=16+i*32
        o[b]=species;o[b+1]=255;o[b+4]=100
        o[b+5:b+7]=bytes([9,12])
        o[b+8:b+12]=bytes([94,59,86,105])  # Psychic, Blizzard, TWave, Recover
        o[b+12:b+16]=bytes([16,8,32,32])
    o[208]=94;o[209]=255;o[212]=100;o[213:215]=bytes([7,3])
    o[400]=121;o[401:403]=bytes([9,12]);o[403:409]=bytes([6]*6)
    o[432]=94;o[433:435]=bytes([7,3]);o[435:441]=bytes([6]*6)
    o[480:484]=bytes([1]*4)
    for i in range(1,6):o[480+4+i]=1
    return o


class PrivateViewTests(unittest.TestCase):
    def test_no_unrevealed_species_or_moves(self):
        state,mapping=PrivateView().project(observation())
        self.assertEqual(state['opponent_active_pokemon']['moves'],[])
        self.assertEqual(state['opponents_remaining'],6)
        self.assertEqual(state['opponent_teampreview'],[])
        self.assertEqual(set(mapping.values()),{0,1,2,3,5,6,7,8,9})

    def test_alphabetical_moves_and_switches(self):
        state,m=PrivateView().project(observation())
        self.assertEqual([m[i] for i in range(4)],[1,0,3,2])
        self.assertEqual([p['name'] for p in state['available_switches']],
                         ['chansey','exeggutor','rhydon','snorlax','tauros'])
        self.assertEqual([m[i] for i in range(4,9)],[7,5,6,8,9])

    def test_disabled_moves_do_not_reindex(self):
        o=observation();o[480+1]=0
        state,m=PrivateView().project(o)
        self.assertEqual(len(state['player_active_pokemon']['moves']),4)
        self.assertNotIn(0,m)
        self.assertEqual(m[1],0)

    def test_trapping_does_not_hide_own_reserves(self):
        o=observation();o[484:490]=bytes(6)
        state,m=PrivateView().project(o)
        self.assertEqual(len(state['available_switches']),5)
        self.assertEqual(set(m),{0,1,2,3})

    def test_struggle_and_wait(self):
        o=observation();o[480:]=bytes(168);o[491]=1
        _,m=PrivateView().project(o)
        self.assertEqual(set(m.values()),{11})
        o[491]=0;o[490]=1;o[6]=0
        _,m=PrivateView().project(o)
        self.assertEqual(m,{0:10})

    def test_forced_switch_fainted_reserves(self):
        o=observation();o[6]=2;o[480:484]=bytes(4)
        o[16+32+3]=1;o[485]=0
        state,m=PrivateView().project(o)
        self.assertTrue(state['forced_switch'])
        self.assertEqual(len(state['available_switches']),4)
        self.assertEqual(set(m.values()),{6,7,8,9})

    def test_public_reveals_and_history_reset(self):
        o=observation();view=PrivateView()
        o[9]=94;o[11]=1;o[216]=94
        state,_=view.project(o)
        self.assertEqual(state['opponent_prev_move']['name'],'psychic')
        self.assertEqual(state['opponent_active_pokemon']['moves'][0]['current_pp'],15)
        o[9]=o[11]=0
        state,_=view.project(o)
        self.assertEqual(state['opponent_prev_move']['name'],'psychic')
        view.reset();o[216]=0
        state,_=view.project(o)
        self.assertEqual(state['opponent_prev_move']['name'],'nomove')

    def test_effects_and_special_boosts_are_public(self):
        o=observation();o[406]=8;o[417]=1
        state,_=PrivateView().project(o)
        p=state['player_active_pokemon']
        self.assertEqual(p['spa_boost'],2)
        self.assertEqual(p['spd_boost'],2)
        self.assertEqual(p['effect'],'substitute')

    def test_unreported_effects_stay_hidden(self):
        # Recharging, Thrashing, Charging, Binding, Invulnerable: never in
        # Metamon's vocabulary, so they must not displace a reported effect.
        for byte in (418,410,411,412,413):
            o=observation();o[byte]=1
            view=PrivateView()
            self.assertEqual(view.project(o)[0]['player_active_pokemon']['effect'],'noeffect')
            o[417]=1
            self.assertEqual(view.project(o)[0]['player_active_pokemon']['effect'],'substitute')

    def test_heuristic_uses_only_public_view(self):
        o=observation()
        self.assertEqual(heuristic_action(o),0)  # Psychic into Gengar
        o[480:484]=bytes(4)
        self.assertIn(heuristic_action(o),range(5,10))

    def test_reject_incomplete_or_nonbattle_input(self):
        for o in [bytes(648),bytes(384),b'']:
            with self.assertRaises(ValueError):PrivateView().project(o)


if __name__=='__main__':unittest.main()
