from pdb import set_trace as T
import numpy as np

import torch
import torch.nn as nn

import pufferlib.emulation
import pufferlib.pytorch
import pufferlib.spaces

class Dynamics(nn.Module):
    def __init__(self, num_atns, hidden_size):
        super().__init__()
        self.action_embed = nn.Embedding(num_atns, hidden_size)
        self.net = nn.Sequential(
            nn.Linear(2*hidden_size, hidden_size),
            nn.GELU(),
        )
        self.state_decoder = nn.Linear(hidden_size, hidden_size)
        self.reward_decoder = nn.Linear(hidden_size, 1)

    def forward(self, state, action):
        action_embed = self.action_embed(action)
        state = self.net(torch.cat([state, action_embed], dim=-1))
        state = self.state_decoder(state)
        reward = self.reward_decoder(state)
        return state, reward.squeeze(-1)

class Default(nn.Module):
    '''Default PyTorch policy. Flattens obs and applies a linear layer.

    PufferLib is not a framework. It does not enforce a base class.
    You can use any PyTorch policy that returns actions and values.
    We structure our forward methods as encode_observations and decode_actions
    to make it easier to wrap policies with LSTMs. You can do that and use
    our LSTM wrapper or implement your own. To port an existing policy
    for use with our LSTM wrapper, simply put everything from forward() before
    the recurrent cell into encode_observations and put everything after
    into decode_actions.
    '''
    def __init__(self, env, hidden_size=128):
        super().__init__()
        self.hidden_size = hidden_size
        self.is_multidiscrete = isinstance(env.single_action_space,
                pufferlib.spaces.MultiDiscrete)
        self.is_continuous = isinstance(env.single_action_space,
                pufferlib.spaces.Box)
        try:
            self.is_dict_obs = isinstance(env.env.observation_space, pufferlib.spaces.Dict) 
        except:
            self.is_dict_obs = isinstance(env.observation_space, pufferlib.spaces.Dict) 

        if self.is_dict_obs:
            self.dtype = pufferlib.pytorch.nativize_dtype(env.emulated)
            input_size = int(sum(np.prod(v.shape) for v in env.env.observation_space.values()))
            self.encoder = nn.Linear(input_size, self.hidden_size)
        else:
            num_obs = np.prod(env.single_observation_space.shape)
            self.encoder = torch.nn.Sequential(
                pufferlib.pytorch.layer_init(nn.Linear(num_obs, hidden_size)),
                nn.GELU(),
            )
            
        if self.is_multidiscrete:
            self.action_nvec = tuple(env.single_action_space.nvec)
            self.decoder = pufferlib.pytorch.layer_init(
                    nn.Linear(hidden_size, num_atns), std=0.01)
        elif not self.is_continuous:
            num_atns = env.single_action_space.n
            self.decoder = pufferlib.pytorch.layer_init(
                nn.Linear(hidden_size, num_atns), std=0.01)
        else:
            self.decoder_mean = pufferlib.pytorch.layer_init(
                nn.Linear(hidden_size, env.single_action_space.shape[0]), std=0.01)
            self.decoder_logstd = nn.Parameter(torch.zeros(
                1, env.single_action_space.shape[0]))

        self.value = pufferlib.pytorch.layer_init(
            nn.Linear(hidden_size, 1), std=1)

        self.G = Dynamics(num_atns, hidden_size)

    def forward_eval(self, observations, state=None):
        hidden = self.encode_observations(observations, state=state)
        logits, values = self.decode_actions(hidden)
        return logits, values

    def forward(self, observations, state=None):
        return self.forward_eval(observations, state)

    def encode_observations(self, observations, state=None):
        '''Encodes a batch of observations into hidden states. Assumes
        no time dimension (handled by LSTM wrappers).'''
        batch_size = observations.shape[0]
        if self.is_dict_obs:
            observations = pufferlib.pytorch.nativize_tensor(observations, self.dtype)
            observations = torch.cat([v.view(batch_size, -1) for v in observations.values()], dim=1)
        else: 
            observations = observations.view(batch_size, -1)
        return self.encoder(observations.float())

    def decode_actions(self, hidden):
        '''Decodes a batch of hidden states into (multi)discrete actions.
        Assumes no time dimension (handled by LSTM wrappers).'''
        if self.is_multidiscrete:
            logits = self.decoder(hidden).split(self.action_nvec, dim=1)
        elif self.is_continuous:
            mean = self.decoder_mean(hidden)
            logstd = self.decoder_logstd.expand_as(mean)
            std = torch.exp(logstd)
            logits = torch.distributions.Normal(mean, std)
        else:
            logits = self.decoder(hidden)

        values = self.value(hidden)
        return logits, values

