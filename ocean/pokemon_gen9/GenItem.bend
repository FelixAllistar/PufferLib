# Generated direct port of teams.ts getPriorityItem:1157 / getItem:1347.
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

def priority_36(cx: Context) -> S.Mem<U32>:
  S.Mem.pure(U32, 4294967295)

def priority_35(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (Bool.not(is_species(cx, P.species_yanmega())) && U32.is_ge(weakness(cx, T.types_rock()), 10)), S.Mem.pure(U32, I.items_heavydutyboots()), priority_36(cx))

def priority_34(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (has_move(cx, V.moves_rest()) && Bool.not(has_move(cx, V.moves_sleeptalk())) && Bool.not(ability_in(cx, [A.abilities_naturalcure(), A.abilities_shedskin()]))), S.Mem.pure(U32, I.items_chestoberry()), priority_35(cx))

def priority_33(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (is_species(cx, P.species_giratina()) && has_move(cx, V.moves_rest()) && Bool.not(has_move(cx, V.moves_sleeptalk()))), S.Mem.pure(U32, I.items_leftovers()), priority_34(cx))

def priority_32(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, is_ability(cx, A.abilities_gluttony()), R.sample([I.items_aguavberry(), I.items_figyberry(), I.items_iapapaberry(), I.items_magoberry(), I.items_wikiberry()]), priority_33(cx))

def priority_31(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (has_move(cx, V.moves_auroraveil()) || (has_move(cx, V.moves_lightscreen()) && has_move(cx, V.moves_reflect()))), S.Mem.pure(U32, I.items_lightclay()), priority_32(cx))

def priority_30(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (has_move(cx, V.moves_acrobatics()) && Bool.not(is_ability(cx, A.abilities_protosynthesis()))), S.Mem.pure(U32, 0), priority_31(cx))

def priority_29(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (has_move(cx, V.moves_meteorbeam()) || (has_move(cx, V.moves_electroshot()) && Bool.not(team_has(cx, 0n)))), S.Mem.pure(U32, I.items_powerherb()), priority_30(cx))

def priority_28(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (has_move(cx, V.moves_shellsmash()) && Bool.not(is_ability(cx, A.abilities_weakarmor()))), S.Mem.pure(U32, I.items_whiteherb()), priority_29(cx))

def priority_27(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, is_ability(cx, A.abilities_unburden()), S.Mem.pure(U32, L.pick(any_move(cx, [V.moves_closecombat(), V.moves_leafstorm()]), I.items_whiteherb(), I.items_sitrusberry())), priority_28(cx))

def priority_26(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (U32.is_ne(value_count(cx, 25), 0) && Bool.not(is_ability(cx, A.abilities_skilllink())) && Bool.not(is_species(cx, P.species_breloom()))), S.Mem.pure(U32, I.items_loadeddice()), priority_27(cx))

def priority_25(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, is_ability(cx, A.abilities_angershell()), R.sample([I.items_expertbelt(), I.items_lumberry(), I.items_scopelens(), I.items_sitrusberry()]), priority_26(cx))

def priority_24(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (is_ability(cx, A.abilities_magicguard()) || (is_ability(cx, A.abilities_sheerforce()) && U32.is_ne(value_count(cx, 34), 0))), S.Mem.pure(U32, I.items_lifeorb()), priority_25(cx))

def priority_23(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, ((is_ability(cx, A.abilities_guts()) || has_move(cx, V.moves_facade())) && Bool.not(has_move(cx, V.moves_sleeptalk()))), S.Mem.pure(U32, L.pick((has_type(cx, T.types_fire()) || is_ability(cx, A.abilities_toxicboost())), I.items_toxicorb(), I.items_flameorb())), priority_24(cx))

def priority_22(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, is_nfe(cx), S.Mem.pure(U32, I.items_eviolite()), priority_23(cx))

def priority_21(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, ability_in(cx, [A.abilities_poisonheal(), A.abilities_quickfeet()]), S.Mem.pure(U32, I.items_toxicorb()), priority_22(cx))

def priority_20(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, is_species(cx, P.species_scyther()), S.Mem.pure(U32, L.pick((is_lead(cx) && Bool.not(has_move(cx, V.moves_uturn()))), I.items_eviolite(), I.items_heavydutyboots())), priority_21(cx))

