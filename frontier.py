from pufferlib import pufferl
import numpy as np
import json

def run_seeds(env_name, n):
    all_logs = []
    for seed in range(n):
        pufferl.seed_everything()
        logs = pufferl.train(env_name)
        all_logs.append(logs)

    with open(f'{env_name}_seeds.npz', 'w') as f:
        json.dump(all_logs, f)

def ablate_hyperparam(env_name, hyperparam, min, max, space, n, seeds):
    if space == 'log':
        values = np.geomspace(min, max, n)
    elif space == 'linear':
        values = np.linspace(min, max, n)
    else:
        raise ValueError(f'Invalid space {space}')

    args = pufferl.load_config(env_name)

    all_logs = []
    for _ in range(seeds):
        for val in values:
            pufferl.seed_everything()
            args['train'][hyperparam] = val
            logs = pufferl.train(env_name, args)
            logs[-1][hyperparam] = val
            all_logs.append(logs)

    with open(f'{env_name}_{hyperparam}.npz', 'w') as f:
        json.dump(all_logs, f)

# Managing your own trainer
if __name__ == '__main__':
    #run_seeds('puffer_connect4', 5)
    #ablate_hyperparam('puffer_pong', 'learning_rate', 1e-2, 1e-0, 'log', 10, 3)
    #ablate_hyperparam('puffer_breakout', 'learning_rate', 1e-2, 1e-0, 'log', 10, 3)
    ablate_hyperparam('puffer_connect4', 'learning_rate', 1e-2, 1e-0, 'log', 10, 3)