class RSSM(nn.Module):
    def __init__(self, env, input_size=128, hidden_size=128):
        '''Wraps your policy with an LSTM without letting you shoot yourself in the
        foot with bad transpose and shape operations. This saves much pain.
        Requires that your policy define encode_observations and decode_actions.
        See the Default policy for an example.'''
        super().__init__()
        self.obs_shape = env.single_observation_space.shape

        self.input_size = input_size
        self.hidden_size = hidden_size

        self.atn_embed = nn.Embedding(env.single_action_space.n, hidden_size)
        self.cell_proj = torch.nn.Linear(2*hidden_size, hidden_size)
        self.cell = torch.nn.LSTMCell(input_size, hidden_size)

        for name, param in self.named_parameters():
            if 'layer_norm' in name:
                continue
            if "bias" in name:
                nn.init.constant_(param, 0)
            elif "weight" in name and param.ndim >= 2:
                nn.init.orthogonal_(param, 1.0)

        num_obs = np.prod(env.single_observation_space.shape)
        num_atns = env.single_action_space.n
 
        self.encoder = torch.nn.Sequential(
            pufferlib.pytorch.layer_init(nn.Linear(num_obs + hidden_size, hidden_size)),
            nn.GELU(),
            #pufferlib.pytorch.layer_init(nn.Linear(hidden_size, hidden_size)),
        )
        self.dynamics = torch.nn.Sequential(
            #pufferlib.pytorch.layer_init(nn.Linear(hidden_size, hidden_size)),
            #nn.GELU(),
            pufferlib.pytorch.layer_init(nn.Linear(hidden_size, hidden_size)),
        )
        self.reward = torch.nn.Sequential(
            #pufferlib.pytorch.layer_init(nn.Linear(hidden_size, hidden_size)),
            #nn.GELU(),
            pufferlib.pytorch.layer_init(nn.Linear(hidden_size, 1)),
        )
        self.terminal = torch.nn.Sequential(
            #pufferlib.pytorch.layer_init(nn.Linear(hidden_size, hidden_size)),
            #nn.GELU(),
            pufferlib.pytorch.layer_init(nn.Linear(hidden_size, 1)),
        )
        self.actor = torch.nn.Sequential(
            #pufferlib.pytorch.layer_init(nn.Linear(hidden_size, hidden_size)),
            #nn.GELU(),
            pufferlib.pytorch.layer_init(nn.Linear(hidden_size, num_atns)),
        )
        self.value = torch.nn.Sequential(
            #pufferlib.pytorch.layer_init(nn.Linear(hidden_size, hidden_size)),
            #nn.GELU(),
            pufferlib.pytorch.layer_init(nn.Linear(hidden_size, 1)),
        )

    def forward(self, observations, state=None):
        h = state['lstm_h']
        c = state['lstm_c']
        a = state['action']

        z = self.encode(observations, a)
        hidden, c = self.cell(z, (h, c))

        state['hidden'] = hidden
        state['lstm_h'] = hidden
        state['lstm_c'] = c
 
        logits = self.actor(hidden)
        value = self.value(hidden)
        return logits, value

    def imagine(self, z, state):
        h = state['lstm_h']
        c = state['lstm_c']
        a = state['action']

        hidden, c = self.cell(z, (h, c))

        state['hidden'] = hidden
        state['lstm_h'] = hidden
        state['lstm_c'] = c
 
        logits = self.actor(hidden)
        action, logprob, entropy = pufferlib.pytorch.sample_logits(logits)
        #state['action'] = action

        value = self.value(hidden)
        reward = self.reward(hidden)
        terminal = self.terminal(hidden)

        return action, logprob, entropy, value, reward, terminal

    def encode(self, obs, atn):
        atn = atn * 0
        atn = self.atn_embed(atn)
        obs_atn = torch.cat([obs, atn], dim=-1)
        z = self.encoder(obs_atn)
        return z

