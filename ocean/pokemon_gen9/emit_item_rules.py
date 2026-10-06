#!/usr/bin/env python3
"""Emit the ordered singles item decisions as ordinary native Bend functions.

This is source authoring, not a runtime interpreter. Each stage retains its
Showdown condition and lazily invokes only the selected continuation.
"""
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent


def norm(name):
    return re.sub("[^a-z0-9]", "", name.lower())


def enum(kind, name):
    if not name:
        return "0"
    return f"{dict(items='I', moves='V', abilities='A', species='P', roles='Role', types='T')[kind]}.{kind}_{norm(name)}()"


def ids(kind, names):
    return "[" + ", ".join(enum(kind, name) for name in names.split("|")) + "]"


def move(name):
    return f"has_move(cx, {enum('moves', name)})"


def moves(names):
    return f"any_move(cx, {ids('moves', names)})"


def ability(name):
    return f"is_ability(cx, {enum('abilities', name)})"


def abilities(names):
    return f"ability_in(cx, {ids('abilities', names)})"


def species(name):
    return f"is_species(cx, {enum('species', name)})"


def role(name):
    return f"is_role(cx, {enum('roles', name)})"


def roles(names):
    return f"role_in(cx, {ids('roles', names)})"


def typ(name):
    return f"has_type(cx, {enum('types', name)})"


def count(key):
    return f"value_count(cx, {key})"


def yes(key):
    return f"U32.is_ne({count(key)}, 0)"


def no(expression):
    return f"Bool.not({expression})"


def stat(key):
    return f"value_stat(cx, {key}n)"


def weak(name):
    return f"weakness(cx, {enum('types', name)})"


def item(name):
    return f"S.Mem.pure(U32, {enum('items', name)})"


def choose(condition, left, right):
    return f"S.Mem.pure(U32, L.pick({condition}, {enum('items', left)}, {enum('items', right)}))"


def chance_choice(condition, left, right):
    return f"S.branch(U32, {condition}, coin({enum('items', left)}, {enum('items', right)}), {item(right)})"


def sample(names):
    return f"R.sample({ids('items', names)})"


def and_(*terms):
    return "(" + " && ".join(terms) + ")"


def or_(*terms):
    return "(" + " || ".join(terms) + ")"


def ge(a, b):
    return f"U32.is_ge({a}, {b})"


def gt(a, b):
    return f"U32.is_gt({a}, {b})"


def le(a, b):
    return f"U32.is_le({a}, {b})"


def lt(a, b):
    return f"U32.is_lt({a}, {b})"


LEAD = "is_lead(cx)"
SPEED = and_(ge(stat(5), 60), le(stat(5), 108))
TOTAL = f"({stat(0)} + {stat(2)} + {stat(4)} : U32)"
HPDEF = f"({stat(0)} + {stat(2)} : U32)"
LIFE = no(moves("flamecharge|nuzzle|rapidspin"))