def priority_19(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (U32.is_ne(value_count(cx, 22), 0) && (is_species(cx, P.species_latias()) || is_species(cx, P.species_latios()))), S.Mem.pure(U32, I.items_souldew()), priority_20(cx))

def priority_18(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, any_move(cx, [V.moves_healingwish(), V.moves_switcheroo(), V.moves_trick()]), S.Mem.pure(U32, L.pick(((U32.is_ge(value_stat(cx, 5n), 60) && U32.is_le(value_stat(cx, 5n), 108)) && Bool.not(is_role(cx, Role.roles_wallbreaker())) && Bool.not(U32.is_ne(value_count(cx, 33), 0))), I.items_choicescarf(), L.pick(U32.is_gt(value_count(cx, 20), value_count(cx, 21)), I.items_choiceband(), I.items_choicespecs()))), priority_19(cx))

def priority_17(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (ability_in(cx, [A.abilities_cheekpouch(), A.abilities_cudchew(), A.abilities_harvest(), A.abilities_ripen()]) || any_move(cx, [V.moves_bellydrum(), V.moves_filletaway()])), S.Mem.pure(U32, I.items_sitrusberry()), priority_18(cx))

def priority_16(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (has_move(cx, V.moves_courtchange()) || is_species(cx, P.species_luvdisc()) || (is_species(cx, P.species_terapagos()) && Bool.not(has_move(cx, V.moves_rest())))), S.Mem.pure(U32, I.items_heavydutyboots()), priority_17(cx))

def priority_15(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (is_species(cx, P.species_palkia()) && U32.is_ne(value_count(cx, 22), 0)), S.Mem.pure(U32, I.items_lustrousorb()), priority_16(cx))

def priority_14(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (is_species(cx, P.species_rampardos()) && is_role(cx, Role.roles_fastattacker())), S.Mem.pure(U32, I.items_choicescarf()), priority_15(cx))

def priority_13(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (is_ability(cx, A.abilities_imposter()) || (is_species(cx, P.species_magnezone()) && is_role(cx, Role.roles_fastattacker()))), S.Mem.pure(U32, I.items_choicescarf()), priority_14(cx))

def priority_12(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, any_move(cx, [V.moves_dragonenergy(), V.moves_lastrespects(), V.moves_waterspout()]), S.Mem.pure(U32, I.items_choicescarf()), priority_13(cx))

def priority_11(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (is_base(cx, P.species_magearna()) && is_role(cx, Role.roles_terablastuser())), S.Mem.pure(U32, I.items_weaknesspolicy()), priority_12(cx))

def priority_10(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (has_move(cx, V.moves_clangoroussoul()) || (is_species(cx, P.species_toxtricity()) && has_move(cx, V.moves_shiftgear()))), S.Mem.pure(U32, I.items_throatspray()), priority_11(cx))

def priority_9(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, is_species(cx, P.species_smeargle()), S.Mem.pure(U32, I.items_focussash()), priority_10(cx))

def priority_8_chance(+cx: Context) -> S.Mem<U32>:
  do S.Mem<U32>:
    draw : Bool <- R.chance(1, 2)
    S.branch(U32, draw, S.Mem.pure(U32, I.items_widelens()), priority_9(cx))

def priority_8(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (is_ability(cx, A.abilities_hustle()) && U32.is_ne(value_count(cx, 43), 0)), priority_8_chance(cx), priority_9(cx))

def priority_7(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (is_species(cx, P.species_froslass()) || has_move(cx, V.moves_populationbomb())), S.Mem.pure(U32, I.items_widelens()), priority_8(cx))

def priority_6(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (has_type(cx, T.types_normal()) && has_move(cx, V.moves_doubleedge()) && has_move(cx, V.moves_fakeout())), S.Mem.pure(U32, I.items_silkscarf()), priority_7(cx))

def priority_5(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, is_species(cx, P.species_regieleki()), S.Mem.pure(U32, I.items_magnet()), priority_6(cx))

def priority_4(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, is_role(cx, Role.roles_avpivot()), S.Mem.pure(U32, I.items_assaultvest()), priority_5(cx))

def priority_3(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, is_species(cx, P.species_pikachu()), S.Mem.pure(U32, I.items_lightball()), priority_4(cx))

def priority_2(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, has_required_items(cx), required_item(cx), priority_3(cx))

def priority_1(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, is_species(cx, P.species_lokix()), S.Mem.pure(U32, L.pick(is_role(cx, Role.roles_fastattacker()), I.items_silverpowder(), I.items_lifeorb())), priority_2(cx))

