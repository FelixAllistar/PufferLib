import Base
import ./Model.bend as M
import ../../miniwob/training/TextGenerate.bend as TG

# Transport layout shared with Wire.bend and family_api.c. Task goals stay in
# private field records; public observations include only current values.
def mix(+x: U32) -> U32:
  +a = (x .^. (x >> 16n) : U32)
  +b = (a * 2146121005 : U32)
  +c = (b .^. (b >> 15n) : U32)
  +d = (c * 2221713035 : U32)
  (d .^. (d >> 16n) : U32)

def next_seed(x: U32) -> U32:
  mix((x + 2654435769 : U32))

def write_units(xs: +List<U32>, +at: U32, a: Array<U32>) -> Array<U32>:
  match xs:
    case Nil{}: a
    case Con{x, rest}: write_units(rest, (at + 1 : U32), a[at] <- x)

def write_string(s: String, at: U32, a: Array<U32>) -> Array<U32>:
  write_units(TG.units(s), at, a)

def put_field(index: U32, role: U32, label: String, +value: String, +goal: String,
    +base: U32, a: Array<U32>) -> Array<U32>:
  +at = (base + 128 + index * 544 : U32)
  +value_units = TG.units(value)
  +goal_units = TG.units(goal)
  +label_units = TG.units(label)
  a = a[at] <- role
  a = a[(at + 1 : U32)] <- U32.from_nat(List.length(&2, U32, value_units))
  a = a[(at + 2 : U32)] <- 0
  a = a[(at + 3 : U32)] <- 0
  a = a[(at + 4 : U32)] <- U32.from_nat(List.length(&2, U32, goal_units))
  a = a[(at + 5 : U32)] <- U32.from_nat(List.length(&2, U32, label_units))
  a = write_units(value_units, (at + 8 : U32), a)
  a = write_units(goal_units, (at + 264 : U32), a)
  write_units(label_units, (at + 520 : U32), a)

def put_static(index: U32, role: U32, action: U32, visible: U32, name: String,
    value: String, +base: U32, a: Array<U32>) -> Array<U32>:
  +at = (base + 2400 + index * 128 : U32)
  +name_units = TG.units(name)
  +value_units = TG.units(value)
  a = a[at] <- role
  a = a[(at + 1 : U32)] <- action
  a = a[(at + 2 : U32)] <- 0
  a = a[(at + 3 : U32)] <- visible
  a = a[(at + 4 : U32)] <- 0
  a = a[(at + 5 : U32)] <- U32.from_nat(List.length(&2, U32, name_units))
  a = a[(at + 6 : U32)] <- U32.from_nat(List.length(&2, U32, value_units))
  a = write_units(name_units, (at + 8 : U32), a)
  write_units(value_units, (at + 72 : U32), a)

def name_at(xs: +List<String>, index: Nat) -> String:
  match xs index:
    case Nil{} _: ""
    case Con{value, rest} 0n: value
    case Con{value, rest} 1n+p: name_at(rest, p)

def table_keys() -> +List<String>:
  ["Color", "First name", "Last name", "Country", "Gender", "Language", "Year of Birth", "Religion"]

def table_values() -> +List<String>:
  ["red", "Alice", "Smith", "Canada", "Female", "English", "1984", "Christianity"]

def table_key(seed: U32, row: U32) -> String:
  +slot = (((mix(seed) % 8 : U32) + row : U32) % 8 : U32)
  name_at(table_keys(), U32.to_nat(slot))

def table_value(seed: U32, row: U32) -> String:
  +slot = (((mix(seed) % 8 : U32) + row : U32) % 8 : U32)
  name_at(table_values(), U32.to_nat(slot))

def paragraph(+seed: U32) -> String:
  "The " ++ TG.name(seed) ++ " walks near the " ++ TG.country(next_seed(seed)) ++ " river and watches birds."

def ordinal(index: U32) -> String:
  name_at(["1st", "2nd", "3rd"], U32.to_nat(index))

