import Base
import ./Mem.bend as S
import ./EffectStore.bend as Store
import ./EffectMem.bend as Effects
import ./EventValue.bend as V
import ./Number.bend as N

# Indexed arrays share the persistent object heap, with Value family 8. Keys
# 0..2^32-2 are ordered own indices; 2^32-1 represents the length property.
# This disjoint array representation admits holes but no named properties.
# These bounds are JavaScript array bounds, not a guessed Pokemon type count.
type Shape is Data:
  ArrayShape{extent: U32, cells: +List<Store.Field>}

def length_key() -> U32:
  4294967295

def values_count(values: +List<V.Value>) -> Nat:
  match values:
    case Nil{}: 0n
    case Con{_,tail}: (1n + values_count(tail) : Nat)

def dense_fields(values: +List<V.Value>, +index: U32) -> +List<Store.Field>:
  match values:
    case Nil{}: [Store.FieldData{length_key(),Store.number(index)}]
    case Con{value,tail}:
      Con{Store.FieldData{index,value},dense_fields(tail,(index + 1 : U32))}

def checked_dense(allowed: Bool, values: +List<V.Value>) -> Result<&2,&2,U32,+List<Store.Field>>:
  match allowed:
    case False{}: Fail{3}
    case True{}: Done{dense_fields(values,0)}

def make(+values: +List<V.Value>) -> S.Mem<U32>:
  do S.Mem<U32>:
    cells : +List<Store.Field> <- S.from_result(+List<Store.Field>,
      checked_dense(Nat.is_le(values_count(values),U32.to_nat(length_key())),values))
    Effects.raw_object(cells)

def extent_value(value: V.Value) -> Result<&2,&2,U32,U32>:
  match value:
    case V.VNumber{N.Finite{False{},+magnitude,1}}:
      Bool.pick(Result<&2,&2,U32,U32>,Nat.is_le(magnitude,U32.to_nat(length_key())),
        Done{U32.from_nat(magnitude)},Fail{3})
    case _: Fail{3}

def last_length(tail: +List<Store.Field>) -> Bool:
  match tail:
    case Nil{}: True{}
    case _: False{}

def valid_cells(cells: +List<Store.Field>, +least: U32, +extent: U32) -> Bool:
  match cells:
    case Nil{}: False{}
    case Con{Store.FieldData{+key,_},+tail}:
      Bool.pick(Bool,U32.is_eq(key,length_key()),last_length(tail),
        U32.is_ge(key,least) && U32.is_lt(key,extent) && valid_cells(tail,(key + 1 : U32),extent))

def shaped(+cells: +List<Store.Field>, result: Result<&2,&2,U32,U32>) -> Result<&2,&2,U32,Shape>:
  match result:
    case Fail{code}: Fail{code}
    case Done{+extent}:
      Bool.pick(Result<&2,&2,U32,Shape>,valid_cells(cells,0,extent),Done{ArrayShape{extent,cells}},Fail{3})

def shape(+cells: +List<Store.Field>) -> Result<&2,&2,U32,Shape>:
  shaped(cells,extent_value(Store.field_get(cells,length_key())))

def read_shape(ref: U32) -> S.Mem<Shape>:
  do S.Mem<Shape>:
    heap : Store.Heap <- S.heap()
    cells : +List<Store.Field> <- S.from_result(+List<Store.Field>,Store.get_fields(heap,ref))
    S.from_result(Shape,shape(cells))

def extent_of(data: Shape) -> U32:
  ArrayShape{extent,_} = data
  extent

