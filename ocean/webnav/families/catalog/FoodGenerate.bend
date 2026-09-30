import Base
import ./Generate.bend as G

def item_name(i: Nat) -> String:
  match i:
    case 0n: "Caesar Salad"
    case 1n: "Garlic bread"
    case 2n: "Spinach and goat cheese dip"
    case 3n: "Chicken wings"
    case 4n: "Tofu and Vegetable stone pot"
    case 5n: "Spicy Thai Peanut Chicken"
    case 6n: "Grilled Pork Tenderloin"
    case 7n: "Spaghetti and Meatballs"
    case 8n: "Cheese wheel"
    case 9n: "Chocolate cake"
    case 10n: "Ice cream sundae"
    case _: " Coconut mango tart"

def type_name(i: Nat) -> String:
  match i:
    case 0n: "dairy"
    case 1n: "gluten-free"
    case 2n: "meat"
    case 3n: "peanuts"
    case _: "vegan"

def type_mask(i: Nat) -> U32:
  match i:
    case 0n: 18
    case 1n: 16
    case 2n: 3
    case 3n: 4
    case 4n: 18
    case 5n: 12
    case 6n: 6
    case 7n: 4
    case 8n: 3
    case 9n: 1
    case 10n: 11
    case _: 16

def course(i: Nat) -> U32:
  match i:
    case 0n: 0
    case 1n: 0
    case 2n: 0
    case 3n: 0
    case 4n: 1
    case 5n: 1
    case 6n: 1
    case 7n: 1
    case _: 2

def item(+index: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +at = (base + 64 + index * 128 : U32)
  a = a[(at + 1 : U32)] <- type_mask(U32.to_nat(index))
  a = a[(at + 2 : U32)] <- course(U32.to_nat(index))
  G.text(item_name(U32.to_nat(index)), (at + 8 : U32), a)

def items(n: Nat, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p: items(p, base, item(U32.from_nat(p), base, a))

def second(+seed: U32, +first: U32) -> U32:
  +j = (G.mix((seed + 449 : U32)) % 11 : U32)
  Bool.pick(U32, U32.is_ge(j, first), (j + 1 : U32), j)

def query_index(mode: Nat, +first: U32, +second_item: U32,
    +type_code: U32, required: U32) -> String:
  match mode:
    case 0n:
      "Order one of each item: " ++ item_name(U32.to_nat(first)) ++
      ", " ++ item_name(U32.to_nat(second_item))
    case _:
      "Order " ++ U32.show(required) ++ " items that are " ++
      type_name(U32.to_nat(type_code))

def generate(task: U32, +seed: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  +mode = (G.mix((seed + 1701 : U32)) % 2 : U32)
  +first = (G.mix((seed + 1811 : U32)) % 12 : U32)
  +second_item = second(seed, first)
  +type_code = (G.mix((seed + 1901 : U32)) % 5 : U32)
  +required = (2 + G.mix((seed + 2003 : U32)) % 3 : U32)
  a = G.blocks(64n, base, a)
  a[base] <- 2
  a[(base + 1 : U32)] <- task
  a[(base + 3 : U32)] <- seed
  a[(base + 12 : U32)] <- 20000
  a[(base + 32 : U32)] <- mode
  a[(base + 33 : U32)] <- first
  a[(base + 34 : U32)] <- second_item
  a[(base + 35 : U32)] <- type_code
  a[(base + 36 : U32)] <- required
  a = items(12n, base, a)
  G.text(query_index(U32.to_nat(mode), first, second_item,
    type_code, required), (base + 3000 : U32), a)
