from .environment import env_creator

# try:
#     import torch
# except ImportError:
#     pass
# else:
#     from .torch import Policy
#     try:
#         from .torch import Recurrent
#     except:
#         Recurrent = None

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