def set_header(+task: U32, seed: U32, field_count: U32, static_count: U32,
    popup_mode: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +limit = M.deadline(M.task_kind(task))
  a = a[base] <- 2
  a = a[(base + 1 : U32)] <- task
  a = a[(base + 2 : U32)] <- 0
  a = a[(base + 3 : U32)] <- seed
  a = a[(base + 8 : U32)] <- 0
  a = a[(base + 9 : U32)] <- 0
  a = a[(base + 10 : U32)] <- F32.bits(0.0)
  a = a[(base + 11 : U32)] <- F32.bits(0.0)
  a = a[(base + 12 : U32)] <- limit
  a = a[(base + 32 : U32)] <- field_count
  a = a[(base + 33 : U32)] <- 0
  a = a[(base + 34 : U32)] <- 0
  a = a[(base + 35 : U32)] <- popup_mode
  a = a[(base + 36 : U32)] <- 0
  a = a[(base + 37 : U32)] <- 0
  a[(base + 38 : U32)] <- static_count

def set_query(query: String, +base: U32, a: Array<U32>) -> Array<U32>:
  write_string(query, (base + 4608 : U32), a)

def simple_button(+base: U32, a: Array<U32>) -> Array<U32>:
  put_static(0, 1, 1, 0, "Submit", "", base, a)

def dynamic(+seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +target = TG.password(U32.to_nat((1 + mix(seed) % 6 : U32)), next_seed(seed))
  a = set_header(0, seed, 1, 1, 0, base, a)
  a = put_field(0, 3, "Text", "", target, base, a)
  a = simple_button(base, a)
  set_query("Enter \"" ++ target ++ "\" into the text field and press Submit.", base, a)

def enter_case(+flag: Bool, +seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +source = TG.name(seed)
  +typed = Bool.pick(String, flag, String.to_lower(source), String.to_upper(source))
  +target = Bool.pick(String, flag, String.to_upper(source), String.to_lower(source))
  +case_name = Bool.pick(String, flag, "upper", "lower")
  a = set_header(1, seed, 1, 1, 0, base, a)
  a = put_field(0, 3, "Text", "", target, base, a)
  a = simple_button(base, a)
  set_query("Type \"" ++ typed ++ "\" in all " ++ case_name ++ " case letters in the text input and press Submit.", base, a)

def password(+seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +target = TG.login_password(seed)
  a = set_header(2, seed, 2, 1, 0, base, a)
  a = put_field(0, 3, "Password", "", target, base, a)
  a = put_field(1, 3, "Verify password", "", target, base, a)
  a = simple_button(base, a)
  set_query("Enter the password \"" ++ target ++ "\" into both text fields and press submit.", base, a)

def transform(+seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +target = TG.password(U32.to_nat((3 + mix(seed) % 3 : U32)), next_seed(seed))
  a = set_header(3, seed, 1, 2, 0, base, a)
  a = put_field(0, 3, "Text", "", target, base, a)
  a = put_static(0, 13, 0, 0, "Captcha", target, base, a)
  a = put_static(1, 1, 1, 0, "Submit", "", base, a)
  set_query("Type the text below into the text field and press Submit.", base, a)

def copy_one(+seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +source = paragraph(seed)
  a = set_header(4, seed, 2, 1, 0, base, a)
  a = put_field(0, 16, "Text to copy", source, "", base, a)
  a = put_field(1, 3, "Answer", "", source, base, a)
  a = simple_button(base, a)
  set_query("Copy the text in the textarea below, paste it into the textbox and press Submit.", base, a)

def copy_choice_goal(index: Nat, +p0: String, +p1: String, +p2: String) -> String:
  match index:
    case 0n: p0
    case 1n: p1
    case _: p2

def copy_many(+seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +p0 = paragraph(seed)
  +p1 = paragraph(next_seed(seed))
  +p2 = paragraph(next_seed(next_seed(seed)))
  +choice = (mix(seed) % 3 : U32)
  +goal = copy_choice_goal(U32.to_nat(choice), p0, p1, p2)
  a = set_header(5, seed, 4, 1, 0, base, a)
  a = put_field(0, 16, "1st text area", p0, "", base, a)
  a = put_field(1, 16, "2nd text area", p1, "", base, a)
  a = put_field(2, 16, "3rd text area", p2, "", base, a)
  a = put_field(3, 3, "Answer", "", goal, base, a)
  a = simple_button(base, a)
  set_query("Copy the text from the " ++ ordinal(choice) ++ " text area below and paste it into the text input, then press Submit.", base, a)

def table_static_rows(n: Nat, +seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n: a
    case 1n+ +p:
      +row = U32.from_nat(p)
      +node = (row * 2 : U32)
      +key = table_key(seed, row)
      +value = table_value(seed, row)
      a = put_static(node, 10, 0, 0, key, "", base, a)
      a = put_static((node + 1 : U32), 10, 0, 0, "", value, base, a)
      table_static_rows(p, seed, base, a)

def table_field(+seed: U32, +row: U32, field: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +key = table_key(seed, row)
  +value = table_value(seed, row)
  put_field(field, 3, key ++ ":", "", value, base, a)

def table(+seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +first = (mix(seed) % 5 : U32)
  +second = ((first + 1 + mix(next_seed(seed)) % 4 : U32) % 5 : U32)
  +reverse = U32.is_eq((mix((seed + 37 : U32)) % 2 : U32), 1)
  a = set_header(6, seed, 2, 13, 0, base, a)
  a = table_static_rows(5n, seed, base, a)
  a = table_field(seed, Bool.pick(U32, reverse, second, first), 0, base, a)
  a = table_field(seed, Bool.pick(U32, reverse, first, second), 1, base, a)
  a = put_static(10, 13, 0, 0, "Label 1", table_key(seed, Bool.pick(U32, reverse, second, first)) ++ ":", base, a)
  a = put_static(11, 13, 0, 0, "Label 2", table_key(seed, Bool.pick(U32, reverse, first, second)) ++ ":", base, a)
  a = put_static(12, 1, 1, 0, "Submit", "", base, a)
  set_query("Enter the value that corresponds with each label into the form and submit when done.", base, a)

def popup_mode(seed: U32) -> U32:
  +choice = (mix((seed + 19 : U32)) % 4 : U32)
  Bool.pick(U32, U32.is_eq(choice, 0), 1, Bool.pick(U32, U32.is_eq(choice, 1), 2, 0))

def popup_login(+seed: U32, +base: U32, a: Array<U32>) -> Array<U32>:
  +user = TG.username(seed)
  +secret = TG.login_password(next_seed(seed))
  +mode = popup_mode(seed)
  a = set_header(7, seed, 2, 5, mode, base, a)
  a = put_field(0, 3, "Username", "", user, base, a)
  a = put_field(1, 3, "Password", "", secret, base, a)
  a = put_static(0, 1, 1, 0, "OK", "", base, a)
  a = put_static(1, 13, 0, 1, "Popup", "Your session is about to expire.", base, a)
  a = put_static(2, 13, 0, 1, "Popup prompt", "Exit to home page?", base, a)
  a = put_static(3, 1, 2, 1, "Popup OK", "OK", base, a)
  a = put_static(4, 1, 3, 1, "Popup Cancel", "Cancel", base, a)
  set_query("Enter the username \"" ++ user ++ "\" and the password \"" ++ secret ++ "\" into the text fields and press login.", base, a)

def generate_kind(kind: M.TaskKind, +seed: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  match kind:
    case M.EnterDynamic{}: dynamic(seed, base, a)
    case M.EnterCase{}: enter_case(U32.is_eq((mix(seed) % 2 : U32), 1), seed, base, a)
    case M.EnterPassword{}: password(seed, base, a)
    case M.Transform{}: transform(seed, base, a)
    case M.CopyOne{}: copy_one(seed, base, a)
    case M.CopyThree{}: copy_many(seed, base, a)
    case M.ReadTable{}: table(seed, base, a)
    case M.PopupLogin{}: popup_login(seed, base, a)

def generate(task: U32, +seed: U32, +base: U32,
    a: Array<U32>) -> Array<U32>:
  generate_kind(M.task_kind(task), seed, base, a)