def priority_0(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (is_role(cx, Role.roles_fastbulkysetup()) && ability_in(cx, [A.abilities_quarkdrive(), A.abilities_protosynthesis()])), S.Mem.pure(U32, I.items_boosterenergy()), priority_1(cx))
def general_22(cx: Context) -> S.Mem<U32>:
  S.Mem.pure(U32, I.items_leftovers())

def general_21(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (Bool.not(any_move(cx, [V.moves_flamecharge(), V.moves_nuzzle(), V.moves_rapidspin()])) && role_in(cx, [Role.roles_fastattacker(), Role.roles_setupsweeper(), Role.roles_terablastuser(), Role.roles_wallbreaker()])), S.Mem.pure(U32, I.items_lifeorb()), general_22(cx))

def general_20(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (is_role(cx, Role.roles_terablastuser()) && L.contains(Rules.defensive_tera_blast_users(), species_id(cx))), S.Mem.pure(U32, I.items_leftovers()), general_21(cx))

def general_19(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, role_in(cx, [Role.roles_fastsupport(), Role.roles_fastbulkysetup()]), S.Mem.pure(U32, L.pick((U32.is_gt((value_count(cx, 20) + value_count(cx, 21) : U32), value_count(cx, 22)) && Bool.not(any_move(cx, [V.moves_flamecharge(), V.moves_nuzzle(), V.moves_rapidspin()]))), I.items_lifeorb(), I.items_leftovers())), general_20(cx))

def general_18(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (is_species(cx, P.species_pawmot()) && has_move(cx, V.moves_nuzzle())), S.Mem.pure(U32, I.items_leppaberry()), general_19(cx))

def general_17(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, role_in(cx, [Role.roles_bulkyattacker(), Role.roles_bulkysupport(), Role.roles_bulkysetup()]), S.Mem.pure(U32, I.items_leftovers()), general_18(cx))

def general_16(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (Bool.not(U32.is_ne(value_count(cx, 43), 0)) && Bool.not(is_ability(cx, A.abilities_levitate())) && U32.is_ge(weakness(cx, T.types_ground()), 10)), S.Mem.pure(U32, I.items_airballoon()), general_17(cx))

def general_15(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (is_role(cx, Role.roles_fastsupport()) && is_lead(cx) && Bool.not(U32.is_ne(value_count(cx, 36), 0)) && Bool.not(U32.is_ne(value_count(cx, 26), 0)) && (U32.is_ne(value_count(cx, 44), 0) || U32.is_ne(value_count(cx, 43), 0)) && U32.is_lt((value_stat(cx, 0n) + value_stat(cx, 2n) + value_stat(cx, 4n) : U32), 258)), S.Mem.pure(U32, I.items_focussash()), general_16(cx))

def general_14(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (has_move(cx, V.moves_protect()) && Bool.not(is_ability(cx, A.abilities_speedboost()))), S.Mem.pure(U32, I.items_leftovers()), general_15(cx))

def general_13(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (has_move(cx, V.moves_outrage()) && U32.is_ne(value_count(cx, 43), 0)), S.Mem.pure(U32, I.items_lumberry()), general_14(cx))

def general_12_chance(+cx: Context) -> S.Mem<U32>:
  do S.Mem<U32>:
    draw : Bool <- R.chance(1, 2)
    S.branch(U32, draw, S.Mem.pure(U32, I.items_rockyhelmet()), general_13(cx))

def general_12(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (Bool.not(is_ability(cx, A.abilities_regenerator())) && Bool.not(U32.is_ne(value_count(cx, 43), 0)) && U32.is_ne(value_count(cx, 36), 0) && U32.is_lt(weakness(cx, T.types_fighting()), 9) && U32.is_gt((value_stat(cx, 0n) + value_stat(cx, 2n) : U32), 200)), general_12_chance(cx), general_13(cx))

def general_11_chance(+cx: Context) -> S.Mem<U32>:
  do S.Mem<U32>:
    draw : Bool <- R.chance(1, 2)
    S.branch(U32, draw, S.Mem.pure(U32, I.items_rockyhelmet()), general_12(cx))

def general_11(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (is_ability(cx, A.abilities_regenerator()) && role_in(cx, [Role.roles_bulkysupport(), Role.roles_bulkyattacker()]) && U32.is_ge((value_stat(cx, 0n) + value_stat(cx, 2n) : U32), 180)), general_11_chance(cx), general_12(cx))

