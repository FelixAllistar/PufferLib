import torch
import time

import pufferlib
import pufferlib.vector
import pufferlib.ocean
import pufferlib.models

'''
env_kwargs = {'num_envs': 4}
env_creator = pufferlib.ocean.env_creator('puffer_nmmo3')
vecenv = pufferlib.vector.make(env_creator, env_kwargs=env_kwargs, num_envs=1)
mlp = pufferlib.ocean.torch.NMMO3(vecenv)
policy = pufferlib.ocean.torch.NMMO3LSTM(vecenv, mlp).cuda()
'''

env_kwargs = {'num_envs': 4096}
env_creator = pufferlib.ocean.env_creator('puffer_pong')
vecenv = pufferlib.vector.make(env_creator, env_kwargs=env_kwargs, num_envs=1)
mlp = pufferlib.models.Default(vecenv)
policy = pufferlib.models.LSTMWrapper(vecenv, mlp).cuda()

obs_shape = vecenv.observation_space.shape
atn_shape = vecenv.action_space.shape
obs_dtype = pufferlib.pytorch.numpy_to_torch_dtype_dict[vecenv.single_observation_space.dtype]
atn_dtype = pufferlib.pytorch.numpy_to_torch_dtype_dict[vecenv.single_action_space.dtype]

obs = torch.zeros(*obs_shape, dtype=obs_dtype, device='cuda')
forward_graph = torch.cuda.CUDAGraph()
batch_agents = obs_shape[0]
state = pufferlib.namespace(
    lstm_h=torch.zeros(batch_agents, policy.hidden_size, dtype=torch.float32, device='cuda'),
    lstm_c=torch.zeros(batch_agents, policy.hidden_size, dtype=torch.float32, device='cuda'),
    reward=torch.zeros(batch_agents, 1, dtype=torch.float32, device='cuda'),
    done=torch.zeros(batch_agents, 1, dtype=torch.float32, device='cuda'),
    env_id=torch.zeros(batch_agents, 1, dtype=torch.long, device='cuda'),
    mask=torch.zeros(batch_agents, 1, dtype=torch.float32, device='cuda'),
)

N = 1000
s = torch.cuda.Stream()
s.wait_stream(torch.cuda.current_stream())
with torch.cuda.stream(s):
    with torch.no_grad():
        for i in range(3):
            l, v = policy.forward(obs, state)
torch.cuda.current_stream().wait_stream(s)

torch.cuda.synchronize()
start = time.time()
with torch.no_grad():
    for i in range(N):
        l, v = policy.forward(obs, state)

torch.cuda.synchronize()
end = time.time()
print(f'Eager SPS {N / (end - start)}')

with torch.cuda.graph(forward_graph):
    l, v = policy.forward(obs, state)

torch.cuda.synchronize()

start = time.time()
with torch.no_grad():
    for i in range(N):
        forward_graph.replay()

torch.cuda.synchronize()
end = time.time()
print(f'Graph SPS {N / (end - start)}')

#import logging
#torch._logging.set_logs(graph_breaks=True)

policy.forward = torch.compile(policy.forward)
with torch.no_grad():
    for i in range(10):
        l, v = policy.forward(obs, state)

torch.cuda.synchronize()
start = time.time()
with torch.no_grad():
    for i in range(N):
        l, v = policy.forward(obs, state)

torch.cuda.synchronize()
end = time.time()
print(f'Compiled SPS {N / (end - start)}')

print('Done')



