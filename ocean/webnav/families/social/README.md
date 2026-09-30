# Social media family source audit

Pinned source: MiniWoB-plusplus revision
`33c3b4ddef8c6eb67c57a29663d844b1eda7e614`, pages
`social-media.html`, `social-media-all.html`, and `social-media-some.html` under
`miniwob/html/miniwob/`. These pages use `core/core.js` for random sampling,
episode timing, and time proportional successful rewards, and
`common/ui_utils.js` for names and generated post text. `core.randi(min,max)`
has an exclusive upper bound.

| Task | Posts | Deadline | Expected action | Click and reward rule |
| --- | --- | --- | --- | --- |
| `social-media` | 5..9 | 15 seconds | One of Reply, Retweet, Like, Share via DM, Copy link to Tweet, Embed Tweet, Mute, Block, Report | A direct control or menu item ends the episode. Reward is +1 only when the clicked post's username equals the requested username and its control class equals the requested action class. Other clicks give -1. The More control opens its own menu, closes another menu, or closes itself; it does not score. |
| `social-media-all` | 6..11 | 20 seconds | Reply, Retweet, Like, or Share | Each post and control has its own active bit. Clicking toggles it. Submit gives +1 only when every requested-action control on every post with the requested username is active and every other control is inactive. |
| `social-media-some` | 6..11 | 20 seconds | Reply, Retweet, Like, or Share, plus a requested count | Each control toggles independently. Submit gives +1 only when exactly the requested number of matching username/action controls are active and every other control is inactive. Any subset of matching posts of that size succeeds. |

The two submit tasks generate contiguous runs that often repeat a username,
then shuffle the posts. Their scoring uses username equality after the shuffle,
so the model must preserve every post's identity and all four toggle bits. The
single-click task can also have duplicate usernames by chance; scoring still
uses username equality rather than the private selected post index. All three
tasks sample the target index from the generated posts and show the target
username and action in the public instruction. The `some` task samples an
amount from 1 through the number of posts with that username, inclusive.

`Model.bend`, `Generate.bend`, and `Wire.bend` contain all three local tasks.
The shared ABI v2 row has four 8192-word lanes, a 32-word header, task state at
32..36, eleven 256-word post slots at 64, and the public instruction at 3000.
Each post has name, username, body, time, and a private four-bit active mask.
The mask changes only in Bend, with one bit per post and control; the submit
scan compares every username and every mask in Bend. The C adapter only
validates and transports actions and projects public nodes. It leaves the
private target post, action code, and requested count out of `WFView`.

The Bend generator uses a bounded subset of the original names and body text.
It interleaves repeated username pairs for the submit tasks, then chooses the
target username from a generated post. This preserves duplicate-user and
per-post scoring behavior without claiming the original random distribution.
The original direct-click task may have accidental duplicate usernames;
browser-imported instances preserve them and the Bend scorer compares their
full username text. Browser-imported post text is rejected on overflow or
non-ASCII input rather than truncated.

`test_social.c` calculates expected outcomes independently for direct clicks,
all-post activation, exact-count activation, missing controls, wrong controls,
extra controls, and double-toggle reversal. `test_public.c` solves generated
instances using only `WFView` and checks that changing private goal fields
does not change public observations. `browser_oracle.c` imports each original
browser-generated instance once, then advances the browser and Bend model
separately through matching actions. It compares menu state, every post mask,
terminal state, raw reward, and time-adjusted reward after each action. Its
five trace schedules per task cover success, missing/wrong controls, wrong
user or extra activation, menu open/close, and timeout. Browser clicks use
the original DOM through CDP; the fixture controls the logical clock.

Root qualified all three tasks on 2026-09-29: nine Bend laws, 512 independent
direct and 2,560 multi-post transitions, 768 generated public-only native
episodes, 60 original-page episodes with 127 compared actions, and 3,000/3,000
public scripted successes. The browser fixtures preload the original normal
and hover icons and reject failed loads. Exact commands and source hashes are
in `RESULTS.json`. Original page geometry, scrolling visibility,
full random generation, and real scheduler timing remain outside the current
projection and browser comparison. Source browser comparisons and a scripted
public controller are not learned PPO results or full parity.