PRIORITY = [
    (and_(role("Fast Bulky Setup"), abilities("Quark Drive|Protosynthesis")), item("Booster Energy")),
    (species("lokix"), choose(role("Fast Attacker"), "Silver Powder", "Life Orb")),
    ("has_required_items(cx)", "required_item(cx)"),
    (species("pikachu"), item("Light Ball")),
    (role("AV Pivot"), item("Assault Vest")),
    (species("regieleki"), item("Magnet")),
    (and_(typ("Normal"), move("doubleedge"), move("fakeout")), item("Silk Scarf")),
    (or_(species("froslass"), move("populationbomb")), item("Wide Lens")),
    (and_(ability("Hustle"), yes(43)), "chance_continue", "Wide Lens"),
    (species("smeargle"), item("Focus Sash")),
    (or_(move("clangoroussoul"), and_(species("toxtricity"), move("shiftgear"))), item("Throat Spray")),
    (and_("is_base(cx, P.species_magearna())", role("Tera Blast user")), item("Weakness Policy")),
    (moves("dragonenergy|lastrespects|waterspout"), item("Choice Scarf")),
    (or_(ability("Imposter"), and_(species("magnezone"), role("Fast Attacker"))), item("Choice Scarf")),
    (and_(species("rampardos"), role("Fast Attacker")), item("Choice Scarf")),
    (and_(species("palkia"), yes(22)), item("Lustrous Orb")),
    (or_(move("courtchange"), species("luvdisc"), and_(species("terapagos"), no(move("rest")))), item("Heavy-Duty Boots")),
    (or_(abilities("Cheek Pouch|Cud Chew|Harvest|Ripen"), moves("bellydrum|filletaway")), item("Sitrus Berry")),
    (moves("healingwish|switcheroo|trick"),
     "S.Mem.pure(U32, L.pick(" + and_(SPEED, no(role("Wallbreaker")), no(yes(33))) +
     ", I.items_choicescarf(), L.pick(" + gt(count(20), count(21)) + ", I.items_choiceband(), I.items_choicespecs())))"),
    (and_(yes(22), or_(species("latias"), species("latios"))), item("Soul Dew")),
    (species("scyther"), choose(and_(LEAD, no(move("uturn"))), "Eviolite", "Heavy-Duty Boots")),
    (abilities("Poison Heal|Quick Feet"), item("Toxic Orb")),
    ("is_nfe(cx)", item("Eviolite")),
    (and_(or_(ability("Guts"), move("facade")), no(move("sleeptalk"))),
     choose(or_(typ("Fire"), ability("Toxic Boost")), "Toxic Orb", "Flame Orb")),
    (or_(ability("Magic Guard"), and_(ability("Sheer Force"), yes(34))), item("Life Orb")),
    (ability("Anger Shell"), sample("Expert Belt|Lum Berry|Scope Lens|Sitrus Berry")),
    (and_(yes(25), no(ability("Skill Link")), no(species("breloom"))), item("Loaded Dice")),
    (ability("Unburden"), choose(moves("closecombat|leafstorm"), "White Herb", "Sitrus Berry")),
    (and_(move("shellsmash"), no(ability("Weak Armor"))), item("White Herb")),
    (or_(move("meteorbeam"), and_(move("electroshot"), "Bool.not(team_has(cx, 0n))")), item("Power Herb")),
    (and_(move("acrobatics"), no(ability("Protosynthesis"))), item("")),
    (or_(move("auroraveil"), and_(move("lightscreen"), move("reflect"))), item("Light Clay")),
    (ability("Gluttony"), sample("Aguav Berry|Figy Berry|Iapapa Berry|Mago Berry|Wiki Berry")),
    (and_(species("giratina"), move("rest"), no(move("sleeptalk"))), item("Leftovers")),
    (and_(move("rest"), no(move("sleeptalk")), no(abilities("Natural Cure|Shed Skin"))), item("Chesto Berry")),
    (and_(no(species("yanmega")), ge(weak("Rock"), 10)), item("Heavy-Duty Boots")),
]

