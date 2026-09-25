import Base

# Synonym groups from pinned MiniWoB click-checkboxes-soft (MIT upstream).

def group_0(choice: Nat) -> String:
  match choice:
    case 0n: "big"
    case 1n: "large"
    case 2n: "huge"
    case 3n: "enormous"
    case _: "gigantic"

def group_1(choice: Nat) -> String:
  match choice:
    case 0n: "small"
    case 1n: "tiny"
    case 2n: "little"
    case 3n: "mini"
    case _: "petite"

def group_2(choice: Nat) -> String:
  match choice:
    case 0n: "red"
    case 1n: "scarlet"
    case 2n: "crimson"
    case _: "vermillion"

def group_3(choice: Nat) -> String:
  match choice:
    case 0n: "happy"
    case 1n: "cheerful"
    case 2n: "joyful"
    case 3n: "gleeful"
    case _: "delighted"

def group_4(choice: Nat) -> String:
  match choice:
    case 0n: "sad"
    case 1n: "unhappy"
    case 2n: "sorrowful"
    case 3n: "miserable"
    case _: "tragic"

def group_5(choice: Nat) -> String:
  match choice:
    case 0n: "angry"
    case 1n: "mad"
    case 2n: "furious"
    case _: "irritated"

def group_6(choice: Nat) -> String:
  match choice:
    case 0n: "evil"
    case 1n: "wicked"
    case 2n: "immoral"
    case 3n: "sinful"
    case 4n: "corrupt"
    case _: "depraved"

def group_7(choice: Nat) -> String:
  match choice:
    case 0n: "wrong"
    case 1n: "incorrect"
    case 2n: "mistaken"
    case _: "erroneous"

def group_8(choice: Nat) -> String:
  match choice:
    case 0n: "real"
    case 1n: "genuine"
    case _: "actual"

def group_9(choice: Nat) -> String:
  match choice:
    case 0n: "strange"
    case 1n: "odd"
    case 2n: "peculiar"
    case 3n: "unusual"
    case _: "weird"

def group_10(choice: Nat) -> String:
  match choice:
    case 0n: "stop"
    case 1n: "cease"
    case 2n: "halt"
    case 3n: "end"
    case _: "finish"

def group_11(choice: Nat) -> String:
  match choice:
    case 0n: "scared"
    case 1n: "terrified"
    case 2n: "panicked"
    case 3n: "fearful"
    case 4n: "frightened"
    case _: "afraid"

def group_12(choice: Nat) -> String:
  match choice:
    case 0n: "quiet"
    case 1n: "calm"
    case 2n: "peaceful"
    case 3n: "serene"
    case _: "mild"

def group_13(choice: Nat) -> String:
  match choice:
    case 0n: "old"
    case 1n: "aged"
    case _: "archaic"

def group_14(choice: Nat) -> String:
  match choice:
    case 0n: "love"
    case 1n: "like"
    case 2n: "adore"
    case _: "favor"

def group_15(choice: Nat) -> String:
  match choice:
    case 0n: "kill"
    case 1n: "murder"
    case _: "assassinate"

def group_16(choice: Nat) -> String:
  match choice:
    case 0n: "keep"
    case 1n: "retain"
    case 2n: "preserve"
    case 3n: "sustain"
    case _: "maintain"

def group_17(choice: Nat) -> String:
  match choice:
    case 0n: "hide"
    case 1n: "conceal"
    case _: "camouflage"

def group_18(choice: Nat) -> String:
  match choice:
    case 0n: "hate"
    case 1n: "despise"
    case 2n: "loathe"
    case 3n: "detest"
    case _: "dislike"

def group_19(choice: Nat) -> String:
  match choice:
    case 0n: "funny"
    case 1n: "humorous"
    case 2n: "amusing"
    case 3n: "comical"
    case _: "laughable"

def group_20(choice: Nat) -> String:
  match choice:
    case 0n: "fat"
    case 1n: "fleshy"
    case 2n: "plump"
    case _: "chubby"

def group_21(choice: Nat) -> String:
  match choice:
    case 0n: "stupid"
    case 1n: "dumb"
    case 2n: "dull"
    case _: "unwise"

def group_22(choice: Nat) -> String:
  match choice:
    case 0n: "delicious"
    case 1n: "savory"
    case 2n: "delectable"
    case _: "appetizing"

def group_23(choice: Nat) -> String:
  match choice:
    case 0n: "cut"
    case 1n: "slice"
    case 2n: "carve"
    case _: "chop"

