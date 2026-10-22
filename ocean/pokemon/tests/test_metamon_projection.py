"""Optional integration tests: run in the isolated Metamon evaluation env."""
import os
from pathlib import Path
import sys
import unittest
from dataclasses import asdict
from types import SimpleNamespace

os.environ.setdefault('TORCH_COMPILE_DISABLE','1')
os.environ.setdefault('METAMON_CACHE_DIR', str(Path(__file__).resolve().parents[3]/'build/pokemon/metamon-cache'))
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'benchmarks'))
try:
    import torch
    from metamon_opponent import NativeWindowAttention, MetamonMechanics
    from metamon.interface import UniversalState, UniversalPokemon, UniversalAction, GroupedObservationSpace, consistent_move_order
    from metamon.backend.showdown_dex import Dex
    from metamon.backend.replay_parser.replay_state import Pokemon, Move, Nothing
    from poke_env.environment import Pokemon as PokePokemon, Move as PokeMove
except ImportError as error:
    raise unittest.SkipTest(f'Install isolated Metamon evaluation dependencies: {error}')
from private_view import PrivateView, normalize
from test_private_view import observation


class MetamonProjectionTests(unittest.TestCase):
    def test_public_species_base_stats_match_frozen_metamon_dex(self):
        rules=MetamonMechanics();dex=Dex.from_gen(1)
        for species in rules.species.values():
            if not species['id']:continue
            entry=dex.get_pokedex_entry(species['name'])
            self.assertEqual(species['stats'],entry['baseStats'],species['name'])

    def test_projection_uses_upstream_schema_and_order(self):
        state,mapping=PrivateView(MetamonMechanics()).project(observation())
        state=UniversalState.from_dict(state)
        self.assertEqual(state.player_active_pokemon.name,'starmie')
        self.assertEqual([m.name for m in consistent_move_order(state.player_active_pokemon.moves)],
                         ['blizzard','psychic','recover','thunderwave'])
        obs=GroupedObservationSpace();obs.reset()
        features=obs.state_to_obs(state)
        self.assertEqual(len(features['numbers_active_pokemon']),31)
        self.assertEqual(len(features['text_active_pokemon'].item().split()),12)
        self.assertEqual(len(features['text_misc'].item().split()),20)
        self.assertEqual(mapping[0],1)

    def test_active_features_match_upstream_replay_conversion(self):
        rules=MetamonMechanics();o=observation()
        projected,_=PrivateView(rules).project(o)
        # Independent upstream conversion of the same public Starmie fixture.
        pokemon=Pokemon('Starmie',100,1)
        pokemon.current_hp=pokemon.max_hp=255
        pokemon.active_item=Nothing.NO_ITEM
        for index,move_id in enumerate(o[24:28]):
            name=rules.moves[move_id]['name']
            move=Move(name,1)
            move.pp=o[28+index]
            pokemon.moves[name]=move
        expected=asdict(UniversalPokemon.from_ReplayPokemon(pokemon))
        actual=projected['player_active_pokemon']
        self.assertEqual(actual,expected)

    def test_legal_action_mapping_matches_upstream_orders(self):
        rules=MetamonMechanics()
        for forced in (False, True):
            o=observation();o[481]=0  # Blizzard disabled; do not compact indices.
            if forced:o[6]=2;o[480:484]=bytes(4)
            state,mapping=PrivateView(rules).project(o)
            party=[]
            for index in range(6):
                base=16+32*index
                pokemon=PokePokemon(gen=1,species=normalize(rules.species[o[base]]['name']))
                pokemon._active=index==0
                pokemon._moves={normalize(rules.moves[m]['name']):PokeMove(normalize(rules.moves[m]['name']),gen=1)
                                for m in o[base+8:base+12]}
                party.append(pokemon)
            battle=SimpleNamespace(active_pokemon=party[0],team=dict(enumerate(party)),
                available_moves=[move for index,move in enumerate(party[0].moves.values()) if o[480+index]],
                available_switches=party[1:],force_switch=forced,reviving=False,can_tera=None)
            legal=UniversalAction.definitely_valid_actions(UniversalState.from_dict(state),battle)
            self.assertEqual({a.action_idx for a in legal},set(mapping))
            for action in legal:
                order=action.to_BattleOrder(battle).order
                native=mapping[action.action_idx]
                if native<4:
                    self.assertEqual(order.id,normalize(rules.moves[o[24+native]]['name']))
                else:
                    self.assertIs(order,party[native-4])

    def test_attention_cached_matches_full_sequence(self):
        torch.manual_seed(8);torch.set_num_threads(2)
        qkv=torch.randn(1,70,3,2,8)
        attention=NativeWindowAttention().eval()
        expected=attention(qkv)
        key=torch.full((1,80,2,8),float('nan'))
        value=key.clone()
        for index in range(70):
            actual=attention(qkv[:,index:index+1],key,value,torch.tensor([index],dtype=torch.int32))
            torch.testing.assert_close(actual,expected[:,index:index+1],rtol=.02,atol=.008)

    def test_attention_excludes_keys_outside_published_window(self):
        qkv=torch.zeros(1,70,3,1,4)
        qkv[:,0,2]=1000
        result=NativeWindowAttention().eval()(qkv)
        self.assertTrue(torch.all(result[:,33:]==0))
        self.assertTrue(torch.any(result[:,:33]>0))

    @unittest.skipUnless(torch.cuda.is_available(), 'CUDA evaluation environment required')
    def test_cuda_attention_cached_matches_full_sequence(self):
        torch.manual_seed(8)
        qkv=torch.randn(1,70,3,2,8,device='cuda')
        attention=NativeWindowAttention().eval()
        expected=attention(qkv)
        key=torch.full((1,80,2,8),float('nan'),device='cuda')
        value=key.clone()
        for index in range(70):
            actual=attention(qkv[:,index:index+1],key,value,
                             torch.tensor([index],dtype=torch.int32,device='cuda'))
            torch.testing.assert_close(actual,expected[:,index:index+1],rtol=.02,atol=.008)


if __name__=='__main__':unittest.main()