GENERAL = [
    (and_(no(species("jirachi")), ge(count(20), "move_count(cx)"),
          no(moves("dragontail|fakeout|firstimpression|flamecharge|rapidspin|trailblaze"))),
     chance_choice(and_(no(role("Wallbreaker")), or_(ge(stat(1), 100), abilities("Huge Power|Pure Power")),
                       SPEED, no(ability("Speed Boost")), no(yes(33))), "Choice Scarf", "Choice Band")),
    (or_(ge(count(21), "move_count(cx)"),
         and_(ge(count(21), "(move_count(cx) - 1 : U32)"), moves("flipturn|uturn"))),
     chance_choice(and_(no(role("Wallbreaker")), ge(stat(3), 100), SPEED,
                       no(abilities("Speed Boost|Tinted Lens")), no(move("uturn")), no(yes(33))),
                   "Choice Scarf", "Choice Specs")),
    (and_(yes(41), no(yes(38)), role("Bulky Setup")), item("Weakness Policy")),
    (and_(no(yes(22)), no(roles("Fast Attacker|Wallbreaker|Tera Blast user"))), item("Assault Vest")),
    (species("golem"), choose(yes(41), "Weakness Policy", "Custap Berry")),
    (move("substitute"), item("Leftovers")),
    (and_(move("stickyweb"), LEAD, le(TOTAL, 235)), item("Focus Sash")),
    (ge(weak("Rock"), 9), item("Heavy-Duty Boots")),
    (or_(move("chillyreception"), and_(role("Fast Support"),
          "any_move(cx, L.append(Rules.pivot_moves(), [V.moves_defog(), V.moves_mortalspin(), V.moves_rapidspin()]))",
          no(typ("Flying")), no(ability("Levitate")))), item("Heavy-Duty Boots")),
    (and_(move("dragondance"), role("Bulky Setup")), item("Weakness Policy")),
    (ability("Rough Skin"), item("Rocky Helmet")),
    (and_(ability("Regenerator"), roles("Bulky Support|Bulky Attacker"), ge(HPDEF, 180)),
     "chance_continue", "Rocky Helmet"),
    (and_(no(ability("Regenerator")), no(yes(43)), yes(36), lt(weak("Fighting"), 9), gt(HPDEF, 200)),
     "chance_continue", "Rocky Helmet"),
    (and_(move("outrage"), yes(43)), item("Lum Berry")),
    (and_(move("protect"), no(ability("Speed Boost"))), item("Leftovers")),
    (and_(role("Fast Support"), LEAD, no(yes(36)), no(yes(26)), or_(yes(44), yes(43)), lt(TOTAL, 258)),
     item("Focus Sash")),
    (and_(no(yes(43)), no(ability("Levitate")), ge(weak("Ground"), 10)), item("Air Balloon")),
    (roles("Bulky Attacker|Bulky Support|Bulky Setup"), item("Leftovers")),
    (and_(species("pawmot"), move("nuzzle")), item("Leppa Berry")),
    (roles("Fast Support|Fast Bulky Setup"),
     choose(and_(gt(f"({count(20)} + {count(21)} : U32)", count(22)), LIFE), "Life Orb", "Leftovers")),
    (and_(role("Tera Blast user"), "L.contains(Rules.defensive_tera_blast_users(), species_id(cx))"), item("Leftovers")),
    (and_(LIFE, roles("Fast Attacker|Setup Sweeper|Tera Blast user|Wallbreaker")), item("Life Orb")),
]

