import Base

# Primitive strings are immutable UTF-16 sequences, not effect objects. IDs
# are canonical within a battle; static catalog texts occupy the initial IDs.
# This store does not consume object identities or effect creation order.
type Entry is Data:
  TextEntry{id: U32, hash: U32, units: +List<U32>}

type Heap is Data:
  TextHeap{count: U32, entries: +List<Entry>}

type Interned is Data:
  InternedText{heap: Heap, id: U32}

def empty() -> Heap:
  TextHeap{0,Nil{}}

def same(left: +List<U32>, right: +List<U32>) -> Bool:
  match left right:
    case Nil{} Nil{}: True{}
    case Con{x,xs} Con{y,ys}: U32.is_eq(x,y) && same(xs,ys)
    case _ _: False{}

def valid(units: +List<U32>) -> Bool:
  match units:
    case Nil{}: True{}
    case Con{head,tail}: U32.is_le(head,65535) && valid(tail)

def hash_from(units: +List<U32>, acc: U32) -> U32:
  match units:
    case Nil{}: acc
    case Con{head,tail}: hash_from(tail,(acc * 31 + head : U32))

def hash(units: +List<U32>) -> U32:
  hash_from(units,0)

def find(entries: +List<Entry>, +hashed: U32, +units: +List<U32>) -> Maybe<&2,U32>:
  match entries:
    case Nil{}: None{}
    case Con{TextEntry{id,candidate,stored},tail}:
      Bool.pick(Maybe<&2,U32>,U32.is_eq(candidate,hashed) && same(stored,units),
        Some{id},find(tail,hashed,units))

def read_entries(entries: +List<Entry>, +id: U32) -> Result<&2,&2,U32,+List<U32>>:
  match entries:
    case Nil{}: Fail{3}
    case Con{TextEntry{candidate,_,units},tail}:
      Bool.pick(Result<&2,&2,U32,+List<U32>>,U32.is_eq(candidate,id),Done{units},read_entries(tail,id))

def read(heap: Heap, id: U32) -> Result<&2,&2,U32,+List<U32>>:
  TextHeap{_,entries} = heap
  read_entries(entries,id)

def insert_found(found: Maybe<&2,U32>, heap: Heap, +base: U32, hashed: U32,
  units: +List<U32>) -> Result<&2,&2,U32,Interned>:
  match found:
    case Some{id}: Done{InternedText{heap,id}}
    case None{}:
      TextHeap{+count,entries} = heap
      +id : U32 = (base + count : U32)
      Bool.pick(Result<&2,&2,U32,Interned>,U32.is_eq(base,0) ||
        U32.is_gt(count,(4294967295 - base : U32)),Fail{3},
        Done{InternedText{TextHeap{(count + 1 : U32),Con{TextEntry{id,hashed,units},entries}},id}})

def insert(+heap: Heap, base: U32, +hashed: U32, +units: +List<U32>)
  -> Result<&2,&2,U32,Interned>:
  TextHeap{_,entries} = heap
  insert_found(find(entries,hashed,units),heap,base,hashed,units)

def dynamic_count(heap: Heap) -> U32:
  TextHeap{count,_} = heap
  count
