# Land-purchase action mask

`env.land_buy_min_days = 2` is the default. Set it to **0** to completely
disable this constraint, including the requirement to fill a plot first.
Negative or fractional values are rejected.

For each player, the newest owned plot must first have a live crop or a placed
animal on every tile. Empty structures and weeds do not count. The first
observed full state starts a latched timer. BUY_LAND stays masked until
`land_buy_min_days * turns_per_day` turns have elapsed (48 turns by default),
not merely until the day counter crosses two midnights. Harvesting or losing
crops afterwards does not restart it. Every newly purchased plot starts fresh.

The mask applies to all market slots and the EXPAND macro. The native policy
action limiter also rejects gated purchases and permits at most one purchase
per turn while enabled, so repeated slots cannot bypass the next plot's fill
requirement. Ordinary affordability and game legality still apply. The game
core, rewards, observations, and network dimensions are unchanged.

Episode resets clear the timer. Saved states do not contain fill history:
an already-full plot starts its timer at the reset state, without guessing an
earlier completion time. A partially filled plot must become full during the
new rollout. Scripted replay opponents that already bypass policy limits keep
their original actions.

The setting is read when native train/eval environments are created. Changing
the INI does not modify an already-running process.

Test: `make -C ocean/kaggriculture land-delay-test CC=clang`.
