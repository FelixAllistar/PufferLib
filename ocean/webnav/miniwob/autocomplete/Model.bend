import Base
import ../Text.bend as T
import ../Widgets.bend as W
import ./Countries.bend as C

# A small, typed model of use-autocomplete-nodelay. The text editing rules are
# shared with the text-task lane. Menu state is separate because jQuery UI
# renders the suggestion popup under document.body, outside #area.
type Menu is Data:
  Closed{}
  Open{term: +List<U32>}
  Active{term: +List<U32>, index: Nat}

type Task is Data:
  Q{prefix: +List<U32>, suffix: +List<U32>, match_end: Bool}

type State is Data:
  S{task: Task, field: T.Field, focus: Nat, menu: Menu, outcome: W.Outcome}

type Action is Data:
  Wait{}
  FocusEnd{}
  Editing{edit: T.Edit}
  MenuDown{}
  MenuUp{}
  MenuSelect{index: Nat}
  MenuAccept{}
  Submit{}
  Timeout{}

def chars(xs: +List<U32>) -> +List<Char>:
  match xs:
    case Nil{}:
      Nil{}
    case Con{x, rest}:
      Char.from_u32(x) <> chars(rest)

def string(xs: +List<U32>) -> String:
  String.from_list(chars(xs))

def units(s: String) -> +List<U32>:
  match s:
    case SNil{}:
      Nil{}
    case SCon{head, tail}:
      Char.to_u32(head) <> units(tail)

# jQuery UI's source callback uses a case-insensitive ^term regular expression
# and retains the source order of the country list.
def filter_if(same: Bool, item: String, tail: +List<String>) -> +List<String>:
  match same:
    case True{}:
      item <> tail
    case False{}:
      tail

def filtered(xs: +List<String>, +query: String) -> +List<String>:
  match xs:
    case Nil{}:
      Nil{}
    case Con{+item, rest}:
      +tail = filtered(rest, query)
      filter_if(String.starts_with(String.to_lower(item), query), item, tail)

def menu_items_term(+term: +List<U32>) -> +List<String>:
  +query = String.to_lower(string(term))
  filtered(C.countries(), query)

def menu_count_term(+term: +List<U32>) -> Nat:
  List.length(&2, String, menu_items_term(term))

def menu_should_open(+term: +List<U32>) -> Bool:
  +value = string(term)
  Bool.not(String.is_empty(value)) && Bool.not(Nat.is_eq(menu_count_term(term), 0n))

def menu_items(+field: T.Field) -> +List<String>:
  menu_items_term(T.value(field))

def menu_count(+field: T.Field) -> Nat:
  menu_count_term(T.value(field))

def menu_for_if(open: Bool, +term: +List<U32>) -> Menu:
  match open:
    case True{}:
      Open{term}
    case False{}:
      Closed{}

def menu_for(+field: T.Field) -> Menu:
  +term = T.value(field)
  menu_for_if(menu_should_open(term), term)

def item_at(xs: +List<String>, index: Nat) -> Maybe<&2, String>:
  match xs index:
    case Nil{} _:
      None{}
    case Con{item, rest} 0n:
      Some{item}
    case Con{item, rest} 1n+p:
      item_at(rest, p)

def pred(n: Nat) -> Nat:
  match n:
    case 0n:
      0n
    case 1n+p:
      p

def replace_choice(choice: Maybe<&2, String>, +field: T.Field) -> Maybe<&2, T.Field>:
  match choice:
    case None{}:
      None{}
    case Some{item}:
      Some{T.replace(T.select_all(field), units(item))}

def replace_with_term_item(+field: T.Field, +term: +List<U32>, index: Nat) -> Maybe<&2, T.Field>:
  replace_choice(item_at(menu_items_term(term), index), field)

def restore(task: Task, +field: T.Field, focus: Nat, +term: +List<U32>) -> State:
  S{task, T.replace(T.select_all(field), term), focus, Open{term}, W.Running{}}

def highlight_choice(choice: Maybe<&2, String>, task: Task, +field: T.Field, focus: Nat, +term: +List<U32>, index: Nat) -> State:
  match choice:
    case None{}:
      S{task, field, focus, Closed{}, W.Running{}}
    case Some{item}:
      S{task, T.replace(T.select_all(field), units(item)), focus, Active{term, index}, W.Running{}}

def highlight(task: Task, +field: T.Field, focus: Nat, +term: +List<U32>, +index: Nat) -> State:
  highlight_choice(item_at(menu_items_term(term), index), task, field, focus, term, index)

def down_open_count(count: Nat, task: Task, +field: T.Field, focus: Nat, +term: +List<U32>) -> State:
  match count:
    case 0n:
      S{task, field, focus, Closed{}, W.Running{}}
    case 1n+p:
      highlight(task, field, focus, term, 0n)

def down_active_if(last: Bool, task: Task, +field: T.Field, focus: Nat, +term: +List<U32>, +index: Nat) -> State:
  match last:
    case True{}:
      restore(task, field, focus, term)
    case False{}:
      highlight(task, field, focus, term, 1n+index)

