import Base
import ./Lists.bend as L

type MovePair is Data:
  Pair{left: +List<U32>, right: +List<U32>}

type Species is Data:
  P{id: U32, base: U32, first: U32, second: U32, stats: +List<U32>,
    gender: U32, nfe: Bool, required: U32, items: +List<U32>, forms: +List<U32>,
    sample_form: Bool, level: U32, templates: +List<U32>, native_abilities: +List<U32>,
    weaknesses: +List<U32>}

type Info is Data:
  I{mon: Species, role: U32, tera: U32, lead: Bool, abilities: +List<U32>,
    team: +List<U32>}

type Counter is Data:
  C{counts: +List<U32>, damaging: +List<U32>, base_power: +List<U32>}

type CounterCache is Data:
  Uncounted{}
  Counted{counter: Counter}

type Builder is Data:
  B{info: Info, pool: +List<U32>, moves: +List<U32>, counter: CounterCache}

type GeneratedSet is Data:
  Set{mon: U32, display: U32, role: U32, tera: U32, ability: U32, item: U32,
    level: U32, gender: U32, shiny: Bool, moves: +List<U32>, evs: +List<U32>, ivs: +List<U32>}

def species(s: Species) -> U32:
  P{id, _, _, _, _, _, _, _, _, _, _, _, _, _, _} = s
  id

def base(s: Species) -> U32:
  P{_, base_, _, _, _, _, _, _, _, _, _, _, _, _, _} = s
  base_

def first(s: Species) -> U32:
  P{_, _, first_, _, _, _, _, _, _, _, _, _, _, _, _} = s
  first_

def types(s: Species) -> +List<U32>:
  P{_, _, first_, +second_, _, _, _, _, _, _, _, _, _, _, _} = s
  first_ <> L.keep_member(U32.is_ne(second_, 0), second_, Nil{})

def stats(s: Species) -> +List<U32>:
  P{_, _, _, _, stats_, _, _, _, _, _, _, _, _, _, _} = s
  stats_

def gender(s: Species) -> U32:
  P{_, _, _, _, _, gender_, _, _, _, _, _, _, _, _, _} = s
  gender_

def nfe(s: Species) -> Bool:
  P{_, _, _, _, _, _, nfe_, _, _, _, _, _, _, _, _} = s
  nfe_

def required(s: Species) -> U32:
  P{_, _, _, _, _, _, _, required_, _, _, _, _, _, _, _} = s
  required_

def items(s: Species) -> +List<U32>:
  P{_, _, _, _, _, _, _, _, items_, _, _, _, _, _, _} = s
  items_

def forms(s: Species) -> +List<U32>:
  P{_, _, _, _, _, _, _, _, _, forms_, _, _, _, _, _} = s
  forms_

def sample_form(s: Species) -> Bool:
  P{_, _, _, _, _, _, _, _, _, _, sample_, _, _, _, _} = s
  sample_

def level(s: Species) -> U32:
  P{_, _, _, _, _, _, _, _, _, _, _, level_, _, _, _} = s
  level_

def templates(s: Species) -> +List<U32>:
  P{_, _, _, _, _, _, _, _, _, _, _, _, templates_, _, _} = s
  templates_

def native_abilities(s: Species) -> +List<U32>:
  P{_, _, _, _, _, _, _, _, _, _, _, _, _, abilities_, _} = s
  abilities_

def weaknesses(s: Species) -> +List<U32>:
  P{_, _, _, _, _, _, _, _, _, _, _, _, _, _, weaknesses_} = s
  weaknesses_

def info(b: Builder) -> Info:
  B{i, _, _, _} = b
  i

def pool(b: Builder) -> +List<U32>:
  B{_, pool_, _, _} = b
  pool_

def moves(b: Builder) -> +List<U32>:
  B{_, _, moves_, _} = b
  moves_

def with_pool(b: Builder, pool_: +List<U32>) -> Builder:
  B{i, _, moves_, counter_} = b
  B{i, pool_, moves_, counter_}

def with_moves(b: Builder, moves_: +List<U32>) -> Builder:
  B{i, pool_, _, _} = b
  B{i, pool_, moves_, Uncounted{}}

def with_counter(b: Builder, counter_: Counter) -> Builder:
  B{i, pool_, moves_, _} = b
  B{i, pool_, moves_, Counted{counter_}}

def mon(i: Info) -> Species:
  I{p, _, _, _, _, _} = i
  p

def role(i: Info) -> U32:
  I{_, r, _, _, _, _} = i
  r

def tera(i: Info) -> U32:
  I{_, _, t, _, _, _} = i
  t

def lead(i: Info) -> Bool:
  I{_, _, _, l, _, _} = i
  l

def abilities(i: Info) -> +List<U32>:
  I{_, _, _, _, a, _} = i
  a

def team(i: Info) -> +List<U32>:
  I{_, _, _, _, _, t} = i
  t

def with_tera(i: Info, tera_: U32) -> Info:
  I{p, r, _, l, a, t} = i
  I{p, r, tera_, l, a, t}

def set_id(s: GeneratedSet) -> U32:
  Set{id, _, _, _, _, _, _, _, _, _, _, _} = s
  id

def set_ability(s: GeneratedSet) -> U32:
  Set{_, _, _, _, ability, _, _, _, _, _, _, _} = s
  ability

def set_level(s: GeneratedSet) -> U32:
  Set{_, _, _, _, _, _, level, _, _, _, _, _} = s
  level

def set_role(s: GeneratedSet) -> U32:
  Set{_, _, role, _, _, _, _, _, _, _, _, _} = s
  role

def set_moves(s: GeneratedSet) -> +List<U32>:
  Set{_, _, _, _, _, _, _, _, _, moves, _, _} = s
  moves
