import Base
import ./Mem.bend as S
import ./GenData.bend as D
import ./data/Layout.bend as C

def read_list(n: Nat, +at: U32) -> S.Mem<+List<U32>>:
  match n:
    case 0n: S.Mem.pure(+List<U32>, Nil{})
    case 1n+p:
      do S.Mem<+List<U32>>:
        head : U32 <- S.get(at)
        tail : +List<U32> <- read_list(p, (at + 1 : U32))
        return head <> tail

def species(+id: U32) -> S.Mem<D.Species>:
  +at = (C.species_start() + id * C.species_stride() : U32)
  do S.Mem<D.Species>:
    base : U32 <- S.get((at + 1 : U32))
    first : U32 <- S.get((at + 2 : U32))
    second : U32 <- S.get((at + 3 : U32))
    stats : +List<U32> <- read_list(6n, (at + 4 : U32))
    gender : U32 <- S.get((at + 10 : U32))
    nfe : U32 <- S.get((at + 11 : U32))
    required : U32 <- S.get((at + 12 : U32))
    ni : U32 <- S.get((at + 13 : U32))
    pi : U32 <- S.get((at + 14 : U32))
    items : +List<U32> <- read_list(U32.to_nat(ni), pi)
    nf : U32 <- S.get((at + 15 : U32))
    pf : U32 <- S.get((at + 16 : U32))
    forms : +List<U32> <- read_list(U32.to_nat(nf), pf)
    level : U32 <- S.get((at + 17 : U32))
    nt : U32 <- S.get((at + 18 : U32))
    pt : U32 <- S.get((at + 19 : U32))
    templates : +List<U32> <- read_list(U32.to_nat(nt), pt)
    na : U32 <- S.get((at + 20 : U32))
    pa : U32 <- S.get((at + 21 : U32))
    abilities : +List<U32> <- read_list(U32.to_nat(na), pa)
    sample_form : U32 <- S.get((at + 22 : U32))
    weaknesses : +List<U32> <- read_list(20n, (at + 31 : U32))
    return D.P{id, base, first, second, stats, gender, U32.is_ne(nfe, 0), required,
      items, forms, U32.is_ne(sample_form, 0), level, templates, abilities, weaknesses}

def template(+id: U32, p: D.Species, lead: Bool, team: +List<U32>) -> S.Mem<D.Builder>:
  +at = (C.templates_start() + id * C.templates_stride() : U32)
  do S.Mem<D.Builder>:
    role : U32 <- S.get(at)
    nm : U32 <- S.get((at + 1 : U32))
    pm : U32 <- S.get((at + 2 : U32))
    moves : +List<U32> <- read_list(U32.to_nat(nm), pm)
    na : U32 <- S.get((at + 3 : U32))
    pa : U32 <- S.get((at + 4 : U32))
    abilities : +List<U32> <- read_list(U32.to_nat(na), pa)
    return D.B{D.I{p, role, 0, lead, abilities, team}, moves, Nil{}, D.Uncounted{}}

def template_teras(+id: U32) -> S.Mem<+List<U32>>:
  +at = (C.templates_start() + id * C.templates_stride() : U32)
  do S.Mem<+List<U32>>:
    count : U32 <- S.get((at + 5 : U32))
    start : U32 <- S.get((at + 6 : U32))
    read_list(U32.to_nat(count), start)

def move(+id: U32, field: U32) -> S.Mem<U32>:
  S.get((C.moves_start() + id * C.moves_stride() + field : U32))

def species_field(+id: U32, field: U32) -> S.Mem<U32>:
  S.get((C.species_start() + id * C.species_stride() + field : U32))

def effectiveness(attack: U32, defender: U32) -> S.Mem<U32>:
  S.get((C.effectiveness_start() + attack * 20 + defender : U32))

def immunity(attack: U32, defender: U32) -> S.Mem<U32>:
  S.get((C.immunity_start() + attack * 20 + defender : U32))

def condition(+id: U32, field: U32) -> S.Mem<U32>:
  S.get((C.conditions_start() + id * C.conditions_stride() + field : U32))

def ability(+id: U32, field: U32) -> S.Mem<U32>:
  S.get((C.abilities_start() + id * C.abilities_stride() + field : U32))

def item(+id: U32, field: U32) -> S.Mem<U32>:
  S.get((C.items_start() + id * C.items_stride() + field : U32))

def base_pool() -> S.Mem<+List<U32>>:
  read_list(U32.to_nat(C.basePool_count()), C.basePool_start())

def form_pool(+id: U32) -> S.Mem<+List<U32>>:
  +at = (C.formPools_start() + id * C.formPools_stride() : U32)
  do S.Mem<+List<U32>>:
    count : U32 <- S.get(at)
    ptr : U32 <- S.get((at + 1 : U32))
    read_list(U32.to_nat(count), ptr)

def callback_info(callback: U32, field: U32) -> S.Mem<U32>:
  S.get((C.callbackInfo_start() + callback * C.callbackInfo_stride() + field : U32))

def species_text(id: U32, field: U32) -> S.Mem<U32>:
  S.get((C.speciesText_start() + id * 2 + field : U32))

def move_text(id: U32, field: U32) -> S.Mem<U32>:
  S.get((C.movesText_start() + id * 2 + field : U32))

def ability_text(id: U32, field: U32) -> S.Mem<U32>:
  S.get((C.abilitiesText_start() + id * 2 + field : U32))

def item_text(id: U32, field: U32) -> S.Mem<U32>:
  S.get((C.itemsText_start() + id * 2 + field : U32))

def condition_text(id: U32, field: U32) -> S.Mem<U32>:
  S.get((C.conditionsText_start() + id * 2 + field : U32))
