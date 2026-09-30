import Base
import ./Model.bend as M
import ./Generate.bend as G

# Each 8192-word lane has the ABI header, 12 fixed email slots, public query,
# editable buffers, private expected reply/recipient and command payload.
def email_at(+base: U32, +index: U32) -> U32:
  (base + 64 + (index - 1) * 256 : U32)

def compared_right(pair: Array<U32> & U32, x: U32, same: Bool) -> Array<U32> & Bool:
  (a, y) = pair
  (a, same && U32.is_eq(x, y))

def compared_left(pair: Array<U32> & U32, +right: U32, +i: U32,
    same: Bool) -> Array<U32> & Bool:
  (a, x) = pair
  compared_right(a[(right + i : U32)], x, same)

def same_words(n: Nat, +left: U32, +right: U32,
    pair: Array<U32> & Bool) -> Array<U32> & Bool:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, same) = pair
      +i = U32.from_nat(p)
      same_words(p, left, right,
        compared_left(a[(left + i : U32)], right, i, same))

def copy_read(pair: Array<U32> & U32, +dst: U32) -> Array<U32>:
  (a, ch) = pair
  a[dst] <- ch

def copy_words(n: Nat, +src: U32, +dst: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +i = U32.from_nat(p)
      copy_words(p, src, dst,
        copy_read(a[(src + i : U32)], (dst + i : U32)))

def zero(n: Nat, +dst: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p: zero(p, dst, a[(dst + U32.from_nat(p) : U32)] <- 0)

def selected_same(+selected: U32, +goal: U32, +base: U32,
    a: Array<U32>) -> Array<U32> & Bool:
  same_words(32n, (email_at(base, selected) + 8 : U32),
    (email_at(base, goal) + 8 : U32), (a, True{}))

def toggled_star(pair: Array<U32> & U32, +at: U32, same: Bool) -> Array<U32> & M.Action:
  (a, old) = pair
  (a[at] <- Bool.to_u32(U32.is_eq(old, 0)), M.Star{same})

def email_action_slot(slot: Nat, +index: U32, +same: Bool,
    +base: U32, a: Array<U32>) -> Array<U32> & M.Action:
  match slot:
    case 0n: (a, M.OpenEmail{index})
    case 1n:
      +at = (email_at(base, index) + 3 : U32)
      toggled_star(a[at], at, same)
    case 2n: (a, M.Trash{same})
    case 3n: (a, M.OpenEmail{index})
    case 4n: (a, M.OpenEmail{index})
    case 5n: (a, M.OpenEmail{index})
    case 6n:
      +at = (email_at(base, index) + 5 : U32)
      toggled_star(a[at], at, same)
    case _: (a, M.Trash{same})

def email_action_same(pair: Array<U32> & Bool, slot: Nat, index: U32,
    +base: U32) -> Array<U32> & M.Action:
  (a, same) = pair
  email_action_slot(slot, index, same, base, a)

def email_action(+target: U32, goal: U32, +base: U32,
    a: Array<U32>) -> Array<U32> & M.Action:
  +i = ((target - 10) / 8 + 1 : U32)
  +slot = U32.to_nat(((target - 10) % 8 : U32))
  email_action_same(selected_same(i, goal, base, a), slot, i, base)

def reply_action_text(pair: Array<U32> & Bool, same: Bool) -> Array<U32> & M.Action:
  (a, text_same) = pair
  (a, M.SendReply{same, text_same})

def reply_action_same(pair: Array<U32> & Bool, +base: U32) -> Array<U32> & M.Action:
  (a, same) = pair
  reply_action_text(same_words(160n, (base + 4500 : U32),
    (base + 5100 : U32), (a, True{})), same)

def reply_action(+selected: U32, +goal: U32, +base: U32,
    a: Array<U32>) -> Array<U32> & M.Action:
  reply_action_same(selected_same(selected, goal, base, a), base)

def forward_action_body(pair: Array<U32> & Bool, recipient_same: Bool) -> Array<U32> & M.Action:
  (a, body_same) = pair
  (a, M.SendForward{recipient_same, body_same})

def forward_action_recipient(pair: Array<U32> & Bool, +goal: U32,
    +base: U32) -> Array<U32> & M.Action:
  (a, recipient_same) = pair
  forward_action_body(same_words(160n, (base + 4800 : U32),
    (email_at(base, goal) + 80 : U32), (a, True{})), recipient_same)

def forward_action(+goal: U32, +base: U32,
    a: Array<U32>) -> Array<U32> & M.Action:
  forward_action_recipient(same_words(64n, (base + 4700 : U32),
    (base + 5300 : U32), (a, True{})), goal, base)

def fixed_action(ref: Nat, selected: U32, goal: U32, +base: U32,
    a: Array<U32>) -> Array<U32> & M.Action:
  match ref:
    case 1n: (a, M.OpenSearch{})
    case 2n: (a, M.Focus{1})
    case 3n: (a, M.CancelSearch{})
    case 112n: (a, M.CloseEmail{})
    case 113n: (a, M.OpenReply{})
    case 114n: (a, M.OpenForward{})
    case 115n: (a, M.CloseReply{})
    case 116n: reply_action(selected, goal, base, a)
    case 117n: (a, M.Focus{2})
    case 118n: (a, M.CloseForward{})
    case 119n: forward_action(goal, base, a)
    case 120n: (a, M.Focus{3})
    case 121n: (a, M.Focus{4})
    case _: (a, M.Wait{})

def click_action(dynamic: Bool, target: U32, selected: U32,
    goal: U32, +base: U32, a: Array<U32>) -> Array<U32> & M.Action:
  match dynamic:
    case True{}: email_action(target, goal, base, a)
    case False{}: fixed_action(U32.to_nat(target), selected, goal, base, a)

def prepare_forward_len(pair: Array<U32> & U32, +at: U32,
    +base: U32) -> Array<U32>:
  (a, len) = pair
  a = zero(64n, (base + 4700 : U32), a)
  a = a[(base + 38 : U32)] <- 0
  a = a[(base + 39 : U32)] <- len
  copy_words(160n, (at + 80 : U32), (base + 4800 : U32), a)

def prepare_forward(selected: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +at = email_at(base, selected)
  prepare_forward_len(a[(at + 2 : U32)], at, base)

def after_ref(ref: Nat, selected: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  match ref:
    case 113n:
      a = a[(base + 37 : U32)] <- 0
      zero(160n, (base + 4500 : U32), a)
    case 114n:
      prepare_forward(selected, base, a)
    case 3n:
      a = a[(base + 36 : U32)] <- 0
      zero(128n, (base + 4300 : U32), a)
    case _: a

def after_email(slot: Nat, index: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  match slot:
    case 0n: a[(email_at(base, index) + 5 : U32)] <- 0
    case 3n: a[(email_at(base, index) + 5 : U32)] <- 0
    case 4n: a[(email_at(base, index) + 5 : U32)] <- 0
    case 5n: a[(email_at(base, index) + 5 : U32)] <- 0
    case _: a

def after_click_kind(dynamic: Bool, +target: U32, selected: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match dynamic:
    case True{}:
      after_email(U32.to_nat(((target - 10) % 8 : U32)),
        ((target - 10) / 8 + 1 : U32), base, a)
    case False{}: after_ref(U32.to_nat(target), selected, base, a)

def after_click(+target: U32, selected: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  after_click_kind(U32.is_ge(target, 10) && U32.is_lt(target, 106),
    target, selected, base, a)

def save_outcome(outcome: M.Outcome, elapsed: U32, deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match outcome:
    case M.Running{}: a
    case M.Finished{+success, +timed}:
      a = a[(base + 9 : U32)] <- 1
      a = a[(base + 10 : U32)] <- F32.bits(M.raw(M.Finished{success, timed}))
      a[(base + 11 : U32)] <- F32.bits(M.reward(M.Finished{success, timed}, elapsed, deadline))
    case M.TimedOut{}:
      a = a[(base + 9 : U32)] <- 2
      a = a[(base + 10 : U32)] <- 3212836864
      a[(base + 11 : U32)] <- 3212836864

def save(s: M.State, elapsed: U32, deadline: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  M.S{screen, selected, focus, goal, outcome} = s
  a = a[(base + 33 : U32)] <- M.screen_code(screen)
  a = a[(base + 34 : U32)] <- selected
  a = a[(base + 35 : U32)] <- focus
  save_outcome(outcome, elapsed, deadline, base, a)

def click_step_action(pair: Array<U32> & M.Action, +target: U32,
    selected: U32, old: M.State, +elapsed: U32, +deadline: U32,
    +base: U32) -> Array<U32>:
  (a, action) = pair
  new = M.step(old, action, elapsed, deadline)
  save(new, elapsed, deadline, base, after_click(target, selected, base, a))

def click_step(+target: U32, goal: U32, +s: M.State, elapsed: U32,
    deadline: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  M.S{screen, +selected, focus, kind, outcome} = s
  old = {M.S{screen, selected, focus, kind, outcome} : M.State}
  click_step_action(click_action(U32.is_ge(target, 10) && U32.is_lt(target, 106),
    target, selected, goal, base, a), target, selected, old, elapsed, deadline, base)

def focus_buffer_index(focus: Nat, +base: U32) -> U32:
  match focus:
    case 1n: (base + 4300 : U32)
    case 2n: (base + 4500 : U32)
    case 3n: (base + 4700 : U32)
    case _: (base + 4800 : U32)

def focus_buffer(focus: U32, +base: U32) -> U32:
  focus_buffer_index(U32.to_nat(focus), base)

def focus_cap(+focus: U32) -> U32:
  Bool.pick(U32, U32.is_eq(focus, 1), 127,
    Bool.pick(U32, U32.is_eq(focus, 3), 63, 159))

def insert_char(pair: Array<U32> & U32, +at: U32) -> Array<U32>:
  (a, ch) = pair
  a[at] <- ch

def insert_chars(n: Nat, +src: U32, +dst: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +i = U32.from_nat(p)
      insert_chars(p, src, dst,
        insert_char(a[(src + i : U32)], (dst + i : U32)))

def clear_field(focus: Nat, +at: U32, a: Array<U32>) -> Array<U32>:
  match focus:
    case 1n: zero(128n, at, a)
    case 3n: zero(64n, at, a)
    case _: zero(160n, at, a)

def clear_selected(selected: Bool, focus: U32, +at: U32, a: Array<U32>) -> Array<U32>:
  match selected:
    case True{}: clear_field(U32.to_nat(focus), at, a)
    case False{}: a

def insert_ready(+focus: U32, len: U32, +payload: U32, +selected: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  +at = focus_buffer(focus, base)
  +start = Bool.pick(U32, U32.is_eq(selected, 1), 0, len)
  +newlen = (start + payload : U32)
  a = clear_selected(U32.is_eq(selected, 1), focus, at, a)
  a = insert_chars(U32.to_nat(payload), (base + 5500 : U32), (at + start : U32), a)
  a = a[(at + newlen : U32)] <- 0
  a = a[(base + 45 : U32)] <- 0
  a[(base + 35 + focus : U32)] <- newlen

def edit_insert_selected(pair: Array<U32> & U32, focus: U32,
    len: U32, payload: U32, +base: U32) -> Array<U32>:
  (a, selected) = pair
  insert_ready(focus, len, payload, selected, base, a)

def edit_insert_len(pair: Array<U32> & U32, +focus: U32,
    payload: U32, +base: U32) -> Array<U32>:
  (a, len) = pair
  edit_insert_selected(a[(base + 45 : U32)], focus, len, payload, base)

def edit_insert(+focus: U32, payload: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  edit_insert_len(a[(base + 35 + focus : U32)], focus, payload, base)

def backspace_selected(selected: Bool, +focus: U32, +base: U32,
    newlen: U32, a: Array<U32>) -> Array<U32>:
  match selected:
    case True{}: clear_field(U32.to_nat(focus), focus_buffer(focus, base), a)
    case False{}: a[(focus_buffer(focus, base) + newlen : U32)] <- 0

def edit_backspace_selected(pair: Array<U32> & U32, +focus: U32,
    +len: U32, +base: U32) -> Array<U32>:
  (a, +selected) = pair
  +newlen = Bool.pick(U32, U32.is_eq(selected, 1), 0,
    Bool.pick(U32, U32.is_eq(len, 0), 0, (len - 1 : U32)))
  a = a[(base + 45 : U32)] <- 0
  a = a[(base + 35 + focus : U32)] <- newlen
  backspace_selected(U32.is_eq(selected, 1), focus, base, newlen, a)

def edit_backspace_len(pair: Array<U32> & U32, +focus: U32,
    +base: U32) -> Array<U32>:
  (a, len) = pair
  edit_backspace_selected(a[(base + 45 : U32)], focus, len, base)

def edit_backspace(+focus: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  edit_backspace_len(a[(base + 35 + focus : U32)], focus, base)

def delete_selected(selected: Bool, +focus: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  match selected:
    case True{}:
      a = a[(base + 45 : U32)] <- 0
      a = a[(base + 35 + focus : U32)] <- 0
      clear_field(U32.to_nat(focus), focus_buffer(focus, base), a)
    case False{}: a

def edit_delete_selected(pair: Array<U32> & U32, focus: U32,
    +base: U32) -> Array<U32>:
  (a, selected) = pair
  delete_selected(U32.is_eq(selected, 1), focus, base, a)

def edit_delete(focus: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  edit_delete_selected(a[(base + 45 : U32)], focus, base)

def edit_action(kind: Nat, focus: U32, payload: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match kind:
    case 2n: edit_insert(focus, payload, base, a)
    case 3n: edit_backspace(focus, base, a)
    case 4n: edit_delete(focus, base, a)
    case 9n: a[(base + 45 : U32)] <- 1
    case _: a

# Search rows are materialized by the page on keyup. Cancel clears the input,
# but leaves those rows in the DOM until the next keyup.
def search_position_merge(pair: Array<U32> & Bool, found: Bool) -> Array<U32> & Bool:
  (a, hit) = pair
  (a, found || hit)

def search_positions(n: Nat, +hay: U32, +needle: U32, +query_len: U32,
    pair: Array<U32> & Bool) -> Array<U32> & Bool:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, found) = pair
      search_positions(p, hay, needle, query_len,
        search_position_merge(same_words(U32.to_nat(query_len),
          (hay + U32.from_nat(p) : U32), needle, (a, True{})), found))

def search_field_fits(fits: Bool, hay_len: U32, +query_len: U32,
    +hay: U32, +needle: U32, a: Array<U32>) -> Array<U32> & Bool:
  match fits:
    case True{}:
      search_positions(U32.to_nat((hay_len - query_len + 1 : U32)),
        hay, needle, query_len, (a, False{}))
    case False{}: (a, False{})

def search_field(+hay_len: U32, +query_len: U32, +hay: U32,
    +needle: U32, a: Array<U32>) -> Array<U32> & Bool:
  search_field_fits(U32.is_le(query_len, hay_len), hay_len, query_len,
    hay, needle, a)

def search_email_body(pair: Array<U32> & Bool, name_match: Bool,
    subject_match: Bool) -> Array<U32> & Bool:
  (a, body_match) = pair
  (a, name_match || subject_match || body_match)

def search_email_subject(pair: Array<U32> & Bool, name_match: Bool,
    body_len: U32, query_len: U32, +at: U32,
    +base: U32) -> Array<U32> & Bool:
  (a, subject_match) = pair
  search_email_body(search_field(body_len, query_len,
    (at + 80 : U32), (base + 4300 : U32), a), name_match, subject_match)

def search_email_name(pair: Array<U32> & Bool, subject_len: U32,
    body_len: U32, +query_len: U32, +at: U32,
    +base: U32) -> Array<U32> & Bool:
  (a, name_match) = pair
  search_email_subject(search_field(subject_len, query_len,
    (at + 40 : U32), (base + 4300 : U32), a), name_match,
    body_len, query_len, at, base)

def search_email_body_len(pair: Array<U32> & U32, name_len: U32,
    subject_len: U32, +query_len: U32, +at: U32,
    +base: U32) -> Array<U32> & Bool:
  (a, body_len) = pair
  search_email_name(search_field(name_len, query_len,
    (at + 8 : U32), (base + 4300 : U32), a), subject_len,
    body_len, query_len, at, base)

def search_email_subject_len(pair: Array<U32> & U32, name_len: U32,
    +query_len: U32, +at: U32, +base: U32) -> Array<U32> & Bool:
  (a, subject_len) = pair
  search_email_body_len(a[(at + 2 : U32)], name_len, subject_len,
    query_len, at, base)

def search_email_name_len(pair: Array<U32> & U32, +query_len: U32,
    +at: U32, +base: U32) -> Array<U32> & Bool:
  (a, name_len) = pair
  search_email_subject_len(a[(at + 1 : U32)], name_len,
    query_len, at, base)

def search_email(index: U32, query_len: U32, +base: U32,
    a: Array<U32>) -> Array<U32> & Bool:
  +at = email_at(base, (index + 1 : U32))
  search_email_name_len(a[at], query_len, at, base)

def search_mask_hit(pair: Array<U32> & Bool, mask: U32,
    index: Nat) -> Array<U32> & U32:
  (a, hit) = pair
  (a, (mask .|. (Bool.to_u32(hit) << index : U32) : U32))

def search_mask(n: Nat, +query_len: U32, +base: U32,
    pair: Array<U32> & U32) -> Array<U32> & U32:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, mask) = pair
      +index = U32.from_nat(p)
      search_mask(p, query_len, base,
        search_mask_hit(search_email(index, query_len, base, a), mask, p))

def search_nonblank_char(pair: Array<U32> & U32, seen: Bool) -> Array<U32> & Bool:
  (a, +ch) = pair
  (a, seen || (U32.is_ne(ch, 32) && U32.is_ne(ch, 9)))

def search_nonblank(n: Nat, +at: U32,
    pair: Array<U32> & Bool) -> Array<U32> & Bool:
  match n:
    case 0n: pair
    case 1n+ +p:
      (a, seen) = pair
      search_nonblank(p, at,
        search_nonblank_char(a[(at + U32.from_nat(p) : U32)], seen))

def search_store_mask(pair: Array<U32> & U32, +base: U32) -> Array<U32>:
  (a, mask) = pair
  a[(base + 46 : U32)] <- mask

def search_refresh_nonblank(nonblank: Bool, count: U32, len: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match nonblank:
    case True{}:
      search_store_mask(search_mask(U32.to_nat(count), len, base, (a, 0)), base)
    case False{}: a[(base + 46 : U32)] <- 0

def search_refresh_seen(pair: Array<U32> & Bool, count: U32,
    len: U32, +base: U32) -> Array<U32>:
  (a, nonblank) = pair
  search_refresh_nonblank(nonblank, count, len, base, a)

def search_refresh_count(pair: Array<U32> & U32, +len: U32,
    +base: U32) -> Array<U32>:
  (a, count) = pair
  search_refresh_seen(search_nonblank(U32.to_nat(len),
    (base + 4300 : U32), (a, False{})), count, len, base)

def search_refresh_len(pair: Array<U32> & U32, +base: U32) -> Array<U32>:
  (a, len) = pair
  search_refresh_count(a[(base + 32 : U32)], len, base)

def search_refresh(+base: U32, a: Array<U32>) -> Array<U32>:
  search_refresh_len(a[(base + 36 : U32)], base)

def search_edit_focus(search: Bool, +base: U32, a: Array<U32>) -> Array<U32>:
  match search:
    case True{}: search_refresh(base, a)
    case False{}: a

def search_edit_kind(kind: Nat, focus: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  match kind:
    case 2n: search_edit_focus(U32.is_eq(focus, 1), base, a)
    case 3n: search_edit_focus(U32.is_eq(focus, 1), base, a)
    case 4n: search_edit_focus(U32.is_eq(focus, 1), base, a)
    case 9n: search_edit_focus(U32.is_eq(focus, 1), base, a)
    case _: a

def event(+kind: Nat, target: U32, payload: U32, goal: U32,
    +s: M.State, elapsed: U32, deadline: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  match kind:
    case 1n: click_step(target, goal, s, elapsed, deadline, base, a)
    case _:
      M.S{screen, selected, +focus, goal_kind, outcome} = s
      search_edit_kind(kind, focus, base, edit_action(kind, focus, payload, base, a))

def active(expired: Bool, kind: U32, target: U32, payload: U32,
    goal: U32, +s: M.State, elapsed: U32, deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match expired:
    case True{}: save(M.timeout(s), elapsed, deadline, base, a)
    case False{}: event(U32.to_nat(kind), target, payload, goal, s, elapsed, deadline, base, a)

def run_status(status: Nat, kind: U32, target: U32, payload: U32,
    goal_index: U32, s: M.State, +elapsed: U32, +deadline: U32,
    +base: U32, a: Array<U32>) -> Array<U32>:
  match status:
    case 0n: active(U32.is_ge(elapsed, deadline), kind, target, payload,
      goal_index, s, elapsed, deadline, base, a)
    case _: a

def run_deadline(pair: Array<U32> & U32, status: U32, screen: U32,
    selected: U32, focus: U32, goal: U32, goal_index: U32, kind: U32,
    target: U32, payload: U32, elapsed: U32, +base: U32) -> Array<U32>:
  (a, deadline) = pair
  a = a[(base + 2 : U32)] <- 0
  s = {M.S{M.decode_screen(screen), selected, focus, M.decode_goal(goal), M.Running{}} : M.State}
  run_status(U32.to_nat(status), kind, target, payload, goal_index, s,
    elapsed, deadline, base, a)

def run_elapsed(pair: Array<U32> & U32, status: U32, screen: U32,
    selected: U32, focus: U32, goal: U32, goal_index: U32, kind: U32,
    target: U32, payload: U32, +base: U32) -> Array<U32>:
  (a, elapsed) = pair
  run_deadline(a[(base + 12 : U32)], status, screen, selected, focus,
    goal, goal_index, kind, target, payload, elapsed, base)

def run_payload(pair: Array<U32> & U32, status: U32, screen: U32,
    selected: U32, focus: U32, goal: U32, goal_index: U32, kind: U32,
    target: U32, +base: U32) -> Array<U32>:
  (a, payload) = pair
  run_elapsed(a[(base + 8 : U32)], status, screen, selected, focus,
    goal, goal_index, kind, target, payload, base)

def run_target(pair: Array<U32> & U32, status: U32, screen: U32,
    selected: U32, focus: U32, goal: U32, goal_index: U32, kind: U32,
    +base: U32) -> Array<U32>:
  (a, target) = pair
  run_payload(a[(base + 6 : U32)], status, screen, selected, focus,
    goal, goal_index, kind, target, base)

def run_kind(pair: Array<U32> & U32, status: U32, screen: U32,
    selected: U32, focus: U32, goal: U32, goal_index: U32,
    +base: U32) -> Array<U32>:
  (a, kind) = pair
  run_target(a[(base + 5 : U32)], status, screen, selected, focus,
    goal, goal_index, kind, base)

def run_goal_index(pair: Array<U32> & U32, status: U32, screen: U32,
    selected: U32, focus: U32, goal: U32, +base: U32) -> Array<U32>:
  (a, goal_index) = pair
  run_kind(a[(base + 4 : U32)], status, screen, selected, focus,
    goal, goal_index, base)

def run_goal(pair: Array<U32> & U32, status: U32, screen: U32,
    selected: U32, focus: U32, +base: U32) -> Array<U32>:
  (a, goal) = pair
  run_goal_index(a[(base + 41 : U32)], status, screen, selected, focus,
    goal, base)

def run_focus(pair: Array<U32> & U32, status: U32, screen: U32,
    selected: U32, +base: U32) -> Array<U32>:
  (a, focus) = pair
  run_goal(a[(base + 40 : U32)], status, screen, selected, focus, base)

def run_selected(pair: Array<U32> & U32, status: U32, screen: U32,
    +base: U32) -> Array<U32>:
  (a, selected) = pair
  run_focus(a[(base + 35 : U32)], status, screen, selected, base)

def run_screen(pair: Array<U32> & U32, status: U32,
    +base: U32) -> Array<U32>:
  (a, screen) = pair
  run_selected(a[(base + 34 : U32)], status, screen, base)

def run_status_field(pair: Array<U32> & U32, +base: U32) -> Array<U32>:
  (a, status) = pair
  run_screen(a[(base + 33 : U32)], status, base)

def run(+base: U32, a: Array<U32>) -> Array<U32>:
  run_status_field(a[(base + 9 : U32)], base)

def reset_seed(pair: Array<U32> & U32, task: U32,
    +base: U32) -> Array<U32>:
  (a, seed) = pair
  G.generate(task, seed, base, a)

def reset_task(pair: Array<U32> & U32, +base: U32) -> Array<U32>:
  (a, task) = pair
  reset_seed(a[(base + 3 : U32)], task, base)

def reset(+base: U32, a: Array<U32>) -> Array<U32>:
  reset_task(a[(base + 1 : U32)], base)

def operation_index(op: Nat, +base: U32, a: Array<U32>) -> Array<U32>:
  match op:
    case 0n: a
    case 1n: reset(base, a)
    case _: run(base, a)

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
