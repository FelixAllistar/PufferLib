import Base
import ./Model.bend as M
import ./Generate.bend as G

type Check is Data:
  C{ok: Bool, selected: U32}

def user_at(+base: U32, +post: U32) -> U32:
  (base + 64 + (post - 1) * 256 + 64 : U32)

def compare_right(pair: Array<U32> & U32, x: U32, same: Bool) -> Array<U32> & Bool:
  (a, y) = pair
  (a, same && U32.is_eq(x, y))

def compare_left(pair: Array<U32> & U32, +right: U32, +i: U32,
    same: Bool) -> Array<U32> & Bool:
  (a, x) = pair
  compare_right(a[(right + i : U32)], x, same)

def same_words(n: Nat, +left: U32, +right: U32,
    pair: Array<U32> & Bool) -> Array<U32> & Bool:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, same) = pair
      +i = U32.from_nat(p)
      same_words(p, left, right, compare_left(a[(left + i : U32)], right, i, same))

def outcome_index(status: Nat, raw: U32) -> M.Outcome:
  match status:
    case 0n: M.Running{}
    case 1n: M.Finished{U32.is_eq(raw, 1065353216)}
    case _: M.TimedOut{}

def outcome(status: U32, raw: U32) -> M.Outcome:
  outcome_index(U32.to_nat(status), raw)

