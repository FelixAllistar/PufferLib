let engine;
self.onmessage = async ({data}) => {
  const {id, command, players, experimental, seed, action, policy} = data;
  try {
    let chosen;
    if (command === 'init') {
      const name = experimental && players === 2 ? 'poker2pro' : `poker${players}`;
      const {default: create} = await import(`./${name}.mjs`);
      engine = await create();
      engine._web_init(seed);
    } else if (command === 'next') engine._web_next();
    else if (command === 'act') {
      if (!engine._web_act(action)) throw Error('That action is no longer available.');
    } else if (command === 'bot') {
      chosen = engine._web_bot(policy);
      if (chosen < -1 || !engine._web_act(chosen)) throw Error('The opponent returned an invalid action.');
    } else throw Error('Unknown table command.');
    const at = engine._web_state() >>> 2;
    const count = engine.HEAP32[at];
    const state = Array.from(engine.HEAP32.subarray(at, at + 24 + 9 * count));
    self.postMessage({id, state, action: chosen});
  } catch (error) {
    self.postMessage({id, error: error.message || String(error)});
  }
};