def down_active_count(count: Nat, task: Task, +field: T.Field, focus: Nat, +term: +List<U32>, +index: Nat) -> State:
  match count:
    case 0n:
      S{task, field, focus, Closed{}, W.Running{}}
    case 1n+ +p:
      down_active_if(Nat.is_eq(index, p), task, field, focus, term, index)

def down_search_menu(menu: Menu, task: Task, +field: T.Field, focus: Nat) -> State:
  match menu:
    case Closed{}:
      S{task, field, focus, Closed{}, W.Running{}}
    case Open{+term}:
      down_open_count(menu_count_term(term), task, field, focus, term)
    case Active{+term, +index}:
      down_active_count(menu_count_term(term), task, field, focus, term, index)

def down_search(task: Task, +field: T.Field, focus: Nat) -> State:
  S{task, field, focus, menu_for(field), W.Running{}}

def down_menu(menu: Menu, task: Task, +field: T.Field, focus: Nat) -> State:
  match menu:
    case Closed{}:
      down_search(task, field, focus)
    case Open{+term}:
      down_open_count(menu_count_term(term), task, field, focus, term)
    case Active{+term, +index}:
      down_active_count(menu_count_term(term), task, field, focus, term, index)

def up_open_count(count: Nat, task: Task, +field: T.Field, focus: Nat, +term: +List<U32>) -> State:
  match count:
    case 0n:
      S{task, field, focus, Closed{}, W.Running{}}
    case 1n+p:
      highlight(task, field, focus, term, p)

def up_active_if(first: Bool, task: Task, +field: T.Field, focus: Nat, +term: +List<U32>, +index: Nat) -> State:
  match first:
    case True{}:
      restore(task, field, focus, term)
    case False{}:
      highlight(task, field, focus, term, pred(index))

def up_active_count(count: Nat, task: Task, +field: T.Field, focus: Nat, +term: +List<U32>, +index: Nat) -> State:
  match count:
    case 0n:
      S{task, field, focus, Closed{}, W.Running{}}
    case 1n+p:
      up_active_if(Nat.is_eq(index, 0n), task, field, focus, term, index)

def up_search_menu(menu: Menu, task: Task, +field: T.Field, focus: Nat) -> State:
  match menu:
    case Closed{}:
      S{task, field, focus, Closed{}, W.Running{}}
    case Open{+term}:
      up_open_count(menu_count_term(term), task, field, focus, term)
    case Active{+term, +index}:
      up_active_count(menu_count_term(term), task, field, focus, term, index)

def up_search(task: Task, +field: T.Field, focus: Nat) -> State:
  S{task, field, focus, menu_for(field), W.Running{}}

def up_menu(menu: Menu, task: Task, +field: T.Field, focus: Nat) -> State:
  match menu:
    case Closed{}:
      up_search(task, field, focus)
    case Open{+term}:
      up_open_count(menu_count_term(term), task, field, focus, term)
    case Active{+term, +index}:
      up_active_count(menu_count_term(term), task, field, focus, term, index)

def changed(+edit: T.Edit) -> Bool:
  match edit:
    case T.Insert{units_}:
      True{}
    case T.Backspace{}:
      True{}
    case T.Delete{}:
      True{}
    case _:
      False{}

def edit_state_if(changes: Bool, task: Task, +next: T.Field, focus: Nat, menu: Menu) -> State:
  match changes:
    case True{}:
      S{task, next, focus, menu_for(next), W.Running{}}
    case False{}:
      S{task, next, focus, menu, W.Running{}}

def edit_state(task: Task, +field: T.Field, focus: Nat, menu: Menu, +edit: T.Edit) -> State:
  +next = T.edit(edit, field)
  edit_state_if(changed(edit), task, next, focus, menu)

def edit_if_focused(task: Task, +field: T.Field, focus: Nat, menu: Menu, edit: T.Edit) -> State:
  match focus:
    case 1n:
      edit_state(task, field, focus, menu, edit)
    case _:
      S{task, field, focus, menu, W.Running{}}

def choose_replacement(choice: Maybe<&2, T.Field>, task: Task, +field: T.Field, focus: Nat, menu: Menu) -> State:
  match choice:
    case None{}:
      S{task, field, focus, menu, W.Running{}}
    case Some{next}:
      S{task, next, focus, Closed{}, W.Running{}}

def choose_open(task: Task, +field: T.Field, focus: Nat, +term: +List<U32>, index: Nat) -> State:
  choose_replacement(replace_with_term_item(field, term, index), task, field, focus, Open{term})

def choose_state(task: Task, +field: T.Field, focus: Nat, menu: Menu, +index: Nat) -> State:
  match menu:
    case Closed{}:
      S{task, field, focus, menu, W.Running{}}
    case Open{+term}:
      choose_open(task, field, focus, term, index)
    case Active{+term, active}:
      choose_open(task, field, focus, term, index)

def accept_active(task: Task, +field: T.Field, focus: Nat, +term: +List<U32>, +index: Nat) -> State:
  choose_replacement(replace_with_term_item(field, term, index), task, field, focus, Closed{})

