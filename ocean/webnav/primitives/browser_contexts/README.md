# Browser contexts

`Model.bend` composes the browser primitive into independent tabs. Each tab has
its own route, back/forward stacks, pending load and failure schedule. Every
accepted command advances all existing tabs before dispatch, including tabs in
the background. Application records and page drafts belong to the host
application and are not copied into the shell.

The preset holds 1..16 tabs and 1..32 routes, including route zero for the blank
document. A host maps other route IDs to public URLs. New tab identities increase
monotonically, never reuse after close, and remain distinct from public node
references. Each tab retains up to eight history entries besides its current
route, using the existing browser primitive.

Commands include wait, navigation, back, forward, retry, reload, open, switch
and close. Open can retain the active tab or select the new one. Opening at a
nonblank route composes a new blank tab followed by navigation, so Back can
return to that blank document. This does not model a popup's direct initial
navigation. Closing the active tab selects the last surviving tab in creation
order; closing the last tab creates a new blank tab with a fresh identity.
Closing a background tab retains the active tab.

The active-close/replacement rule follows WebArena's browser action wrapper,
whose synchronous and asynchronous branches both select the last remaining
page or create a new one. The audited source is pinned separately from the
WebArena-Verified dataset/evaluator:
[actions.py at 60a6ca1](https://github.com/web-arena-x/webarena/blob/60a6ca1a5d0f1db3a74451925fcc2cd8591f9edf/browser_env/actions.py#L1188).
This is the wrapper's selected-page policy, not a claim about desktop Chrome's
automatic tab-strip focus policy. Background close/open are explicit extensions.

The private 2048-word row embeds the checked single-browser rows. C validates
bounds, identities and canonical transport, projects current public facts, and
packs commands. All transitions and seed-derived generation run in stock CPU
Bend. Malformed, missing-tab, capacity and identity-exhaustion inputs reject
without changing either state or clocks. Closing the last tab at identity
exhaustion rejects because it cannot allocate the replacement identity.

Nine laws and an independent C oracle pass 32,000 differential steps across 64
seeds. The oracle uses chronological visit logs and absolute event deadlines;
the model uses stacks and remaining durations. Checks also cover dirty reset,
closed identities, background loading, complete removal of closed slots,
finite capacities, atomic rejection and public independence from private timing.

`public.h` provides tab buttons, Back, Forward, Reload, New tab, Close tab,
Retry, loading/error status and the active address through the existing WF
action space. The pure renderer/binder takes only public tab facts, enabling
reuse by native applications and an original-browser host. A native session
advances its public reference interval after every accepted action; stale and
disabled controls reject atomically. Hosts supply public titles/URLs and keep
reference intervals distinct across composed components and episodes.

Controls require at most 24 nodes and 4 KiB of text, with a 32-reference stride.
Long titles and address displays clip at UTF-8 boundaries and mark the view
incomplete. Append failures preserve the input view and capability catalog.
These controls currently display the address; URL entry remains a separate
work item. Native public checks cover 32 generated worlds. The unchanged random
and sampled shared policies execute 512 legal actions across eight episodes;
these are interface checks without task rewards or success scores.

```sh
node ocean/webnav/families/build.cjs primitive:browser_contexts --test
bash ocean/webnav/benchmarks/run.sh test-browser-contexts
bash ocean/webnav/benchmarks/run.sh test-contexts-policy
```

Three Chromium fixtures pass 34 comparisons against independently navigated
local documents. They compare settled route/tab/history facts, including repeated
URLs, blank-page history and reload retaining Forward. They do not qualify
synthetic timing or pending navigation commit semantics. The test-only Python
bridge calls the native API; it is outside simulator, learner and production
browser loops. Binding the shared controls into the original-browser runner,
application/training composition, popups, frames, redirects, same-document
transitions and per-history page-state restoration remain separate gates. No
WebArena workflow or learned success is counted by these primitive tests.
