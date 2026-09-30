import Base
import ./Generate.bend as G
import ./PieGenerate.bend as PG
import ./ShapeModel.bend as M

def kind_index(index: Nat) -> U32:
  match index:
    case 0n: 1
    case 1n: 1
    case 2n: 1
    case 3n: 1
    case 4n: 1
    case 5n: 2
    case 6n: 2
    case _: 3

def shape_glyph(index: Nat) -> U32:
  match index:
    case 0n: 201
    case 1n: 202
    case _: 203

def glyph(kind: Nat, +seed: U32, +index: U32) -> U32:
  match kind:
    case 1n:
      PG.nth(PG.alphabet(),
        U32.to_nat((G.mix((seed + index * 37 + 953 : U32)) % 52 : U32)))
    case 2n:
      PG.nth("1234567890",
        U32.to_nat((G.mix((seed + index * 41 + 967 : U32)) % 10 : U32)))
    case _:
      shape_glyph(U32.to_nat((G.mix((seed + index * 43 + 971 : U32)) % 3 : U32)))

def record(+seed: U32, +index: U32, +cells: U32) -> M.Shape:
  +start = (G.mix((seed + 881 : U32)) % cells : U32)
  +position = ((start + index * 5 : U32) % cells : U32)
  +kind = kind_index(U32.to_nat((G.mix((seed + index * 23 + 907 : U32)) % 10 : U32)))
  M.Item{(position % 7 : U32),(position / 7 : U32),
    (G.mix((seed + index * 29 + 919 : U32)) % 7 : U32),
    (G.mix((seed + index * 31 + 929 : U32)) % 2 : U32),
    kind,glyph(U32.to_nat(kind),seed,index)}

def items(n: Nat, +index: U32, +seed: U32, +cells: U32) -> +List<M.Shape>:
  match n:
    case 0n: Nil{}
    case 1n+ +p:
      record(seed,index,cells) <>
        items(p,(index + 1 : U32),seed,cells)

def descriptor(item: M.Shape, +seed: U32) -> M.Desc:
  M.Item{x,y,+color,+size,+kind,+glyph_code}=item
  +s_mode = (G.mix((seed + 977 : U32)) % 2 : U32)
  +c_mode = (G.mix((seed + 983 : U32)) % 2 : U32)
  +t_mode = (G.mix((seed + 991 : U32)) % 3 : U32)
  +show_token = Bool.pick(U32,
    U32.is_eq(s_mode,0) && U32.is_eq(c_mode,0) && U32.is_eq(t_mode,0),
    1,t_mode)
  M.D{Bool.pick(U32,U32.is_eq(s_mode,0),0,(size + 1 : U32)),
    Bool.pick(U32,U32.is_eq(c_mode,0),0,(color + 1 : U32)),
    Bool.pick(U32,U32.is_eq(show_token,0),0,
      Bool.pick(U32,U32.is_eq(show_token,1),kind,glyph_code))}

def color_name(index: Nat) -> String:
  match index:
    case 1n: "red "
    case 2n: "green "
    case 3n: "blue "
    case 4n: "aqua "
    case 5n: "black "
    case 6n: "magenta "
    case 7n: "yellow "
    case _: ""

def size_name(index: Nat) -> String:
  match index:
    case 1n: "small "
    case 2n: "large "
    case _: ""

def token_name(index: Nat, token: U32) -> String:
  match index:
    case 0n: "item"
    case 1n: "letter"
    case 2n: "digit"
    case 3n: "shape"
    case 201n: "circle"
    case 202n: "rectangle"
    case 203n: "triangle"
    case _: SCon{Char.from_u32(token),SNil{}}

def query_phrase(count_mode: Bool, phrase: String) -> String:
  match count_mode:
    case True{}: "How many " ++ phrase ++ "s are there?"
    case False{}: "Click on a " ++ phrase

def query(count_mode: Bool, size: U32, color: U32,
    +token: U32) -> String:
  query_phrase(count_mode,size_name(U32.to_nat(size)) ++
    color_name(U32.to_nat(color)) ++ token_name(U32.to_nat(token),token))

def write_items(xs: +List<M.Shape>, +index: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  match xs:
    case Nil{}: a
    case Con{M.Item{x,y,color,size,kind,glyph_code},rest}:
      +at = (base + 128 + index * 6 : U32)
      a[at] <- x
      a[(at + 1 : U32)] <- y
      a[(at + 2 : U32)] <- color
      a[(at + 3 : U32)] <- size
      a[(at + 4 : U32)] <- kind
      a[(at + 5 : U32)] <- glyph_code
      write_items(rest,(index + 1 : U32),base,a)

def label(+answer: U32,+slot: U32,+index: U32) -> U32:
  Bool.pick(U32,U32.is_eq(index,slot),answer,
    ((answer + 1 + Bool.pick(U32,U32.is_lt(index,slot),index,
      (index - 1 : U32)) : U32) % 10 : U32))

def write_buttons(n: Nat,+answer: U32,+slot: U32,+base: U32,
    a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +index = U32.from_nat(p)
      a[(base + 64 + index : U32)] <- label(answer,slot,index)
      write_buttons(p,answer,slot,base,a)

def finish(+task: U32,+seed: U32,+base: U32,
    +count_mode: Bool,+xs: +List<M.Shape>,+desc: M.Desc,
    a: Array<U32>) -> Array<U32>:
  M.D{+size,+color,+token}=desc
  +count = M.count_matches(xs,desc)
  +slot = (G.mix((seed + 997 : U32)) % 5 : U32)
  a = G.header(task,seed,base,a)
  a[(base + 32 : U32)] <- U32.from_nat(List.length(&2,M.Shape,xs))
  a[(base + 33 : U32)] <- size
  a[(base + 34 : U32)] <- color
  a[(base + 35 : U32)] <- token
  a[(base + 36 : U32)] <- count
  a[(base + 37 : U32)] <- Bool.to_u32(count_mode)
  a = write_items(xs,0,base,a)
  a = write_buttons(5n,count,slot,base,a)
  G.text(query(count_mode,size,color,token),(base + 512 : U32),a)

def generate(+task: U32,+seed: U32,+base: U32,
    a: Array<U32>) -> Array<U32>:
  +count_mode = U32.is_eq(task,5)
  +cells = Bool.pick(U32,count_mode,42,49)
  +count = Bool.pick(U32,count_mode,
    (3 + (G.mix((seed + 853 : U32)) % 7 : U32) : U32),
    (3 + (G.mix((seed + 857 : U32)) % 17 : U32) : U32))
  +chosen = (G.mix((seed + 859 : U32)) % count : U32)
  +unrelated = count_mode &&
    U32.is_lt((G.mix((seed + 863 : U32)) % 10 : U32),3)
  +sample = Bool.pick(M.Shape,unrelated,
    record((seed + 1009 : U32),0,cells),record(seed,chosen,cells))
  desc = descriptor(sample,seed)
  finish(task,seed,base,count_mode,
    items(U32.to_nat(count),0,seed,cells),desc,a)
