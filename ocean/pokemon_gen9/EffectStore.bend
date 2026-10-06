import Base
import ./EventValue.bend as V
import ./Number.bend as N
import ./TextStore.bend as Texts

# Persistent effect objects. References are IDs, so aliases keep referring to
# the same object when its fields change. Other objects/arrays may be named by
# typed Value references; this module does not flatten or deep-copy them.
type Field is Data:
  FieldData{key: U32, value: V.Value}

type ObjectTree is Data:
  EmptyObjectTree{}
  ObjectLeaf{reference: U32, fields: +List<Field>}
  ObjectNode{lo: ObjectTree, hi: ObjectTree}

type Heap is Data:
  HeapData{next_reference: U32, effect_order: U32, objects: ObjectTree, texts: Texts.Heap}

# Stable reserved field keys; other keys are source-property identities.
def id_key() -> U32:
  1
def target_key() -> U32:
  2
def order_key() -> U32:
  3
def counter_key() -> U32:
  4
def ending_key() -> U32:
  16

def number(value: U32) -> V.Value:
  V.VNumber{N.Finite{False{},U32.to_nat(value),1}}

def empty(counter: U32) -> Heap:
  HeapData{1,counter,EmptyObjectTree{},Texts.empty()}

def field_get(fields: +List<Field>, +key: U32) -> V.Value:
  match fields:
    case Nil{}: V.VUndefined{}
    case Con{FieldData{candidate,+value},tail}:
      Bool.pick(V.Value,U32.is_eq(candidate,key),value,field_get(tail,key))

def field_has(fields: +List<Field>, +key: U32) -> Bool:
  match fields:
    case Nil{}: False{}
    case Con{FieldData{candidate,_},tail}: U32.is_eq(candidate,key) || field_has(tail,key)

def field_set(fields: +List<Field>, +key: U32, +value: V.Value) -> +List<Field>:
  match fields:
    case Nil{}: [FieldData{key,value}]
    case Con{+head,+tail}:
      FieldData{candidate,_} = head
      Bool.pick(+List<Field>,U32.is_eq(candidate,key),Con{FieldData{key,value},tail},
        Con{head,field_set(tail,key,value)})

def field_delete(fields: +List<Field>, +key: U32) -> +List<Field>:
  match fields:
    case Nil{}: Nil{}
    case Con{+head,+tail}:
      FieldData{candidate,_} = head
      Bool.pick(+List<Field>,U32.is_eq(candidate,key),tail,Con{head,field_delete(tail,key)})

def overlay(overrides: +List<Field>, fields: +List<Field>) -> +List<Field>:
  match overrides:
    case Nil{}: fields
    case Con{FieldData{key,value},tail}: overlay(tail,field_set(fields,key,value))

def clear_existing(fields: +List<Field>) -> +List<Field>:
  match fields:
    case Nil{}: Nil{}
    case Con{+head,tail}:
      FieldData{+key,_} = head
      +rest : +List<Field> = clear_existing(tail)
      Bool.pick(+List<Field>,U32.is_eq(key,id_key()) || U32.is_eq(key,target_key()),Con{head,rest},
        Bool.pick(+List<Field>,U32.is_eq(key,order_key()),Con{FieldData{key,number(0)},rest},rest))

def clear_fields(fields: +List<Field>) -> +List<Field>:
  clear_existing(field_set(fields,id_key(),V.VText{0}))

# A U32 radix tree has bounded 32-step reads/writes independent of the number
# of allocated objects. Field lists preserve source string-property order.
def tree_get(n: Nat, tree: ObjectTree, +mask: U32, +reference: U32) -> Maybe<&2,+List<Field>>:
  match n tree:
    case 0n ObjectLeaf{candidate,fields}:
      Bool.pick(Maybe<&2,+List<Field>>,U32.is_eq(candidate,reference),Some{fields},None{})
    case 1n+p ObjectNode{lo,hi}:
      tree_get(p,Bool.pick(ObjectTree,U32.is_eq((reference .&. mask : U32),0),lo,hi),
        U32.shr(mask),reference)
    case _ _: None{}

def tree_write(n: Nat, left: Bool, tree: ObjectTree, +mask: U32, +reference: U32, fields: +List<Field>) -> ObjectTree:
  match n left tree:
    case 0n _ _: ObjectLeaf{reference,fields}
    case 1n+p True{} ObjectNode{lo,hi}:
      ObjectNode{tree_write(p,U32.is_eq((reference .&. U32.shr(mask) : U32),0),lo,
        U32.shr(mask),reference,fields),hi}
    case 1n+p False{} ObjectNode{lo,hi}:
      ObjectNode{lo,tree_write(p,U32.is_eq((reference .&. U32.shr(mask) : U32),0),hi,
        U32.shr(mask),reference,fields)}
    case 1n+p True{} _:
      ObjectNode{tree_write(p,U32.is_eq((reference .&. U32.shr(mask) : U32),0),EmptyObjectTree{},
        U32.shr(mask),reference,fields),EmptyObjectTree{}}
    case 1n+p False{} _:
      ObjectNode{EmptyObjectTree{},tree_write(p,U32.is_eq((reference .&. U32.shr(mask) : U32),0),EmptyObjectTree{},
        U32.shr(mask),reference,fields)}