HEADER = """# Generated direct port of teams.ts getPriorityItem:1157 / getItem:1347.
# Regenerate with emit_item_rules.py. Singles specialization; original order.
import Base
import ./Mem.bend as S
import ./Lists.bend as L
import ./GenData.bend as D
import ./GenCounter.bend as Counter
import ./GenRng.bend as R
import ./data/items.bend as I
import ./data/abilities.bend as A
import ./data/moves.bend as V
import ./data/species.bend as P
import ./data/types.bend as T
import ./data/roles.bend as Role
import ./data/Rules.bend as Rules

type Context is Data:
  Ctx{info: D.Info, moves: +List<U32>, counts: D.Counter, ability: U32}

def context_info(cx: Context) -> D.Info:
  Ctx{i, _, _, _} = cx
  i
def context_moves(cx: Context) -> +List<U32>:
  Ctx{_, m, _, _} = cx
  m
def context_counts(cx: Context) -> D.Counter:
  Ctx{_, _, c, _} = cx
  c
def context_ability(cx: Context) -> U32:
  Ctx{_, _, _, a} = cx
  a
def species_id(cx: Context) -> U32:
  D.species(D.mon(context_info(cx)))
def is_species(cx: Context, id: U32) -> Bool:
  U32.is_eq(species_id(cx), id)
def is_base(cx: Context, id: U32) -> Bool:
  U32.is_eq(D.base(D.mon(context_info(cx))), id)
def has_move(cx: Context, id: U32) -> Bool:
  L.contains(context_moves(cx), id)
def any_move(cx: Context, ids: +List<U32>) -> Bool:
  U32.is_ne(L.length(L.filter_in(context_moves(cx), ids)), 0)
def is_ability(cx: Context, id: U32) -> Bool:
  U32.is_eq(context_ability(cx), id)
def ability_in(cx: Context, ids: +List<U32>) -> Bool:
  L.contains(ids, context_ability(cx))
def is_role(cx: Context, id: U32) -> Bool:
  U32.is_eq(D.role(context_info(cx)), id)
def role_in(cx: Context, ids: +List<U32>) -> Bool:
  L.contains(ids, D.role(context_info(cx)))
def has_type(cx: Context, id: U32) -> Bool:
  L.contains(D.types(D.mon(context_info(cx))), id)
def value_count(cx: Context, key: U32) -> U32:
  Counter.count(context_counts(cx), key)
def value_stat(cx: Context, key: Nat) -> U32:
  L.at(D.stats(D.mon(context_info(cx))), key)
def weakness(cx: Context, key: U32) -> U32:
  L.at(D.weaknesses(D.mon(context_info(cx))), U32.to_nat(key))
def move_count(cx: Context) -> U32:
  L.length(context_moves(cx))
def is_lead(cx: Context) -> Bool:
  D.lead(context_info(cx))
def is_nfe(cx: Context) -> Bool:
  D.nfe(D.mon(context_info(cx)))
def team_has(cx: Context, key: Nat) -> Bool:
  U32.is_ne(L.at(D.team(context_info(cx)), key), 0)
def has_required_items(cx: Context) -> Bool:
  U32.is_ne(L.length(D.items(D.mon(context_info(cx)))), 0)
def required_item(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, is_base(cx, P.species_arceus()),
    S.Mem.pure(U32, L.at(D.items(D.mon(context_info(cx))), 0n)),
    R.sample(D.items(D.mon(context_info(cx)))))
def coin(left: U32, right: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    draw : Bool <- R.chance(1, 2)
    return L.pick(draw, left, right)

"""


def emit_stages(name, rules, final):
    out = [f"def {name}_{len(rules)}(cx: Context) -> S.Mem<U32>:\n  {final}\n"]
    for i in reversed(range(len(rules))):
        condition, action, *arg = rules[i]
        nxt = f"{name}_{i + 1}(cx)"
        if action == "chance_continue":
            helper = f"{name}_{i}_chance"
            out.append(f"def {helper}(+cx: Context) -> S.Mem<U32>:\n"
                       "  do S.Mem<U32>:\n"
                       "    draw : Bool <- R.chance(1, 2)\n"
                       f"    S.branch(U32, draw, {item(arg[0])}, {nxt})\n")
            action = f"{helper}(cx)"
        out.append(f"def {name}_{i}(+cx: Context) -> S.Mem<U32>:\n"
                   f"  S.branch(U32, {condition}, {action}, {nxt})\n")
    return "\n".join(out)


def main():
    code = HEADER + emit_stages("priority", PRIORITY, "S.Mem.pure(U32, 4294967295)")
    code += emit_stages("general", GENERAL, item("Leftovers"))
    code += """
def choose_priority(found: Bool, priority_item: U32, cx: Context) -> S.Mem<U32>:
  match found:
    case True{}: S.Mem.pure(U32, priority_item)
    case False{}: general_0(cx)

def get(+b: D.Builder, ability: U32) -> S.Mem<U32>:
  do S.Mem<U32>:
    c : D.Counter <- Counter.query(b)
    +cx : Context = Ctx{D.info(b), D.moves(b), c, ability}
    +priority_item : U32 <- priority_0(cx)
    choose_priority(U32.is_ne(priority_item, 4294967295), priority_item, cx)
"""
    (HERE / "GenItem.bend").write_text(code)
    print(f"Wrote GenItem.bend: {len(PRIORITY)} priority and {len(GENERAL)} general ordered decisions")


if __name__ == "__main__":
    main()
