from .environment import env_creator

try:
    import pufferlib.environments.morph._torch as torch
except ImportError:
    pass
else:
    from pufferlib.environments.morph._torch import Policy
    try:
        from pufferlib.environments.morph._torch import Recurrent
    except:
        Recurrent = None

'''
try:
    import pufferlib.environments.morph.policy as torch
except ImportError:
    pass
else:
    from .policy import Policy
    try:
        from .policy import Recurrent
    except:
        Recurrent = None
'''
