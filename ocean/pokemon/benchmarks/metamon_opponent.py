"""Released Metamon inference on the native game's player-only projection."""
import copy
import hashlib
import os
from dataclasses import asdict
from pathlib import Path

os.environ.setdefault('TORCH_COMPILE_DISABLE', '1')

import gin
import amago
import numpy as np
import torch
import torch.nn.functional as F
from amago.nets.transformer import SelfAttention

from private_view import PublicMechanics, PrivateView, normalize
from metamon.interface import UniversalState, UniversalMove
from metamon.rl.pretrained import get_pretrained_model
from metamon.rl.metamon_to_amago import make_placeholder_experiment
from poke_env.environment import Move


@gin.configurable
class NativeWindowAttention(SelfAttention):
    """Exact causal FlashAttention semantics in ordinary torch.

    This changes the attention backend, not model parameters or the context
    window. `window=32` is FlashAttention's (32, 0); `window=None` is its
    default (-1, -1), full causal attention over the cache. Unlike
    VanillaAttention it keeps FlashAttention's BF16 Q/K/V rounding. No torch
    compilation or flash_attn package is required.
    """
    def __init__(self, causal=True, dropout=0., window=32):
        super().__init__(causal=causal, dropout=dropout)
        self.window=window

    def forward(self, qkv, key_cache=None, val_cache=None, cache_seqlens=None):
        # Upstream FlashAttention explicitly rounds Q/K/V to BF16, even
        # with FP32 weights. Preserve that rounding on the CPU backend.
        qkv=qkv.to(torch.bfloat16)
        q,k,v=qkv.unbind(dim=2)
        batch,length,_,_=q.shape
        if cache_seqlens is not None:
            if length != 1:
                raise ValueError('Cached inference requires one timestep')
            index=torch.arange(batch,device=q.device)
            key_cache[index,cache_seqlens]=k[:,0].to(key_cache.dtype)
            val_cache[index,cache_seqlens]=v[:,0].to(val_cache.dtype)
            end=cache_seqlens+1
            maximum=int(end.max())
            k=torch.nan_to_num(key_cache[:,:maximum]).to(q.dtype)
            v=torch.nan_to_num(val_cache[:,:maximum]).to(q.dtype)
            positions=torch.arange(maximum,device=q.device)[None,:]
            mask=positions<end[:,None]
            if self.window is not None:
                mask&=positions>=cache_seqlens[:,None]-self.window
            mask=mask[:,None,None,:]
        else:
            positions=torch.arange(length,device=q.device)
            delta=positions[:,None]-positions[None,:]
            mask=delta>=0
            if self.window is not None:
                mask&=delta<=self.window
            mask=mask[None,None,:,:]
        result=F.scaled_dot_product_attention(q.transpose(1,2),k.transpose(1,2),
                     v.transpose(1,2),attn_mask=mask,dropout_p=0.)
        return result.transpose(1,2)


# Qualified released checkpoints. `window` is each model's upstream
# FlashAttention window (None: upstream default full causal attention).
MODELS={
    'TaurosV0':dict(sha256='3f55b698bd19f189a465797c7bd1fe49c75884de701ab91ad156e6bac70b8ba1',window=32),
    'SyntheticRLV2':dict(sha256='fbbca4dd6a8f84936f438ee8f2f621be107df3612639b1c78ea7c88fd3d42c04',window=None),
    # Original paper models (epoch 40, full causal attention); weaker than SyntheticRLV2.
    'SmallRL':dict(sha256='f4258211777480643fbfe573ff906a2ef1a86b0bc2ad9cce117efdc797e40369',window=None),
    'MediumRL':dict(sha256='38d92771a49896787bb08a54133456dc548243132dc17ca4308fb061c1aaae54',window=None),
    'LargeRL':dict(sha256='04ed6d5179a0fbc1254384a8199cbf09eed56982c41adf0e22465bfe95044d59',window=None),
    # Metamon's stated current best (release notes: Superkazam finetuned on +7.8M self-play battles; ~82% GXE vs
    # humans in gen1ou). superkazam.gin: FlashAttention window (96, 0).
    'Kakuna':dict(sha256='7e7474d5b2ec6624afdeb29a7cf1dc5c84817d5aee8ec39c1d35a153b18df26c',window=96),
    # Gen 9-era replication of the Synthetic agents (release notes: Gens 1-4 comparable to SyntheticRLV1, "nowhere
    # close to SyntheticRLV2"). FlashAttention window (32, 0).
    'Abra':dict(sha256='8f473d8b5cfb344925b753e0477b327c81012c01d000aa98e31096e25479b895',window=32),
}


class MetamonMechanics(PublicMechanics):
    """Use the exact frozen upstream Move features expected by its tokenizer."""
    def __init__(self):
        super().__init__()
        self.cache={}

    def move(self, move_id, pp=None):
        if not move_id:
            return asdict(UniversalMove.blank_move())
        if move_id not in self.cache:
            name=normalize(self.moves[move_id]['name'])
            if name=='vicegrip':name='visegrip'
            self.cache[move_id]=asdict(UniversalMove.from_Move(Move(name,gen=1)))
        value=self.cache[move_id].copy()
        if pp is not None:value['current_pp']=pp
        return value