def save_outcome(o: M.Outcome, +elapsed: U32, +deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match o:
    case M.Running{}: a
    case M.Finished{+success}:
      a[(base + 9 : U32)] <- 1
      a[(base + 10 : U32)] <- F32.bits(M.raw(M.Finished{success}))
      a[(base + 11 : U32)] <- F32.bits(M.timed(M.Finished{success}, elapsed, deadline))
    case M.TimedOut{}:
      a[(base + 9 : U32)] <- 2
      a[(base + 10 : U32)] <- 3212836864
      a[(base + 11 : U32)] <- 3212836864

def save(s: M.State, elapsed: U32, deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  M.S{menu, expected, o} = s
  a = a[(base + 33 : U32)] <- menu
  a = a[(base + 2 : U32)] <- 0
  save_outcome(o, elapsed, deadline, base, a)

def action_for(slot: Nat, post: U32, +same_user: Bool) -> M.Action:
  match slot:
    case 0n: M.Press{0, same_user}
    case 1n: M.Press{1, same_user}
    case 2n: M.Press{2, same_user}
    case 3n: M.Menu{post}
    case 4n: M.Press{3, same_user}
    case 5n: M.Press{4, same_user}
    case 6n: M.Press{5, same_user}
    case 7n: M.Press{6, same_user}
    case 8n: M.Press{7, same_user}
    case _: M.Press{8, same_user}

def apply_compared(pair: Array<U32> & Bool, s: M.State, post: U32,
    slot: U32, +elapsed: U32, +deadline: U32, +base: U32) -> Array<U32>:
  (a, same_user) = pair
  save(M.step(s, action_for(U32.to_nat(slot), post, same_user), elapsed, deadline),
    elapsed, deadline, base, a)

def scan_acc(acc: Check, +mask: U32, +same_user: Bool,
    +task: U32, +expected: U32) -> Check:
  match acc:
    case C{ok, selected}:
      C{ok && M.mark_ok(task, mask, same_user, expected),
        (selected + M.marked(mask, same_user, expected) : U32)}

def scan_mask(pair: Array<U32> & U32, same_user: Bool,
    task: U32, expected: U32, acc: Check) -> Array<U32> & Check:
  (a, mask) = pair
  (a, scan_acc(acc, mask, same_user, task, expected))

def scan_same(pair: Array<U32> & Bool, +post: U32,
    task: U32, expected: U32, +base: U32, acc: Check) -> Array<U32> & Check:
  (a, same_user) = pair
  scan_mask(a[(base + 64 + (post - 1) * 256 + 252 : U32)],
    same_user, task, expected, acc)

def scan(n: Nat, +task: U32, +expected: U32,
    +goal_user: U32, +base: U32, pair: Array<U32> & Check) -> Array<U32> & Check:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, acc) = pair
      +post = (U32.from_nat(p) + 1 : U32)
      scan(p, task, expected, goal_user, base,
        scan_same(same_words(64n, goal_user, user_at(base, post),
          (a, True{})), post, task, expected, base, acc))

def finish_check(acc: Check, a: Array<U32>, task: U32, required: U32,
    s: M.State, +elapsed: U32, +deadline: U32, +base: U32) -> Array<U32>:
  match acc:
    case C{ok, selected}:
      save(M.step(s, M.Submit{M.accepted(task, ok, selected, required)},
        elapsed, deadline), elapsed, deadline, base, a)

def scan_result(pair: Array<U32> & Check, task: U32, required: U32,
    s: M.State, elapsed: U32, deadline: U32, +base: U32) -> Array<U32>:
  (a, acc) = pair
  finish_check(acc, a, task, required, s, elapsed, deadline, base)

def submit(+task: U32, +count_posts: U32, +goal_post: U32,
    +expected: U32, required: U32, s: M.State,
    elapsed: U32, deadline: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  scan_result(scan(U32.to_nat(count_posts), task, expected,
    user_at(base, goal_post), base, (a, C{True{}, 0})),
    task, required, s, elapsed, deadline, base)

def toggle_read(pair: Array<U32> & U32, slot: U32, +at: U32,
    s: M.State, +elapsed: U32, +deadline: U32,
    +base: U32) -> Array<U32>:
  (a, mask) = pair
  save(M.step(s, M.Wait{}, elapsed, deadline), elapsed, deadline, base,
    a[at] <- M.toggle(mask, slot))

def toggle_allowed(allowed: Bool, +post: U32, +slot: U32,
    s: M.State, +elapsed: U32, +deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match allowed:
    case True{}:
      +at = (base + 64 + (post - 1) * 256 + 252 : U32)
      toggle_read(a[at], slot, at, s, elapsed, deadline, base)
    case False{}:
      save(M.step(s, M.Wait{}, elapsed, deadline), elapsed, deadline, base, a)

def multi_submit(is_submit: Bool, +ref: U32, +task: U32,
    +count_posts: U32, +goal_post: U32, +expected: U32, required: U32,
    allowed: Bool, s: M.State, elapsed: U32, deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match is_submit:
    case True{}:
      submit(task, count_posts, goal_post, expected, required,
        s, elapsed, deadline, base, a)
    case False{}:
      toggle_allowed(allowed, (ref / 16 : U32), (ref % 16 : U32),
        s, elapsed, deadline, base, a)

def dispatch_task(single: Bool, +ref: U32, +task: U32,
    count_posts: U32, +goal_post: U32, +expected: U32,
    required: U32, allowed: Bool, s: M.State,
    +elapsed: U32, +deadline: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  match single:
    case True{}:
      +post = (ref / 16 : U32)
      +slot = (ref % 16 : U32)
      apply_compared(same_words(64n, user_at(base, post), user_at(base, goal_post),
        (a, True{})), s, post, slot, elapsed, deadline, base)
    case False{}:
      multi_submit(U32.is_eq(ref, 1), ref, task, count_posts,
        goal_post, expected, required, allowed, s, elapsed, deadline, base, a)

def dispatch_zero(zero: Bool, +ref: U32, +goal_post: U32, s: M.State,
    +task: U32, count_posts: U32, expected: U32, required: U32,
    allowed: Bool, +elapsed: U32, +deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match zero:
    case True{}: save(M.step(s, M.Wait{}, elapsed, deadline), elapsed, deadline, base, a)
    case False{}:
      dispatch_task(U32.is_eq(task, 0), ref, task, count_posts,
        goal_post, expected, required, allowed, s, elapsed, deadline, base, a)

def ready(pair: Array<U32> & U32, +status: U32, raw: U32,
    +elapsed: U32, +ref: U32, menu: U32, goal_post: U32,
    +deadline: U32, task: U32, count_posts: U32, required: U32,
    +base: U32) -> Array<U32>:
  (a, +expected) = pair
  s = {M.S{menu, expected, outcome(status, raw)} : M.State}
  dispatch_zero(U32.is_eq(ref, 0), ref, goal_post, s,
    task, count_posts, expected, required,
    U32.is_eq(status, 0) && U32.is_lt(elapsed, deadline),
    elapsed, deadline, base, a)

def load_goal(pair: Array<U32> & U32, status: U32, raw: U32,
    elapsed: U32, ref: U32, menu: U32, deadline: U32,
    task: U32, count_posts: U32, required: U32, +base: U32) -> Array<U32>:
  (a, goal_post) = pair
  ready(a[(base + 35 : U32)], status, raw, elapsed, ref, menu, goal_post,
    deadline, task, count_posts, required, base)

def load_menu(pair: Array<U32> & U32, status: U32, raw: U32,
    elapsed: U32, ref: U32, deadline: U32,
    task: U32, count_posts: U32, required: U32, +base: U32) -> Array<U32>:
  (a, menu) = pair
  load_goal(a[(base + 34 : U32)], status, raw, elapsed, ref, menu,
    deadline, task, count_posts, required, base)

def load_ref(pair: Array<U32> & U32, status: U32, raw: U32,
    elapsed: U32, deadline: U32, task: U32, count_posts: U32,
    required: U32, +base: U32) -> Array<U32>:
  (a, ref) = pair
  load_menu(a[(base + 33 : U32)], status, raw, elapsed, ref, deadline,
    task, count_posts, required, base)

def load_elapsed(pair: Array<U32> & U32, status: U32, raw: U32,
    deadline: U32, task: U32, count_posts: U32,
    required: U32, +base: U32) -> Array<U32>:
  (a, elapsed) = pair
  load_ref(a[(base + 5 : U32)], status, raw, elapsed, deadline,
    task, count_posts, required, base)

def load_deadline(pair: Array<U32> & U32, status: U32, raw: U32,
    task: U32, count_posts: U32, required: U32, +base: U32) -> Array<U32>:
  (a, deadline) = pair
  load_elapsed(a[(base + 8 : U32)], status, raw, deadline,
    task, count_posts, required, base)

def load_raw(pair: Array<U32> & U32, status: U32,
    task: U32, count_posts: U32, required: U32, +base: U32) -> Array<U32>:
  (a, raw) = pair
  load_deadline(a[(base + 12 : U32)], status, raw,
    task, count_posts, required, base)

def load_status(pair: Array<U32> & U32, task: U32, count_posts: U32,
    required: U32, +base: U32) -> Array<U32>:
  (a, status) = pair
  load_raw(a[(base + 10 : U32)], status, task, count_posts, required, base)

def load_required(pair: Array<U32> & U32, task: U32,
    count_posts: U32, +base: U32) -> Array<U32>:
  (a, required) = pair
  load_status(a[(base + 9 : U32)], task, count_posts, required, base)

def load_count(pair: Array<U32> & U32, task: U32,
    +base: U32) -> Array<U32>:
  (a, count_posts) = pair
  load_required(a[(base + 36 : U32)], task, count_posts, base)

def load_task(pair: Array<U32> & U32, +base: U32) -> Array<U32>:
  (a, task) = pair
  load_count(a[(base + 32 : U32)], task, base)

def reset_seed(pair: Array<U32> & U32, task: U32, +base: U32) -> Array<U32>:
  (a, seed) = pair
  G.generate(task, seed, base, a)

def reset_task(pair: Array<U32> & U32, +base: U32) -> Array<U32>:
  (a, task) = pair
  reset_seed(a[(base + 3 : U32)], task, base)

def operation_index(op: Nat, +base: U32, a: Array<U32>) -> Array<U32>:
  match op:
    case 0n: a
    case 1n: reset_task(a[(base + 1 : U32)], base)
    case _: load_task(a[(base + 1 : U32)], base)

def operation(op: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  operation_index(U32.to_nat(op), base, a)

def row(+base: U32, pair: Array<U32> & U32) -> Array<U32>:
  (a, op) = pair
  operation(op, base, a)

def batch(n: Nat, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +base = (U32.from_nat(p) * 8192 : U32)
      batch(p, row(base, a[(base + 2 : U32)]))
