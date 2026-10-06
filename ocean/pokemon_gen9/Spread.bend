import Base
import ./Mem.bend as S
import ./Catalog.bend as C
import ./Stats.bend as Stats
import ./EffectStore.bend as Store
import ./EffectMem.bend as Effects
import ./StatTable.bend as Table

def base_reference(previous: U32, fresh: U32, is_transform: Bool) -> U32:
  Bool.pick(U32,is_transform,previous,fresh)

type Health is Data:
  HealthData{hp: U32, maximum: U32, base_maximum: U32}

def health_after(original: Health, +spread_hp: U32) -> Health:
  HealthData{hp,+maximum,base_maximum} = original
  Bool.pick(Health,U32.is_eq(maximum,0),HealthData{spread_hp,spread_hp,spread_hp},
    HealthData{hp,maximum,base_maximum})

def save_health(+at: U32, health: Health) -> S.Mem<Unit>:
  HealthData{hp,maximum,base_maximum} = health
  do S.Mem<Unit>:
    S.put((at + 9 : U32),base_maximum)
    S.put((at + 8 : U32),maximum)
    S.put((at + 7 : U32),hp)

# set.level and nature are retained separately from Pokemon.level. Nature
# fields 104/105 encode plus/minus as hp=0, atk=1, ..., spe=5; zero is neutral.
def nature_kind(+index: U32, plus: U32, minus: U32) -> U32:
  Bool.pick(U32,U32.is_eq(index,plus),1,Bool.pick(U32,U32.is_eq(index,minus),2,0))

def numbers(n: Nat, +at: U32, +display: U32, +index: U32, +level: U32,
  +plus: U32, +minus: U32, +fixed: U32) -> S.Mem<+List<Store.Field>>:
  match n:
    case 0n: S.Mem.pure(+List<Store.Field>,Nil{})
    case 1n+p:
      do S.Mem<+List<Store.Field>>:
        +base : U32 <- C.species_field(display,(4 + index : U32))
        +ev : U32 <- S.get((at + 64 + index : U32))
        +iv : U32 <- S.get((at + 70 + index : U32))
        S.branch(+List<Store.Field>,U32.is_le(base,10000) && U32.is_le(ev,255) && U32.is_le(iv,31),
          (do S.Mem<+List<Store.Field>>:
            value : U32 = Bool.pick(U32,U32.is_eq(index,0),Stats.max_hp(base,iv,ev,level,fixed),
              Stats.stat(base,iv,ev,level,nature_kind(index,plus,minus)))
            rest : +List<Store.Field> <- numbers(p,at,display,(index + 1 : U32),level,plus,minus,fixed)
            return Con{Store.FieldData{Table.key(index),Store.number(value)},rest}),
          S.raise(+List<Store.Field>,3))

def fields(+at: U32, +display: U32) -> S.Mem<+List<Store.Field>>:
  do S.Mem<+List<Store.Field>>:
    +level : U32 <- S.get((at + 106 : U32))
    +plus : U32 <- S.get((at + 104 : U32))
    +minus : U32 <- S.get((at + 105 : U32))
    fixed : U32 <- C.species_field(display,25)
    S.branch(+List<Store.Field>,U32.is_gt(level,0) && U32.is_le(level,9999) &&
      U32.is_le(plus,5) && U32.is_le(minus,5),numbers(6n,at,display,0,level,plus,minus,fixed),
      S.raise(+List<Store.Field>,3))

def synchronize(n: Nat, +at: U32, +reference: U32, +index: U32) -> S.Mem<Unit>:
  match n:
    case 0n: S.Mem.pure(Unit,Unit{})
    case 1n+p:
      do S.Mem<Unit>:
        value : U32 <- Table.read(reference,index)
        S.put((at + 16 + index : U32),value)
        synchronize(p,at,reference,(index + 1 : U32))

# spreadModify allocates even during Transform. Base-table replacement and HP
# initialization follow the source; storedStats is copied into its old object.
def apply(+at: U32, display: U32, is_transform: Bool) -> S.Mem<Unit>:
  do S.Mem<Unit>:
    +stats : +List<Store.Field> <- fields(at,display)
    fresh : U32 <- Effects.raw_object(stats)
    maximum : U32 <- S.get((at + 8 : U32))
    old_hp : U32 <- S.get((at + 7 : U32))
    old_base : U32 <- S.get((at + 9 : U32))
    +hp : U32 <- S.from_result(U32,Table.integer(Store.field_get(stats,Table.key(0))))
    save_health(at,health_after(HealthData{old_hp,maximum,old_base},hp))
    previous : U32 <- S.get((at + 103 : U32))
    S.put((at + 103 : U32),base_reference(previous,fresh,is_transform))
    +stored : U32 <- S.get((at + 102 : U32))
    Table.copy_into(stored,stats)
    synchronize(5n,at,stored,1)
    # Row 16 is the newest spread HP, separate from the retained base table.
    S.put((at + 16 : U32),hp)
    speed : U32 <- Table.read(stored,5)
    S.put((at + 95 : U32),speed)