def length(ref: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    data : Shape <- read_shape(ref)
    return extent_of(data)

def indexed(data: Shape, +index: U32) -> V.Value:
  ArrayShape{extent,cells} = data
  Bool.pick(V.Value,U32.is_lt(index,extent),Store.field_get(cells,index),V.VUndefined{})

def get(ref: U32, index: U32) -> S.Mem<V.Value>:
  do S.Mem<V.Value>:
    data : Shape <- read_shape(ref)
    return indexed(data,index)

def appended(cells: +List<Store.Field>, +index: U32, +value: V.Value) -> +List<Store.Field>:
  match cells:
    case Nil{}: Nil{}
    case Con{+head,tail}:
      Store.FieldData{key,_} = head
      Bool.pick(+List<Store.Field>,U32.is_eq(key,length_key()),
        [Store.FieldData{index,value},Store.FieldData{length_key(),Store.number((index + 1 : U32))}],
        Con{head,appended(tail,index,value)})

def push_shape(data: Shape, value: V.Value) -> Result<&2,&2,U32,Shape>:
  ArrayShape{+extent,cells} = data
  Bool.pick(Result<&2,&2,U32,Shape>,U32.is_eq(extent,length_key()),Fail{3},
    Done{ArrayShape{(extent + 1 : U32),appended(cells,extent,value)}})

def replace(ref: U32, data: Shape) -> S.Mem<U32>:
  ArrayShape{extent,cells} = data
  do S.Mem<U32>:
    heap : Store.Heap <- S.heap()
    S.set_heap(Store.put_fields(heap,ref,cells))
    return extent

# Index assignment keeps numeric own keys sorted, including when filling a
# hole, and grows length without materializing intervening absent cells.
def assigned_cells(cells: +List<Store.Field>, +index: U32, +value: V.Value) -> +List<Store.Field>:
  match cells:
    case Nil{}: [Store.FieldData{index,value}]
    case Con{+head,+tail}:
      Store.FieldData{+key,_} = head
      Bool.pick(+List<Store.Field>,U32.is_eq(key,index),Con{Store.FieldData{index,value},tail},
        Bool.pick(+List<Store.Field>,U32.is_gt(key,index),Con{Store.FieldData{index,value},Con{head,tail}},
          Con{head,assigned_cells(tail,index,value)}))

def assigned_extent(extent: U32, index: U32) -> U32:
  U32.max(extent,(index + 1 : U32))

def assigned_shape(data: Shape, +index: U32, value: V.Value) -> Result<&2,&2,U32,Shape>:
  ArrayShape{extent,cells} = data
  +next : U32 = assigned_extent(extent,index)
  Bool.pick(Result<&2,&2,U32,Shape>,U32.is_eq(index,length_key()),Fail{3},
    Done{ArrayShape{next,Store.field_set(assigned_cells(cells,index,value),length_key(),Store.number(next))}})

def set_index(+ref: U32, index: U32, +value: V.Value) -> S.Mem<V.Value>:
  do S.Mem<V.Value>:
    data : Shape <- read_shape(ref)
    next : Shape <- S.from_result(Shape,assigned_shape(data,index,value))
    extent : U32 <- replace(ref,next)
    return value

def push(+ref: U32, value: V.Value) -> S.Mem<U32>:
  do S.Mem<U32>:
    data : Shape <- read_shape(ref)
    next : Shape <- S.from_result(Shape,push_shape(data,value))
    replace(ref,next)

def reference(value: V.Value) -> Result<&2,&2,U32,U32>:
  match value:
    case V.VObject{8,ref}: Done{ref}
    case _: Fail{3}

def is_text(value: V.Value, text: U32) -> Bool:
  match value:
    case V.VText{id}: U32.is_eq(id,text)
    case _: False{}

# Array.prototype.filter visits present indices only. Explicit undefined is
# a visited value, unlike a hole. The returned array is dense and fresh even
# when no value was removed; retained object values are shallow aliases.
def filtered_values(cells: +List<Store.Field>, +text: U32) -> +List<V.Value>:
  match cells:
    case Nil{}: Nil{}
    case Con{Store.FieldData{+key,+value},tail}:
      +rest : +List<V.Value> = filtered_values(tail,text)
      Bool.pick(+List<V.Value>,U32.is_eq(key,length_key()) || is_text(value,text),
        rest,Con{value,rest})

def filter_shape(data: Shape, text: U32) -> +List<V.Value>:
  ArrayShape{_,cells} = data
  filtered_values(cells,text)

def filter_text(ref: U32, text: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    data : Shape <- read_shape(ref)
    make(filter_shape(data,text))

# This models arr.concat([value]): the receiver's holes are preserved and
# precisely one value is appended, including if that value is an array ref.
# Generic concat spreadability/prototype properties are outside this model.
def allocate_shape(data: Shape) -> S.Mem<U32>:
  ArrayShape{_,cells} = data
  Effects.raw_object(cells)

def concat_one(ref: U32, value: V.Value) -> S.Mem<U32>:
  do S.Mem<U32>:
    data : Shape <- read_shape(ref)
    result : Shape <- S.from_result(Shape,push_shape(data,value))
    allocate_shape(result)

def clone(ref: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    data : Shape <- read_shape(ref)
    allocate_shape(data)

# Type-name membership uses strict text equality. Reading holes as undefined
# cannot match a string, so scanning present cells gives exactly includes(text).
def cells_include_text(cells: +List<Store.Field>, +text: U32) -> Bool:
  match cells:
    case Nil{}: False{}
    case Con{Store.FieldData{key,value},tail}:
      U32.is_ne(key,length_key()) && is_text(value,text) || cells_include_text(tail,text)

def shape_includes_text(data: Shape, text: U32) -> Bool:
  ArrayShape{_,cells} = data
  cells_include_text(cells,text)

def includes_text(ref: U32, text: U32) -> S.Mem<Bool>:
  do S.Mem<Bool>:
    data : Shape <- read_shape(ref)
    return shape_includes_text(data,text)