def group_24(choice: Nat) -> String:
  match choice:
    case 0n: "brave"
    case 1n: "courageous"
    case _: "fearless"

def group_25(choice: Nat) -> String:
  match choice:
    case 0n: "begin"
    case 1n: "start"
    case 2n: "initiate"
    case _: "launch"

def group_26(choice: Nat) -> String:
  match choice:
    case 0n: "answer"
    case 1n: "reply"
    case _: "response"

def group_27(choice: Nat) -> String:
  match choice:
    case 0n: "television"
    case 1n: "televisions"
    case 2n: "TV"
    case _: "TVs"

def group_28(choice: Nat) -> String:
  match choice:
    case 0n: "house"
    case 1n: "home"
    case 2n: "houses"
    case _: "homes"

def group_29(choice: Nat) -> String:
  match choice:
    case 0n: "fire"
    case 1n: "flame"
    case 2n: "fires"
    case _: "flames"

def group_30(choice: Nat) -> String:
  match choice:
    case 0n: "pig"
    case 1n: "pork"
    case 2n: "swine"
    case _: "pig"

def group_31(choice: Nat) -> String:
  match choice:
    case 0n: "rabbit"
    case 1n: "rabbits"
    case 2n: "bunny"
    case _: "bunnies"

def group_32(choice: Nat) -> String:
  match choice:
    case 0n: "car"
    case 1n: "cars"
    case 2n: "automobile"
    case 3n: "automobiles"
    case 4n: "vehicle"
    case _: "vehicles"

def group_33(choice: Nat) -> String:
  match choice:
    case _: "water"

def select(index: Nat, choice: U32) -> String:
  match index:
    case 0n: group_0(U32.to_nat((choice % 5 : U32)))
    case 1n: group_1(U32.to_nat((choice % 5 : U32)))
    case 2n: group_2(U32.to_nat((choice % 4 : U32)))
    case 3n: group_3(U32.to_nat((choice % 5 : U32)))
    case 4n: group_4(U32.to_nat((choice % 5 : U32)))
    case 5n: group_5(U32.to_nat((choice % 4 : U32)))
    case 6n: group_6(U32.to_nat((choice % 6 : U32)))
    case 7n: group_7(U32.to_nat((choice % 4 : U32)))
    case 8n: group_8(U32.to_nat((choice % 3 : U32)))
    case 9n: group_9(U32.to_nat((choice % 5 : U32)))
    case 10n: group_10(U32.to_nat((choice % 5 : U32)))
    case 11n: group_11(U32.to_nat((choice % 6 : U32)))
    case 12n: group_12(U32.to_nat((choice % 5 : U32)))
    case 13n: group_13(U32.to_nat((choice % 3 : U32)))
    case 14n: group_14(U32.to_nat((choice % 4 : U32)))
    case 15n: group_15(U32.to_nat((choice % 3 : U32)))
    case 16n: group_16(U32.to_nat((choice % 5 : U32)))
    case 17n: group_17(U32.to_nat((choice % 3 : U32)))
    case 18n: group_18(U32.to_nat((choice % 5 : U32)))
    case 19n: group_19(U32.to_nat((choice % 5 : U32)))
    case 20n: group_20(U32.to_nat((choice % 4 : U32)))
    case 21n: group_21(U32.to_nat((choice % 4 : U32)))
    case 22n: group_22(U32.to_nat((choice % 4 : U32)))
    case 23n: group_23(U32.to_nat((choice % 4 : U32)))
    case 24n: group_24(U32.to_nat((choice % 3 : U32)))
    case 25n: group_25(U32.to_nat((choice % 4 : U32)))
    case 26n: group_26(U32.to_nat((choice % 3 : U32)))
    case 27n: group_27(U32.to_nat((choice % 4 : U32)))
    case 28n: group_28(U32.to_nat((choice % 4 : U32)))
    case 29n: group_29(U32.to_nat((choice % 4 : U32)))
    case 30n: group_30(U32.to_nat((choice % 4 : U32)))
    case 31n: group_31(U32.to_nat((choice % 4 : U32)))
    case 32n: group_32(U32.to_nat((choice % 6 : U32)))
    case 33n: group_33(U32.to_nat((choice % 1 : U32)))
    case _: "water"

def count() -> U32:
  34

def word(index: U32, choice: U32) -> String:
  select(U32.to_nat(index), choice)