def general_10(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, is_ability(cx, A.abilities_roughskin()), S.Mem.pure(U32, I.items_rockyhelmet()), general_11(cx))

def general_9(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (has_move(cx, V.moves_dragondance()) && is_role(cx, Role.roles_bulkysetup())), S.Mem.pure(U32, I.items_weaknesspolicy()), general_10(cx))

def general_8(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (has_move(cx, V.moves_chillyreception()) || (is_role(cx, Role.roles_fastsupport()) && any_move(cx, L.append(Rules.pivot_moves(), [V.moves_defog(), V.moves_mortalspin(), V.moves_rapidspin()])) && Bool.not(has_type(cx, T.types_flying())) && Bool.not(is_ability(cx, A.abilities_levitate())))), S.Mem.pure(U32, I.items_heavydutyboots()), general_9(cx))

def general_7(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, U32.is_ge(weakness(cx, T.types_rock()), 9), S.Mem.pure(U32, I.items_heavydutyboots()), general_8(cx))

def general_6(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (has_move(cx, V.moves_stickyweb()) && is_lead(cx) && U32.is_le((value_stat(cx, 0n) + value_stat(cx, 2n) + value_stat(cx, 4n) : U32), 235)), S.Mem.pure(U32, I.items_focussash()), general_7(cx))

def general_5(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, has_move(cx, V.moves_substitute()), S.Mem.pure(U32, I.items_leftovers()), general_6(cx))

def general_4(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, is_species(cx, P.species_golem()), S.Mem.pure(U32, L.pick(U32.is_ne(value_count(cx, 41), 0), I.items_weaknesspolicy(), I.items_custapberry())), general_5(cx))

def general_3(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (Bool.not(U32.is_ne(value_count(cx, 22), 0)) && Bool.not(role_in(cx, [Role.roles_fastattacker(), Role.roles_wallbreaker(), Role.roles_terablastuser()]))), S.Mem.pure(U32, I.items_assaultvest()), general_4(cx))

def general_2(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (U32.is_ne(value_count(cx, 41), 0) && Bool.not(U32.is_ne(value_count(cx, 38), 0)) && is_role(cx, Role.roles_bulkysetup())), S.Mem.pure(U32, I.items_weaknesspolicy()), general_3(cx))

def general_1(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (U32.is_ge(value_count(cx, 21), move_count(cx)) || (U32.is_ge(value_count(cx, 21), (move_count(cx) - 1 : U32)) && any_move(cx, [V.moves_flipturn(), V.moves_uturn()]))), S.branch(U32, (Bool.not(is_role(cx, Role.roles_wallbreaker())) && U32.is_ge(value_stat(cx, 3n), 100) && (U32.is_ge(value_stat(cx, 5n), 60) && U32.is_le(value_stat(cx, 5n), 108)) && Bool.not(ability_in(cx, [A.abilities_speedboost(), A.abilities_tintedlens()])) && Bool.not(has_move(cx, V.moves_uturn())) && Bool.not(U32.is_ne(value_count(cx, 33), 0))), coin(I.items_choicescarf(), I.items_choicespecs()), S.Mem.pure(U32, I.items_choicespecs())), general_2(cx))

def general_0(+cx: Context) -> S.Mem<U32>:
  S.branch(U32, (Bool.not(is_species(cx, P.species_jirachi())) && U32.is_ge(value_count(cx, 20), move_count(cx)) && Bool.not(any_move(cx, [V.moves_dragontail(), V.moves_fakeout(), V.moves_firstimpression(), V.moves_flamecharge(), V.moves_rapidspin(), V.moves_trailblaze()]))), S.branch(U32, (Bool.not(is_role(cx, Role.roles_wallbreaker())) && (U32.is_ge(value_stat(cx, 1n), 100) || ability_in(cx, [A.abilities_hugepower(), A.abilities_purepower()])) && (U32.is_ge(value_stat(cx, 5n), 60) && U32.is_le(value_stat(cx, 5n), 108)) && Bool.not(is_ability(cx, A.abilities_speedboost())) && Bool.not(U32.is_ne(value_count(cx, 33), 0))), coin(I.items_choicescarf(), I.items_choiceband()), S.Mem.pure(U32, I.items_choiceband())), general_1(cx))

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