def tree_put(n: Nat, tree: ObjectTree, +mask: U32, +reference: U32, fields: +List<Field>) -> ObjectTree:
  tree_write(n,U32.is_eq((reference .&. mask : U32),0),tree,mask,reference,fields)

def object_fields(heap: Heap, reference: U32) -> Maybe<&2,+List<Field>>:
  HeapData{_,_,tree,_} = heap
  tree_get(32n,tree,2147483648,reference)

def required_fields(found: Maybe<&2,+List<Field>>) -> Result<&2,&2,U32,+List<Field>>:
  match found:
    case None{}: Fail{3}
    case Some{fields}: Done{fields}

def get_fields(heap: Heap, reference: U32) -> Result<&2,&2,U32,+List<Field>>:
  required_fields(object_fields(heap,reference))

def put_fields(heap: Heap, reference: U32, fields: +List<Field>) -> Heap:
  HeapData{next,counter,tree,texts} = heap
  HeapData{next,counter,tree_put(32n,tree,2147483648,reference,fields),texts}

# Import a new raw object without calling initEffectState. This is initial
# diagnostic/checkpoint input, not a replacement for native post-action state.
# Existing identities cannot be overwritten by this operation.
def import_new(found: Maybe<&2,+List<Field>>, heap: Heap, +reference: U32,
  fields: +List<Field>) -> Result<&2,&2,U32,Heap>:
  match found:
    case Some{_}: Fail{3}
    case None{}:
      HeapData{next,counter,tree,texts} = heap
      Bool.pick(Result<&2,&2,U32,Heap>,U32.is_eq(reference,0) || U32.is_eq(reference,4294967295),Fail{3},
        Done{HeapData{U32.max(next,(reference + 1 : U32)),counter,
          tree_put(32n,tree,2147483648,reference,fields),texts}})

def import_object(+heap: Heap, +reference: U32, fields: +List<Field>) -> Result<&2,&2,U32,Heap>:
  import_new(object_fields(heap,reference),heap,reference,fields)

def init_id(+fields: +List<Field>) -> +List<Field>:
  Bool.pick(+List<Field>,V.truthy(field_get(fields,id_key())),fields,field_set(fields,id_key(),V.VText{0}))

def auto_order(+fields: +List<Field>, target_pokemon: Bool, target_active: Bool) -> Bool:
  V.truthy(field_get(fields,id_key())) && V.truthy(field_get(fields,target_key())) &&
    (Bool.not(target_pokemon) || target_active)

type InitialOrder is Data:
  AutomaticOrder{}
  ExplicitOrder{value: V.Value}

type Initialized is Data:
  InitData{fields: +List<Field>, next_order: U32}

type Allocated is Data:
  Allocation{heap: Heap, reference: U32}

def automatic_parts(fields: +List<Field>, +counter: U32, +advance: Bool) -> Result<&2,&2,U32,Initialized>:
  Bool.pick(Result<&2,&2,U32,Initialized>,advance && U32.is_eq(counter,4294967295),Fail{3},
    Done{InitData{field_set(fields,order_key(),number(Bool.pick(U32,advance,counter,0))),
      Bool.pick(U32,advance,(counter + 1 : U32),counter)}})

def explicit_fields(+fields: +List<Field>, +value: V.Value, +counter: U32,
  target_pokemon: Bool, target_active: Bool) -> Result<&2,&2,U32,Initialized>:
  Bool.pick(Result<&2,&2,U32,Initialized>,V.defined(value),
    Done{InitData{field_set(init_id(fields),order_key(),value),counter}},
    automatic_parts(init_id(fields),counter,auto_order(fields,target_pokemon,target_active)))

def initialize_fields(+fields: +List<Field>, order: InitialOrder, counter: U32,
  target_pokemon: Bool, target_active: Bool) -> Result<&2,&2,U32,Initialized>:
  match order:
    case ExplicitOrder{value}: explicit_fields(fields,value,counter,target_pokemon,target_active)
    case AutomaticOrder{}:
      automatic_parts(init_id(fields),counter,auto_order(fields,target_pokemon,target_active))

def allocated(+next: U32, tree: ObjectTree, texts: Texts.Heap, initialized: Initialized) -> Allocated:
  InitData{fields,counter} = initialized
  Allocation{HeapData{(next + 1 : U32),counter,tree_put(32n,tree,2147483648,next,fields),texts},next}

