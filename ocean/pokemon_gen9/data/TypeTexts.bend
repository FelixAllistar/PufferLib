# Generated exact type text IDs from Showdown 9fb3a5b99f1a0bea17f495c5cc1bfe04fdd19c3e
import Base

def normal() -> U32:
  8845

def flying() -> U32:
  13

def rock() -> U32:
  20

def ice() -> U32:
  17

def stellar() -> U32:
  8846

def unknown() -> U32:
  8843

def bird() -> U32:
  8844

def type_text(kind: Nat) -> U32:
  match kind:
    case 1n: 6
    case 2n: 7
    case 3n: 8
    case 4n: 9
    case 5n: 10
    case 6n: 11
    case 7n: 12
    case 8n: 13
    case 9n: 14
    case 10n: 15
    case 11n: 16
    case 12n: 17
    case 13n: 8845
    case 14n: 18
    case 15n: 19
    case 16n: 20
    case 17n: 21
    case 18n: 8846
    case 19n: 22
    case _: 0
