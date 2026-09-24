import Base
import ./Game.bend as G

# A completed episode's state and outcome are absorbing.
law terminal_absorbs:
  for seed: U32
  for task: U32
  for target: U32
  for order: U32
  for checked: U32
  for focus: U32
  for text: U32
  for length: U32
  for tick: U32
  for result: U32
  for action: U32
  {G.step(G.S{seed, task, target, order, checked, focus, text, length, tick, 1, result}, action) == G.S{seed, task, target, order, checked, focus, text, length, tick, 1, result} : G.State}

# Observation commands cannot mutate episode state, regardless of caller fields.
law observe_preserves_state:
  for s: G.State
  for seed: U32
  for action: U32
  {G.dispatch(2, s, seed, action) == s : G.State}

# A reset depends on the new seed, never on the old episode or action.
law reset_discards_history:
  for s: G.State
  for seed: U32
  for action: U32
  {G.dispatch(1, s, seed, action) == G.reset(seed) : G.State}

# Success/failure must preserve every non-outcome field.
law success_preserves_state:
  for seed: U32
  for task: U32
  for target: U32
  for order: U32
  for checked: U32
  for focus: U32
  for text: U32
  for length: U32
  for tick: U32
  for done: U32
  for result: U32
  {G.terminal(G.S{seed, task, target, order, checked, focus, text, length, tick, done, result}, True{}) == G.S{seed, task, target, order, checked, focus, text, length, tick, 1, 1} : G.State}

law failure_preserves_state:
  for seed: U32
  for task: U32
  for target: U32
  for order: U32
  for checked: U32
  for focus: U32
  for text: U32
  for length: U32
  for tick: U32
  for done: U32
  for result: U32
  {G.terminal(G.S{seed, task, target, order, checked, focus, text, length, tick, done, result}, False{}) == G.S{seed, task, target, order, checked, focus, text, length, tick, 1, 2} : G.State}

# Editing without a focused input has no effect, even for arbitrary input data.
law unfocused_edit_is_identity:
  for seed: U32
  for task: U32
  for target: U32
  for order: U32
  for checked: U32
  for text: U32
  for length: U32
  for tick: U32
  for done: U32
  for result: U32
  for action: U32
  {G.edit(G.S{seed, task, target, order, checked, 0, text, length, tick, done, result}, action) == G.S{seed, task, target, order, checked, 0, text, length, tick, done, result} : G.State}
