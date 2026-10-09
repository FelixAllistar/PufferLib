import Base
import ../browser/Model.bend as B

# Context identity is distinct from route identity and observation refs.
# Application records live outside this shell; each tab owns its browser state.
type Tab is Data:
  T{identity: U32, browser: B.State}

type State is Data:
  S{tabs: +List<Tab>, active: U32, next_identity: U32, capacity: U32,
    route_count: U32, seed: U32}

type Action is Data:
  Wait{}
  Switch{identity: U32}
  Open{route: U32, foreground: Bool}
  Close{identity: U32}
  Browser{action: B.Action}

def identity(tab: Tab) -> U32:
  T{id, browser} = tab
  id

def contains(tabs: +List<Tab>, +id: U32) -> Bool:
  match tabs:
    case Nil{}: False{}
    case Con{tab, rest}: U32.is_eq(identity(tab), id) || contains(rest, id)

def switch_if(found: Bool, s: State, id: U32) -> State:
  match found s:
    case False{} _: s
    case True{} S{tabs, active, next, capacity, count, seed}:
      S{tabs, id, next, capacity, count, seed}

def switch(+s: State, +id: U32) -> State:
  S{tabs, active, next, capacity, count, seed} = s
  switch_if(contains(tabs, id), s, id)

# Route zero is the shell's blank document. Other routes belong to the host.
# The timing/failure streams depend on seed and tab identity, never task goals.
def fresh(+seed: U32, +id: U32, count: U32, +route: U32) -> Tab:
  +salt = (seed .^. (id * 2654435769 : U32) : U32)
  +latency = (25 + (salt .^. 2246822507 : U32) % 7 * 25 : U32)
  failures = ((salt .^. 3266489909 : U32) % 3 : U32)
  +blank = {B.S{0, count, latency, failures, Nil{}, Nil{}, B.Ready{}} : B.State}
  # Opening at a URL composes new blank tab + navigation, retaining that blank.
  # Popup/window.open(url) initial history is a separate host behavior.
  T{id, Bool.pick(B.State, U32.is_eq(route, 0), blank, B.navigate(blank, route))}

def initial(+seed: U32, +count: U32, capacity: U32) -> State:
  S{[fresh(seed, 1, count, 0)], 1, 2, capacity, count, seed}

def open_if(valid: Bool, s: State, route: U32, foreground: Bool) -> State:
  match valid s:
    case False{} _: s
    case True{} S{tabs, active, +next, capacity, +count, +seed}:
      S{List.append(&2, Tab, tabs, [fresh(seed, next, count, route)]),
        Bool.pick(U32, foreground, next, active), (next + 1 : U32), capacity, count, seed}

def open(+s: State, +route: U32, foreground: Bool) -> State:
  S{tabs, active, next, capacity, count, seed} = s
  open_if(U32.is_lt(route, count) && U32.is_lt(next, 4294967295) &&
    Nat.is_lt(List.length(&2, Tab, tabs), U32.to_nat(capacity)), s, route, foreground)

def remove(tabs: +List<Tab>, +id: U32) -> +List<Tab>:
  match tabs:
    case Nil{}: Nil{}
    case Con{+tab, rest}:
      List.append(&2, Tab, Bool.pick(+List<Tab>, U32.is_eq(identity(tab), id), Nil{}, [tab]),
        remove(rest, id))

def last_identity(tabs: +List<Tab>, previous: U32) -> U32:
  match tabs:
    case Nil{}: previous
    case Con{tab, rest}: last_identity(rest, identity(tab))

# Closing the active tab selects the last surviving tab in creation order.
# Closing the final tab creates a fresh blank document and a new identity.
# Background close is an extension and retains the current active identity.
def after_close(tabs: +List<Tab>, +active: U32, removed: U32, +next: U32,
    capacity: U32, +count: U32, +seed: U32) -> State:
  match tabs:
    case Nil{}:
      S{[fresh(seed, next, count, 0)], next, (next + 1 : U32), capacity, count, seed}
    case Con{+tab, +rest}:
      +kept = {tab <> rest : +List<Tab>}
      S{kept, Bool.pick(U32, U32.is_eq(active, removed), last_identity(kept, 0), active),
        next, capacity, count, seed}

def close_if(valid: Bool, s: State, +id: U32) -> State:
  match valid s:
    case False{} _: s
    case True{} S{tabs, active, next, capacity, count, seed}:
      after_close(remove(tabs, id), active, id, next, capacity, count, seed)

def close(+s: State, +id: U32) -> State:
  S{+tabs, active, next, capacity, count, seed} = s
  close_if(contains(tabs, id) &&
    (Nat.is_gt(List.length(&2, Tab, tabs), 1n) || U32.is_lt(next, 4294967295)), s, id)

def act_tab(selected: Bool, tab: Tab, action: B.Action) -> Tab:
  match selected tab:
    case False{} _: tab
    case True{} T{id, browser}: T{id, B.act(action, browser)}

def act_tabs(tabs: +List<Tab>, +active: U32, +action: B.Action) -> +List<Tab>:
  match tabs:
    case Nil{}: Nil{}
    case Con{+tab, rest}:
      act_tab(U32.is_eq(identity(tab), active), tab, action) <> act_tabs(rest, active, action)

def browser_action(s: State, action: B.Action) -> State:
  S{tabs, +active, next, capacity, count, seed} = s
  S{act_tabs(tabs, active, action), active, next, capacity, count, seed}

def advance_tabs(tabs: +List<Tab>, +elapsed: U32) -> +List<Tab>:
  match tabs:
    case Nil{}: Nil{}
    case Con{T{id, browser}, rest}:
      T{id, B.advance(browser, elapsed)} <> advance_tabs(rest, elapsed)

def advance(s: State, elapsed: U32) -> State:
  S{tabs, active, next, capacity, count, seed} = s
  S{advance_tabs(tabs, elapsed), active, next, capacity, count, seed}

def act(action: Action, s: State) -> State:
  match action:
    case Wait{}: s
    case Switch{id}: switch(s, id)
    case Open{route, foreground}: open(s, route, foreground)
    case Close{id}: close(s, id)
    case Browser{action_}: browser_action(s, action_)

# Every existing tab advances before a new command, including background loads.
def step(s: State, action: Action, elapsed: U32) -> State:
  act(action, advance(s, elapsed))