def allocation_result(next: U32, tree: ObjectTree, texts: Texts.Heap, result: Result<&2,&2,U32,Initialized>)
  -> Result<&2,&2,U32,Allocated>:
  match result:
    case Fail{code}: Fail{code}
    case Done{initialized}: Done{allocated(next,tree,texts,initialized)}

def allocate_raw(heap: Heap, fields: +List<Field>) -> Result<&2,&2,U32,Allocated>:
  HeapData{+next,counter,tree,texts} = heap
  Bool.pick(Result<&2,&2,U32,Allocated>,U32.is_eq(next,4294967295),Fail{3},
    Done{allocated(next,tree,texts,InitData{fields,counter})})

def allocation_fields(result: Result<&2,&2,U32,Allocated>) -> Result<&2,&2,U32,+List<Field>>:
  match result:
    case Fail{code}: Fail{code}
    case Done{Allocation{heap,reference}}: get_fields(heap,reference)

def allocation_order(result: Result<&2,&2,U32,Allocated>) -> Result<&2,&2,U32,U32>:
  match result:
    case Fail{code}: Fail{code}
    case Done{Allocation{HeapData{_,counter,_,_},_}}: Done{counter}

def allocate(heap: Heap, fields: +List<Field>, order: InitialOrder,
  target_pokemon: Bool, target_active: Bool) -> Result<&2,&2,U32,Allocated>:
  HeapData{+next,counter,+tree,texts} = heap
  Bool.pick(Result<&2,&2,U32,Allocated>,U32.is_eq(next,4294967295),Fail{3},
    allocation_result(next,tree,texts,initialize_fields(fields,order,counter,target_pokemon,target_active)))

def read_result(key: U32, result: Result<&2,&2,U32,+List<Field>>) -> Result<&2,&2,U32,V.Value>:
  match result:
    case Fail{code}: Fail{code}
    case Done{fields}: Done{field_get(fields,key)}

def read(heap: Heap, reference: U32, key: U32) -> Result<&2,&2,U32,V.Value>:
  read_result(key,get_fields(heap,reference))

def write_result(heap: Heap, reference: U32, key: U32, value: V.Value,
  result: Result<&2,&2,U32,+List<Field>>) -> Result<&2,&2,U32,Heap>:
  match result:
    case Fail{code}: Fail{code}
    case Done{fields}: Done{put_fields(heap,reference,field_set(fields,key,value))}

def write(+heap: Heap, +reference: U32, key: U32, value: V.Value) -> Result<&2,&2,U32,Heap>:
  write_result(heap,reference,key,value,get_fields(heap,reference))

def delete_result(heap: Heap, reference: U32, key: U32, result: Result<&2,&2,U32,+List<Field>>)
  -> Result<&2,&2,U32,Heap>:
  match result:
    case Fail{code}: Fail{code}
    case Done{fields}: Done{put_fields(heap,reference,field_delete(fields,key))}

def delete(+heap: Heap, +reference: U32, key: U32) -> Result<&2,&2,U32,Heap>:
  delete_result(heap,reference,key,get_fields(heap,reference))

def clear_result(heap: Heap, reference: U32, result: Result<&2,&2,U32,+List<Field>>)
  -> Result<&2,&2,U32,Heap>:
  match result:
    case Fail{code}: Fail{code}
    case Done{fields}: Done{put_fields(heap,reference,clear_fields(fields))}

def clear(+heap: Heap, +reference: U32) -> Result<&2,&2,U32,Heap>:
  clear_result(heap,reference,get_fields(heap,reference))

def clone_result(heap: Heap, overrides: +List<Field>, order: InitialOrder, target_pokemon: Bool,
  target_active: Bool, result: Result<&2,&2,U32,+List<Field>>) -> Result<&2,&2,U32,Allocated>:
  match result:
    case Fail{code}: Fail{code}
    case Done{fields}: allocate(heap,overlay(overrides,fields),order,target_pokemon,target_active)

def clone(+heap: Heap, reference: U32, overrides: +List<Field>, order: InitialOrder,
  target_pokemon: Bool, target_active: Bool) -> Result<&2,&2,U32,Allocated>:
  clone_result(heap,overrides,order,target_pokemon,target_active,get_fields(heap,reference))

# Updating primitive string storage preserves every persistent object and the
# effect-order counter. Object updates likewise retain the text snapshot store.
def text_heap(heap: Heap) -> Texts.Heap:
  HeapData{_,_,_,texts} = heap
  texts

def put_text_heap(heap: Heap, texts: Texts.Heap) -> Heap:
  HeapData{next,counter,tree,_} = heap
  HeapData{next,counter,tree,texts}