class MetamonOpponent:
    def __init__(self, name='TaurosV0', sample=True, threads=2, device='cpu'):
        torch.set_num_threads(threads)
        self.device=torch.device(device)
        if self.device.type=='cuda' and not torch.cuda.is_available():
            raise RuntimeError('CUDA evaluation requested but unavailable; refusing CPU fallback')
        self.maker=get_pretrained_model(name)
        self.checkpoint_path=self.maker.get_path_to_checkpoint(self.maker.default_checkpoint)
        self.checkpoint_sha256=hashlib.sha256(open(self.checkpoint_path,'rb').read()).hexdigest()
        if name not in MODELS or self.checkpoint_sha256 != MODELS[name]['sha256']:
            raise ValueError('Unqualified or changed Metamon checkpoint')
        # Follow upstream initialize_agent, but bind the CPU backend AFTER
        # parsing its gin files (those files overwrite gin_overrides).
        amago.cli_utils.use_config(self.maker.base_config | {'MetamonDiscrete.temperature':1.},
            [self.maker.model_gin_config_path,self.maker.train_gin_config_path],finalize=False)
        gin.bind_parameter('traj_encoders.TformerTrajEncoder.attention_type',NativeWindowAttention)
        gin.bind_parameter('NativeWindowAttention.window',MODELS[name]['window'])
        # AMAGO's float 1e10 default is rejected by Python 3.12 randint.
        # This only configures an inert placeholder env, never a rollout.
        gin.bind_parameter('Experiment.traj_save_len',10_000_000_000)
        self.experiment=make_placeholder_experiment(
            ckpt_base_dir=str(Path(self.checkpoint_path).parents[2]),
            run_name=self.maker.model_name,log=False,
            observation_space=self.maker.observation_space,action_space=self.maker.action_space)
        self.experiment.start()
        weights=torch.load(self.checkpoint_path,map_location='cpu',weights_only=True)
        self.maker._validate_checkpoint(weights,self.experiment.policy.state_dict())
        self.experiment.policy.load_state_dict(weights,strict=True)
        self.experiment.policy.on_checkpoint_loaded(is_resume=False)
        self.policy=self.experiment.policy.to(self.device).eval()
        self.sample=sample
        self.view=PrivateView(MetamonMechanics())
        self.action_count=self.maker.action_space.gym_space.n
        self.shadows=[]
        self.reset(0)

    def reset(self, seed):
        torch.manual_seed(seed % (2**63-1))
        self.view.reset()
        self.maker.observation_space.reset()
        self.hidden=self.policy.init_hidden_state(1,self.device)
        # Qualification only: other backends of the same weights, fed the
        # identical observation stream; their probabilities are logged.
        for shadow in self.shadows:shadow['hidden']=shadow['policy'].init_hidden_state(1,self.device)
        self.previous_state=None
        self.previous_action=None
        self.timestep=0

    @torch.inference_mode()
    def choose(self, observation):
        projected,mapping=self.view.project(observation)
        if observation[6]==0 and observation[490]:
            # Other player is replacing a fainted Pokemon. Showdown does not
            # ask this player to act, so do not advance its policy history.
            return 10
        state=UniversalState.from_dict(copy.deepcopy(projected))
        obs=self.maker.observation_space.state_to_obs(state)
        illegal=np.ones(self.action_count,dtype=bool)
        for action in mapping:illegal[action]=False
        obs['illegal_actions']=illegal
        obs={k:torch.as_tensor(v,device=self.device).unsqueeze(0).unsqueeze(0) for k,v in obs.items()}
        rl2=torch.zeros((1,1,self.action_count+1),dtype=torch.float32,device=self.device)
        if self.previous_state is not None:
            rl2[0,0,0]=self.maker.reward_function(self.previous_state,state)
            rl2[0,0,1+self.previous_action]=1
        probabilities,self.hidden=self._probabilities(self.policy,self.hidden,obs,rl2,illegal)
        for shadow in self.shadows:
            shadow_probabilities,shadow['hidden']=self._probabilities(shadow['policy'],shadow['hidden'],obs,rl2,illegal)
            shadow['log'].append((probabilities.cpu(),shadow_probabilities.cpu()))
        action=int(torch.multinomial(probabilities,1)) if self.sample else int(probabilities.argmax())
        if action not in mapping:
            raise RuntimeError(f'Metamon selected masked action {action}: {mapping}')
        self.previous_state=state
        self.previous_action=action
        self.timestep+=1
        return mapping[action]

    def _probabilities(self, policy, hidden, obs, rl2, illegal):
        embedding,hidden=policy.get_state_embedding(obs=obs,rl2s=rl2,
            time_idxs=torch.tensor([[[self.timestep]]],dtype=torch.long,device=self.device),
            hidden_state=hidden)
        distribution=policy.actor(embedding,straight_from_obs={
            k:obs[k] for k in policy.pass_obs_keys_to_actor})
        probabilities=distribution.probs[0,0,-1].clone()
        # Upstream MetamonDiscrete adds a small probability floor AFTER its
        # actor's logit mask. Reapply the public legal mask to that final
        # distribution, rather than silently replacing invalid sampled moves.
        probabilities[torch.as_tensor(illegal,device=self.device)]=0
        probabilities/=probabilities.sum()
        return probabilities,hidden