def accept_state(task: Task, +field: T.Field, focus: Nat, menu: Menu) -> State:
  match menu:
    case Active{+term, +index}:
      accept_active(task, field, focus, term, index)
    case _:
      S{task, field, focus, menu, W.Running{}}

def down_if_focused(task: Task, +field: T.Field, focus: Nat, menu: Menu) -> State:
  match focus:
    case 1n:
      down_menu(menu, task, field, focus)
    case _:
      S{task, field, focus, menu, W.Running{}}

def up_if_focused(task: Task, +field: T.Field, focus: Nat, menu: Menu) -> State:
  match focus:
    case 1n:
      up_menu(menu, task, field, focus)
    case _:
      S{task, field, focus, menu, W.Running{}}

def select_if_focused(task: Task, +field: T.Field, focus: Nat, menu: Menu, +index: Nat) -> State:
  match focus:
    case 1n:
      choose_state(task, field, focus, menu, index)
    case _:
      S{task, field, focus, menu, W.Running{}}

def accept_if_focused(task: Task, +field: T.Field, focus: Nat, menu: Menu) -> State:
  match focus:
    case 1n:
      accept_state(task, field, focus, menu)
    case _:
      S{task, field, focus, menu, W.Running{}}

def satisfies(task: Task, +field: T.Field) -> Bool:
  Q{+prefix, +suffix, match_end} = task
  +value = string(T.value(field))
  +prefix_text = string(prefix)
  +suffix_text = string(suffix)
  +starts = String.starts_with(value, prefix_text)
  +ends = String.ends_with(value, suffix_text)
  starts && Bool.pick(Bool, match_end, ends, True{})

def submit(+task: Task, +field: T.Field, focus: Nat, menu: Menu) -> State:
  S{task, field, 2n, Closed{}, W.binary(satisfies(task, field))}

def active(action: Action, task: Task, +field: T.Field, focus: Nat, menu: Menu) -> State:
  match action:
    case Wait{}:
      S{task, field, focus, menu, W.Running{}}
    case FocusEnd{}:
      S{task, T.end(field), 1n, menu, W.Running{}}
    case Editing{edit}:
      edit_if_focused(task, field, focus, menu, edit)
    case MenuDown{}:
      down_if_focused(task, field, focus, menu)
    case MenuUp{}:
      up_if_focused(task, field, focus, menu)
    case MenuSelect{index}:
      select_if_focused(task, field, focus, menu, index)
    case MenuAccept{}:
      accept_if_focused(task, field, focus, menu)
    case Submit{}:
      submit(task, field, focus, menu)
    case Timeout{}:
      S{task, field, focus, menu, W.TimedOut{}}

def step(s: State, action: Action) -> State:
  match s:
    case S{task, +field, focus, menu, W.Running{}}:
      active(action, task, field, focus, menu)
    case S{task, field, focus, menu, outcome}:
      S{task, field, focus, menu, outcome}

def deadline(expired: Bool, +s: State, action: Action) -> State:
  match expired:
    case True{}:
      step(s, Timeout{})
    case False{}:
      step(s, action)

def step_at(s: State, action: Action, elapsed: U32) -> State:
  deadline(U32.is_ge(elapsed, 10000), s, action)

def value(s: State) -> +List<U32>:
  S{task, field, focus, menu, outcome} = s
  T.value(field)

def task_of(s: State) -> Task:
  S{task, field, focus, menu, outcome} = s
  task

def menu_open(s: State) -> Bool:
  match s:
    case S{task, field, focus, Closed{}, outcome}:
      False{}
    case _:
      True{}

def menu_index(s: State) -> Nat:
  match s:
    case S{task, field, focus, Active{term, index}, outcome}:
      1n+index
    case _:
      0n

def menu_size_of(menu: Menu) -> Nat:
  match menu:
    case Closed{}:
      0n
    case Open{term}:
      menu_count_term(term)
    case Active{term, index}:
      menu_count_term(term)

def menu_size(s: State) -> Nat:
  S{task, field, focus, menu, outcome} = s
  menu_size_of(menu)

def menu_kind(s: State) -> U32:
  match s:
    case S{task, field, focus, Closed{}, outcome}:
      0
    case S{task, field, focus, Open{term}, outcome}:
      1
    case S{task, field, focus, Active{term, index}, outcome}:
      2

def menu_term(s: State) -> +List<U32>:
  match s:
    case S{task, field, focus, Closed{}, outcome}:
      Nil{}
    case S{task, field, focus, Open{term}, outcome}:
      term
    case S{task, field, focus, Active{term, index}, outcome}:
      term

def task_prefix(task: Task) -> +List<U32>:
  Q{prefix, suffix, match_end} = task
  prefix

def task_suffix(task: Task) -> +List<U32>:
  Q{prefix, suffix, match_end} = task
  suffix

def task_match_end(task: Task) -> Bool:
  Q{prefix, suffix, match_end} = task
  match_end