class Convolutional(nn.Module):
    def __init__(self, env, *args, framestack, flat_size,
            input_size=512, hidden_size=512, output_size=512,
            channels_last=False, downsample=1, **kwargs):
        '''The CleanRL default NatureCNN policy used for Atari.
        It's just a stack of three convolutions followed by a linear layer
        
        Takes framestack as a mandatory keyword argument. Suggested default is 1 frame
        with LSTM or 4 frames without.'''
        super().__init__()
        self.channels_last = channels_last
        self.downsample = downsample

        #TODO: Remove these from required params
        self.hidden_size = hidden_size
        self.is_continuous = False

        self.network= nn.Sequential(
            pufferlib.pytorch.layer_init(nn.Conv2d(framestack, 32, 8, stride=4)),
            nn.ReLU(),
            pufferlib.pytorch.layer_init(nn.Conv2d(32, 64, 4, stride=2)),
            nn.ReLU(),
            pufferlib.pytorch.layer_init(nn.Conv2d(64, 64, 3, stride=1)),
            nn.ReLU(),
            nn.Flatten(),
            pufferlib.pytorch.layer_init(nn.Linear(flat_size, hidden_size)),
            nn.ReLU(),
        )
        self.actor = pufferlib.pytorch.layer_init(
            nn.Linear(hidden_size, env.single_action_space.n), std=0.01)
        self.value_fn = pufferlib.pytorch.layer_init(
            nn.Linear(output_size, 1), std=1)

    def forward(self, observations, state=None):
        hidden = self.encode_observations(observations)
        actions, value = self.decode_actions(hidden)
        return actions, value

    def forward_train(self, observations, state=None):
        return self.forward(observations, state)

    def encode_observations(self, observations, state=None):
        if self.channels_last:
            observations = observations.permute(0, 3, 1, 2)
        if self.downsample > 1:
            observations = observations[:, :, ::self.downsample, ::self.downsample]
        return self.network(observations.float() / 255.0)

    def decode_actions(self, flat_hidden):
        action = self.actor(flat_hidden)
        value = self.value_fn(flat_hidden)
        return action, value

class ProcgenResnet(nn.Module):
    '''Procgen baseline from the AICrowd NeurIPS 2020 competition
    Based on the ResNet architecture that was used in the Impala paper.'''
    def __init__(self, env, cnn_width=16, mlp_width=256):
        super().__init__()
        h, w, c = env.single_observation_space.shape
        shape = (c, h, w)
        conv_seqs = []
        for out_channels in [cnn_width, 2*cnn_width, 2*cnn_width]:
            conv_seq = ConvSequence(shape, out_channels)
            shape = conv_seq.get_output_shape()
            conv_seqs.append(conv_seq)
        conv_seqs += [
            nn.Flatten(),
            nn.ReLU(),
            nn.Linear(in_features=shape[0] * shape[1] * shape[2], out_features=mlp_width),
            nn.ReLU(),
        ]
        self.network = nn.Sequential(*conv_seqs)
        self.actor = pufferlib.pytorch.layer_init(
                nn.Linear(mlp_width, env.single_action_space.n), std=0.01)
        self.value = pufferlib.pytorch.layer_init(
                nn.Linear(mlp_width, 1), std=1)

    def forward(self, observations, state=None):
        hidden = self.encode_observations(observations)
        actions, value = self.decode_actions(hidden)
        return actions, value

    def forward_train(self, observations, state=None):
        return self.forward(observations, state)

    def encode_observations(self, x):
        hidden = self.network(x.permute((0, 3, 1, 2)) / 255.0)
        return hidden
 
    def decode_actions(self, hidden):
        '''linear decoder function'''
        action = self.actor(hidden)
        value = self.value(hidden)
        return action, value

class ResidualBlock(nn.Module):
    def __init__(self, channels):
        super().__init__()
        self.conv0 = nn.Conv2d(in_channels=channels, out_channels=channels, kernel_size=3, padding=1)
        self.conv1 = nn.Conv2d(in_channels=channels, out_channels=channels, kernel_size=3, padding=1)

    def forward(self, x):
        inputs = x
        x = nn.functional.relu(x)
        x = self.conv0(x)
        x = nn.functional.relu(x)
        x = self.conv1(x)
        return x + inputs

class ConvSequence(nn.Module):
    def __init__(self, input_shape, out_channels):
        super().__init__()
        self._input_shape = input_shape
        self._out_channels = out_channels
        self.conv = nn.Conv2d(in_channels=self._input_shape[0], out_channels=self._out_channels, kernel_size=3, padding=1)
        self.res_block0 = ResidualBlock(self._out_channels)
        self.res_block1 = ResidualBlock(self._out_channels)

    def forward(self, x):
        x = self.conv(x)
        x = nn.functional.max_pool2d(x, kernel_size=3, stride=2, padding=1)
        x = self.res_block0(x)
        x = self.res_block1(x)
        assert x.shape[1:] == self.get_output_shape()
        return x

    def get_output_shape(self):
        _c, h, w = self._input_shape
        return (self._out_channels, (h + 1) // 2, (w + 1) // 2)
