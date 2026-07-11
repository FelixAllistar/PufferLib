#pragma once

#include <algorithm>
#include <array>
#include <initializer_list>
#include <utility>
#include <unordered_map>
#include <vector>

namespace ptcg_scorers {

inline CardRef safe_card_ref(const State& state, AreaType area, int area_index, int player_index) {
    try {
        return state.getCardRef(area, area_index, player_index);
    } catch (...) {
        return CardRef();
    }
}

inline const Card* safe_card(const State& state, CardRef ref) {
    if (ref.isNull()) return nullptr;
    try {
        return &state.getCard(ref);
    } catch (...) {
        return nullptr;
    }
}

inline const Card* safe_option_card(const State& state, const SelectOption& option, int default_player) {
    AreaType area = (AreaType)option.param0;
    int area_index = option.param1;
    int player_index = default_player;
    if (option.type == SelectOptionType::Card
        || option.type == SelectOptionType::ToolCard
        || option.type == SelectOptionType::EnergyCard
        || option.type == SelectOptionType::Energy) {
        player_index = option.param2;
    }
    return safe_card(state, safe_card_ref(state, area, area_index, player_index));
}

inline int safe_energy_count(const State& state, int player_index, CardRef ref) {
    if (ref.isNull()) return 0;
    try {
        const Card& card = state.getCard(ref);
        if (card.getMaster().cardType != CardType::Pokemon) return 0;
        return state.getEnergyCount(player_index, ref);
    } catch (...) {
        return 0;
    }
}

inline int card_id_or_zero(const Card* card) {
    return card == nullptr ? 0 : card->cardId;
}

inline int remaining_hp_or_zero(const State& state, const Card* card) {
    if (card == nullptr) return 0;
    try {
        return state.getHp(*card);
    } catch (...) {
        return 0;
    }
}

inline int safe_card_type(const Card* card) {
    if (card == nullptr) return -1;
    try {
        return (int)card->getMaster().cardType;
    } catch (...) {
        return -1;
    }
}

inline int safe_tool_count(const State& state, const Card* card) {
    if (card == nullptr) return 0;
    try {
        return state.getAttachedToolRef(*card).size();
    } catch (...) {
        return 0;
    }
}

inline int safe_attached_card_count(const State& state, CardRef card_ref, const Card* card, int card_id) {
    if (card == nullptr) return 0;
    int count = 0;
    try {
        std::vector<CardRef> refs;
        state.getEnergyCards(card_ref, refs);
        for (CardRef ref : refs) {
            if (card_id_or_zero(safe_card(state, ref)) == card_id) count++;
        }
    } catch (...) {
    }
    try {
        auto tools = state.getAttachedToolRef(*card);
        for (CardRef ref : tools) {
            if (card_id_or_zero(safe_card(state, ref)) == card_id) count++;
        }
    } catch (...) {
    }
    return count;
}

inline void selected_kiyota_abomasnow(State& state) {
    constexpr int Kyogre = 721;
    constexpr int Snover = 722;
    constexpr int Mega_Abomasnow_ex = 723;
    constexpr int Ultra_Ball = 1121;
    constexpr int Carmine = 1192;
    constexpr int Lillie_Determination = 1227;
    constexpr int Surfing_Beach = 1262;
    constexpr int Basic_Water_Energy = 3;

    int my_index = state.selectPlayer;
    PlayerState& my_state = state.players[my_index];
    PlayerState& op_state = state.players[1 - my_index];
    std::array<int, 2048> field_counts = {};
    std::array<int, 2048> hand_counts = {};
    std::array<int, 2048> discard_counts = {};

    int bench_attacker_index0 = -1;
    int bench_attacker_index1 = -1;
    for (int i = 0; i < my_state.bench.size(); i++) {
        CardRef ref = my_state.bench.at(i);
        const Card* card = safe_card(state, ref);
        int id = card_id_or_zero(card);
        if (id >= 0 && id < (int)field_counts.size()) field_counts[id]++;
        int energy_count = safe_energy_count(state, my_index, ref);
        if (id == Mega_Abomasnow_ex && energy_count >= 2) {
            bench_attacker_index0 = i;
        } else if (id == Kyogre && energy_count >= 1) {
            bench_attacker_index1 = i;
        }
    }

    for (CardRef ref : my_state.hand) {
        int id = card_id_or_zero(safe_card(state, ref));
        if (id >= 0 && id < (int)hand_counts.size()) hand_counts[id]++;
    }
    for (CardRef ref : my_state.trash) {
        int id = card_id_or_zero(safe_card(state, ref));
        if (id >= 0 && id < (int)discard_counts.size()) discard_counts[id]++;
    }

    int op_active_hp = 0;
    for (CardRef ref : op_state.active) {
        op_active_hp = remaining_hp_or_zero(state, safe_card(state, ref));
    }

    bool prefer_ky = op_active_hp <= 20 * discard_counts[Basic_Water_Energy];
    int switch_index = -1;
    for (CardRef ref : my_state.active) {
        const Card* card = safe_card(state, ref);
        int id = card_id_or_zero(card);
        if (id >= 0 && id < (int)field_counts.size()) field_counts[id]++;
        int energy_count = safe_energy_count(state, my_index, ref);
        if (id == Mega_Abomasnow_ex && energy_count >= 2) {
            if (prefer_ky && bench_attacker_index1 >= 0) {
                switch_index = bench_attacker_index1;
            }
        } else if (id == Kyogre && energy_count >= 1) {
            if (!prefer_ky && bench_attacker_index0 >= 0) {
                switch_index = bench_attacker_index0;
            }
        } else if (bench_attacker_index0 >= 0) {
            switch_index = bench_attacker_index0;
        }
    }

    std::vector<std::pair<int, int>> scores;
    scores.reserve(state.options.size());
    for (int option_index = 0; option_index < (int)state.options.size(); option_index++) {
        const SelectOption& option = state.options[option_index];
        int score = 0;
        if (option.type == SelectOptionType::Number) {
            score = option.param0;
        } else if (option.type == SelectOptionType::Yes) {
            score = 1;
        } else if (option.type == SelectOptionType::Card) {
            AreaType area = (AreaType)option.param0;
            CardRef ref = safe_card_ref(state, area, option.param1, option.param2);
            const Card* card = safe_card(state, ref);
            int id = card_id_or_zero(card);
            int energy_count = safe_energy_count(state, option.param2, ref);
            if (card != nullptr) {
                if (state.selectContext == SelectContext::Switch
                    || state.selectContext == SelectContext::ToActive
                    || state.selectContext == SelectContext::SetupActivePokemon) {
                    score += energy_count * 2;
                    if (option.param1 == switch_index) score += 100;
                    if (id == Mega_Abomasnow_ex) {
                        score += 20;
                    } else if (id == Kyogre) {
                        score += 10;
                    }
                } else if (state.selectContext == SelectContext::ToBench
                    || state.selectContext == SelectContext::ToHand) {
                    if (id == Snover) {
                        if (field_counts[id] >= 1) {
                            score += 5;
                        } else if (field_counts[Mega_Abomasnow_ex] >= 1) {
                            score += 15;
                        } else {
                            score += 30;
                        }
                    } else if (id == Mega_Abomasnow_ex) {
                        if (field_counts[Snover] >= 1 && field_counts[id] + hand_counts[id] == 0) {
                            score += 100;
                        } else {
                            score += 10;
                        }
                    } else if (id == Kyogre) {
                        if (field_counts[id] >= 1) {
                            score += 1;
                        } else {
                            score += 20;
                        }
                    }
                } else if (state.selectContext == SelectContext::Discard) {
                    if (id == Basic_Water_Energy) {
                        score += 100;
                    } else if (id == Mega_Abomasnow_ex) {
                        score += 10;
                    } else if (id == Carmine) {
                        if (hand_counts[Lillie_Determination] >= 1) score += 30;
                    } else if (id == Lillie_Determination) {
                        score -= 20;
                    }
                    if (id >= 0 && id < (int)hand_counts.size()) {
                        if (hand_counts[id] >= 2) score += 500;
                        hand_counts[id] -= 1;
                    }
                }
            }
        } else if (option.type == SelectOptionType::Play) {
            const Card* card = safe_card(state, safe_card_ref(state, AreaType::Hand, option.param0, my_index));
            int id = card_id_or_zero(card);
            score = 10000;
            if (id == Ultra_Ball) {
                if (hand_counts[Basic_Water_Energy] >= 3
                    || (my_state.hand.size() >= 4
                        && (field_counts[Mega_Abomasnow_ex] + hand_counts[Mega_Abomasnow_ex] == 0
                            || field_counts[Mega_Abomasnow_ex] + field_counts[Snover] == 0
                            || field_counts[Kyogre] == 0))) {
                    score = 4000;
                } else {
                    score = -1;
                }
            } else if (id == Carmine) {
                if (field_counts[Snover] >= 1 && hand_counts[Mega_Abomasnow_ex] >= 1) {
                    score = -1;
                } else {
                    score = 3000;
                }
            } else if (id == Lillie_Determination) {
                if (field_counts[Snover] >= 1 && field_counts[Mega_Abomasnow_ex] == 0
                    && hand_counts[Mega_Abomasnow_ex] >= 1) {
                    score = -1;
                } else {
                    score = 3100;
                }
            }
        } else if (option.type == SelectOptionType::Attach) {
            AreaType target_area = (AreaType)option.param2;
            CardRef target_ref = safe_card_ref(state, target_area, option.param3, my_index);
            const Card* pokemon = safe_card(state, target_ref);
            int id = card_id_or_zero(pokemon);
            score = 5000;
            int energy_count = safe_energy_count(state, my_index, target_ref);
            if (energy_count == 0 && target_area == AreaType::Bench) score += 1;
            if (id == Snover) {
                score += 1;
                if (energy_count == 1) {
                    score -= 100;
                } else if (energy_count >= 2) {
                    score -= 400;
                }
                if (bench_attacker_index0 >= 0) score -= 300;
            } else if (id == Mega_Abomasnow_ex) {
                score += 10;
                if (energy_count == 1) {
                    score += 30;
                } else if (energy_count >= 2) {
                    score -= 300;
                }
                if (bench_attacker_index0 >= 0) score -= 200;
            } else if (id == Kyogre) {
                score += 5;
                if (energy_count >= 1) score -= 200;
                if (bench_attacker_index1 >= 0) score -= 200;
            }
            if (target_area == AreaType::Active) {
                if (bench_attacker_index0 >= 0 && bench_attacker_index1 >= 0 && energy_count <= 2) {
                    score += 200;
                }
            }
        } else if (option.type == SelectOptionType::Evolve) {
            CardRef target_ref = safe_card_ref(state, (AreaType)option.param2, option.param3, my_index);
            score = 10000 + safe_energy_count(state, my_index, target_ref);
        } else if (option.type == SelectOptionType::Ability) {
            const Card* card = safe_option_card(state, option, my_index);
            if (card_id_or_zero(card) == Surfing_Beach && switch_index >= 0) {
                score = 2000;
            } else {
                score = -1;
            }
        } else if (option.type == SelectOptionType::Retreat) {
            score = switch_index >= 0 ? 1500 : -1;
        } else if (option.type == SelectOptionType::Attack) {
            score = 1000;
            if (option.param0 == 1042) {
                score += discard_counts[Basic_Water_Energy] * 20 - 90;
            } else if (option.param0 == 1046) {
                if (op_active_hp <= 200) {
                    score -= 100;
                } else {
                    score += 100;
                }
            }
        }
        scores.push_back({ score, option_index });
    }

    std::stable_sort(scores.begin(), scores.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });

    state.selected.clear();
    int count = std::min(state.selectMax, (int)scores.size());
    for (int i = 0; i < count; i++) {
        state.selected.push_back(scores[i].second);
    }
}

inline void selected_kiyota_iono(State& state) {
    constexpr int Iono_Voltorb = 265;
    constexpr int Iono_Tadbulb = 268;
    constexpr int Iono_Bellibolt_ex = 269;
    constexpr int Iono_Wattrel = 270;
    constexpr int Iono_Kilowattrel = 271;
    constexpr int Buddy_Buddy_Poffin = 1086;
    constexpr int Night_Stretcher = 1097;
    constexpr int Max_Rod = 1110;
    constexpr int Energy_Retrieval = 1118;
    constexpr int Ultra_Ball = 1121;
    constexpr int Poke_Pad = 1152;
    constexpr int Lillie_Determination = 1227;
    constexpr int Canari = 1233;
    constexpr int Levincia = 1254;
    constexpr int Basic_Lightning_Energy = 4;

    int my_index = state.selectPlayer;
    PlayerState& my_state = state.players[my_index];
    PlayerState& op_state = state.players[1 - my_index];
    int op_prize = op_state.prize.size();

    std::array<int, 2048> field_counts = {};
    std::array<int, 2048> field_hand_counts = {};
    std::array<int, 2048> hand_counts = {};
    std::array<int, 2048> discard_counts = {};
    std::array<int, DECK_SIZE> hand_scores = {};

    bool active_attacker = false;
    bool bench_attacker = false;
    bool can_ability = false;
    int total_energy_count = 0;

    int live_bench_count = 0;
    auto count_field_pokemon = [&](CardRef ref, bool active) {
        (void)active;
        const Card* card = safe_card(state, ref);
        if (card == nullptr) return;
        int id = card_id_or_zero(card);
        if (id >= 0 && id < (int)field_counts.size()) {
            field_counts[id]++;
            field_hand_counts[id]++;
        }
        int energy = safe_energy_count(state, my_index, ref);
        total_energy_count += energy;
        if (id == Iono_Kilowattrel && energy > 0) can_ability = true;
        if (id == Iono_Voltorb && energy >= 2) {
            if (active) active_attacker = true;
            else bench_attacker = true;
        }
    };
    for (CardRef ref : my_state.active) count_field_pokemon(ref, true);
    for (CardRef ref : my_state.bench) {
        if (safe_card(state, ref) != nullptr) live_bench_count++;
        count_field_pokemon(ref, false);
    }

    int field_pokemon1 = field_counts[Iono_Tadbulb] + field_counts[Iono_Bellibolt_ex];
    int field_pokemon2 = field_counts[Iono_Wattrel] + field_counts[Iono_Kilowattrel];
    int bench_capacity = state.benchCapacity(my_index);
    bool no_more_pokemon = live_bench_count >= bench_capacity;
    if (field_counts[Iono_Tadbulb] + field_counts[Iono_Wattrel] >= 1) {
        no_more_pokemon = false;
    }

    int stadium_id = 0;
    for (CardRef ref : state.stadium) {
        stadium_id = card_id_or_zero(safe_card(state, ref));
    }

    int unused_hand_count = 0;
    for (int i = 0; i < my_state.hand.size(); i++) {
        const Card* card = safe_card(state, my_state.hand.at(i));
        int id = card_id_or_zero(card);
        int score = -10000;
        if (id == Iono_Voltorb) {
            score = 100;
        } else if (id == Iono_Bellibolt_ex) {
            if (field_counts[id] <= 1) score = 120;
        } else if (id == Iono_Kilowattrel) {
            if (field_counts[id] <= 1) score = 140;
        } else if (id == Ultra_Ball) {
            if (!no_more_pokemon) score = 10;
        } else if (id == Night_Stretcher) {
            score = 50;
        } else if (id == Energy_Retrieval) {
            score = 20;
        } else if (id == Max_Rod) {
            score = 1000;
        } else if (id == Lillie_Determination) {
            score = 150;
        } else if (id == Canari) {
            score = 160;
        } else if (id == Levincia) {
            if (stadium_id != Levincia) score = 30;
        } else if (id == Basic_Lightning_Energy) {
            score = -10;
        }
        if (id >= 0 && id < (int)hand_counts.size()) {
            score -= hand_counts[id] * 100;
            hand_counts[id]++;
            field_hand_counts[id]++;
        }
        hand_scores[i] = score;
        if (score < 0) unused_hand_count++;
    }

    for (CardRef ref : my_state.trash) {
        int id = card_id_or_zero(safe_card(state, ref));
        if (id >= 0 && id < (int)discard_counts.size()) discard_counts[id]++;
    }

    bool can_attack = false;
    if (state.selectContext == SelectContext::Main) {
        for (const SelectOption& option : state.options) {
            if (option.type == SelectOptionType::Attack) can_attack = true;
        }
    }

    int op_active_hp = 10000;
    if (op_state.active.size() >= 1) {
        const Card* active = safe_card(state, op_state.active.at(0));
        if (active != nullptr) op_active_hp = remaining_hp_or_zero(state, active);
    }

    bool no_draw = my_state.deck.size() <= 5;
    std::array<int, 2048> id_counts = {};
    std::vector<std::pair<int, int>> scores;
    scores.reserve(state.options.size());

    for (int option_index = 0; option_index < (int)state.options.size(); option_index++) {
        const SelectOption& option = state.options[option_index];
        int score = 0;
        if (option.type == SelectOptionType::Number) {
            score = option.param0;
        } else if (option.type == SelectOptionType::Yes) {
            score = 1;
        } else if (option.type == SelectOptionType::Attach
            || state.selectContext == SelectContext::AttachFrom) {
            CardRef target_ref;
            AreaType target_area = AreaType::All;
            if (option.type == SelectOptionType::Attach) {
                target_area = (AreaType)option.param2;
                target_ref = safe_card_ref(state, target_area, option.param3, my_index);
            } else {
                target_area = (AreaType)option.param0;
                target_ref = safe_card_ref(state, target_area, option.param1, option.param2);
            }
            const Card* pokemon = safe_card(state, target_ref);
            int id = card_id_or_zero(pokemon);
            int energy = safe_energy_count(state, my_index, target_ref);
            score = 40000;
            bool active = option.type == SelectOptionType::Attach && target_area == AreaType::Active;
            if (id == Iono_Voltorb) {
                if (energy >= 2) {
                    if (active && !can_attack) score += 3000;
                } else {
                    if (active) score += 5000;
                    else if (bench_attacker || active_attacker) score += 100;
                    else score += 1000;
                }
            } else if (id == Iono_Tadbulb) {
                score += 10 - energy;
            } else if (id == Iono_Bellibolt_ex) {
                if (energy >= 4) {
                    if (active && !can_attack) score += 500;
                } else {
                    if (active) score += 800;
                    else if (bench_attacker || active_attacker) score += 14 - energy;
                    else score += 100;
                }
            } else if (id == Iono_Wattrel) {
                if (energy >= 1 || active) score += 10 - energy;
                else score += 6000;
            } else if (id == Iono_Kilowattrel) {
                if (energy >= 1) score += 11 - energy;
                else score += 8000;
            }
        } else if (option.type == SelectOptionType::Card) {
            CardRef ref = safe_card_ref(state, (AreaType)option.param0, option.param1, option.param2);
            const Card* card = safe_card(state, ref);
            int id = card_id_or_zero(card);
            if (card != nullptr) {
                if (state.selectContext == SelectContext::Switch
                    || state.selectContext == SelectContext::ToActive
                    || state.selectContext == SelectContext::SetupActivePokemon) {
                    int energy = safe_energy_count(state, option.param2, ref);
                    int hp = remaining_hp_or_zero(state, card);
                    score -= hp;
                    score -= energy * 100;
                    if (id == Iono_Voltorb) {
                        if (20 + total_energy_count * 20 >= op_active_hp) score += 100000;
                        else score += 1500;
                        if (energy >= 1) {
                            score += 200;
                            if (energy >= 2) score += 10000;
                        }
                    } else if (id == Iono_Bellibolt_ex) {
                        score += 1000;
                        if (energy >= 4) score += 1000;
                    } else if (id == Iono_Tadbulb) {
                        score += 10;
                    }
                } else if (state.selectContext == SelectContext::ToHand
                    || state.selectContext == SelectContext::ToBench) {
                    if (id == Basic_Lightning_Energy) {
                        score += 1;
                    } else if (id == Iono_Voltorb) {
                        if ((AreaType)option.param0 == AreaType::Trash) score += 100000;
                        if (field_counts[id] == 0) score += 110;
                        else if (field_counts[id] == 1 && op_prize >= 2) score += 5;
                    } else if (id == Iono_Tadbulb) {
                        if (field_pokemon1 == 0) score += 200;
                        else if (field_pokemon1 == 1) {
                            if (op_prize >= 3 || (op_prize >= 2 && field_counts[Iono_Bellibolt_ex] == 0)) {
                                score += 20;
                            }
                        }
                    } else if (id == Iono_Bellibolt_ex) {
                        if (field_hand_counts[id] == 0) {
                            score += 250;
                            if (field_counts[Iono_Tadbulb] > 0) score += 300;
                        } else if (field_hand_counts[id] == 1) {
                            if (op_prize >= 3) {
                                score += 30;
                                if (field_counts[Iono_Tadbulb] > 0) score += 30;
                            }
                        }
                    } else if (id == Iono_Wattrel) {
                        if (field_pokemon2 == 0) score += 320;
                        else if (field_pokemon2 == 1) score += 15;
                    } else if (id == Iono_Kilowattrel) {
                        if (field_hand_counts[id] == 0) {
                            score += 300;
                            if (field_counts[Iono_Wattrel] > 0) score += 250;
                        } else if (field_hand_counts[id] == 1) {
                            score += 25;
                            if (field_counts[Iono_Wattrel] > 0) score += 25;
                        }
                    }
                    if (id != Basic_Lightning_Energy) {
                        if (hand_counts[id] >= 2) score -= 20000;
                        else if (hand_counts[id] >= 1) score -= 2000;
                        if (id_counts[id] == 1) score -= 1000;
                        else if (id_counts[id] >= 2) score -= 10000;
                    }
                    if (id >= 0 && id < (int)id_counts.size()) id_counts[id]++;
                } else if (state.selectContext == SelectContext::Discard) {
                    if ((AreaType)option.param0 == AreaType::Hand && option.param2 == my_index) {
                        score = -hand_scores[option.param1];
                    }
                }
            }
        } else if (option.type == SelectOptionType::Play) {
            const Card* card = safe_card(state, safe_card_ref(state, AreaType::Hand, option.param0, my_index));
            int id = card_id_or_zero(card);
            int type = safe_card_type(card);
            if (type == (int)CardType::Stadium) {
                if (discard_counts[Basic_Lightning_Energy] >= 1 || can_ability) score = 85000;
                else score = -1;
            } else if (type == (int)CardType::Supporter) {
                score = 25000;
                if (id == Lillie_Determination) score += 1000;
                else if (no_draw) score = -1;
                else if (id == Canari) {
                    if (no_more_pokemon) score = -1;
                    else if (field_counts[Iono_Voltorb] > 0
                        && field_counts[Iono_Bellibolt_ex] > 0
                        && field_counts[Iono_Kilowattrel] > 0) {
                        score += 100;
                    } else {
                        score += 2000;
                    }
                }
            } else if (type == (int)CardType::Pokemon) {
                score = 100000;
                if (id == Iono_Voltorb && field_counts[Iono_Voltorb] >= 2) {
                    score = -1;
                } else if (id == Iono_Tadbulb && field_pokemon1 >= 2) {
                    score = -1;
                } else if (id == Iono_Wattrel && field_pokemon2 >= 2) {
                    if (op_prize >= 2 || field_counts[Iono_Voltorb] == 0
                        || field_counts[Iono_Bellibolt_ex] == 0) {
                        score = -1;
                    }
                }
            } else {
                if (id == Night_Stretcher) {
                    if (discard_counts[Iono_Voltorb] > 0
                        || (discard_counts[Iono_Bellibolt_ex] > 0 && field_counts[Iono_Tadbulb] > 0)
                        || (discard_counts[Iono_Kilowattrel] > 0 && field_counts[Iono_Wattrel] > 0)) {
                        score = 75000;
                    } else {
                        score = -1;
                    }
                } else if (id == Energy_Retrieval) {
                    score = 61000;
                } else if (id == Max_Rod) {
                    if (state.turn >= 3 && discard_counts[Basic_Lightning_Energy] >= 2) score = 55000;
                    else score = -1;
                } else if (no_draw) {
                    score = -1;
                } else if (id == Buddy_Buddy_Poffin) {
                    score = 80000;
                } else if (id == Ultra_Ball) {
                    if (no_more_pokemon || state.turn <= 2) {
                        score = -1;
                    } else if (field_hand_counts[Iono_Bellibolt_ex] > 0
                        && field_hand_counts[Iono_Kilowattrel] > 0) {
                        score = unused_hand_count >= 2 ? 45000 : -1;
                    } else {
                        score = unused_hand_count >= 1 ? 62000 : -1;
                    }
                } else if (id == Poke_Pad) {
                    score = 79000;
                }
            }
        } else if (option.type == SelectOptionType::Evolve) {
            score = 110000;
        } else if (option.type == SelectOptionType::Ability) {
            score = -1;
            const Card* card = safe_card(state, safe_card_ref(state, (AreaType)option.param0, option.param1, my_index));
            int id = card_id_or_zero(card);
            if (id == Iono_Bellibolt_ex) score = 50000;
            else if (id == Levincia) score = 8000;
            else if (!no_draw && id == Iono_Kilowattrel) score = 30000;
        } else if (option.type == SelectOptionType::Retreat) {
            if (bench_attacker && !active_attacker) score = 10000;
            else score = -1;
        } else if (option.type == SelectOptionType::Attack) {
            score = option.param0;
        }
        scores.push_back({ score, option_index });
    }

    std::stable_sort(scores.begin(), scores.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });

    state.selected.clear();
    int count = std::min(state.selectMax, (int)scores.size());
    for (int i = 0; i < count; i++) {
        state.selected.push_back(scores[i].second);
    }
}

struct LucarioPlan {
    int attacker = -1;
    int target = -1;
    int attack_index = -1;
    int remain_hp = -1;
    bool energy = false;
};

struct LucarioMemory {
    LucarioPlan plan;
    int pre_turn = 0;
    bool ability_used = false;
};

inline int kiyota_lucario_prize_count(const State& state, CardRef ref, const Card* pokemon) {
    if (pokemon == nullptr) return 0;
    const CardMaster& master = pokemon->getMaster();
    int count = master.pokemonType == PokemonType::MegaEx ? 3
        : master.pokemonType == PokemonType::Ex ? 2 : 1;
    count -= safe_attached_card_count(state, ref, pokemon, 12);
    if (master.lillie) {
        count -= safe_attached_card_count(state, ref, pokemon, 1172);
    }
    return std::max(0, count);
}

inline double kiyota_lucario_pokemon_score(const State& state, CardRef ref, const Card* pokemon, int player_index) {
    if (pokemon == nullptr) return 0;
    const CardMaster& master = pokemon->getMaster();
    int id = pokemon->cardId;
    double score = kiyota_lucario_prize_count(state, ref, pokemon) * 1000;
    score += safe_energy_count(state, player_index, ref) * 150;
    score += safe_tool_count(state, pokemon) * 100;
    if (master.evolutionType == EvolutionType::Stage2) score += 250;
    else if (master.evolutionType == EvolutionType::Stage1) score += 130;
    if (id == 173 || id == 174 || id == 190 || id == 1071) score -= 200;
    if (id == 112 && safe_energy_count(state, player_index, ref) >= 1) score += 300;
    score += remaining_hp_or_zero(state, pokemon);
    return score;
}

inline void selected_kiyota_lucario(State& state) {
    constexpr int Makuhita = 673;
    constexpr int Hariyama = 674;
    constexpr int Lunatone = 675;
    constexpr int Solrock = 676;
    constexpr int Riolu = 677;
    constexpr int Mega_Lucario_ex = 678;
    constexpr int Switch = 1123;
    constexpr int Premium_Power_Pro = 1141;
    constexpr int Hero_Cape = 1159;
    constexpr int Boss_Orders = 1182;
    constexpr int Carmine = 1192;
    constexpr int Lillie_Determination = 1227;
    constexpr int Gravity_Mountain = 1252;
    constexpr int Lumiose_City = 1267;
    constexpr int Basic_Fighting_Energy = 6;

    static std::unordered_map<const State*, LucarioMemory> memories;
    LucarioMemory& memory = memories[&state];
    if (memory.pre_turn != state.turn) {
        memory.pre_turn = state.turn;
        memory.plan = LucarioPlan();
        memory.ability_used = false;
    }
    LucarioPlan& plan = memory.plan;

    int my_index = state.selectPlayer;
    PlayerState& my_state = state.players[my_index];
    PlayerState& op_state = state.players[1 - my_index];
    int my_prize = my_state.prize.size();

    std::array<int, 2048> field_counts = {};
    std::array<int, 2048> hand_counts = {};
    std::array<int, 2048> discard_counts = {};

    bool attacker1 = false;
    bool attacker2 = false;
    auto count_lucario_field = [&](CardRef ref) {
        const Card* card = safe_card(state, ref);
        int id = card_id_or_zero(card);
        if (id >= 0 && id < (int)field_counts.size()) field_counts[id]++;
        int energy = safe_energy_count(state, my_index, ref);
        if ((id == Makuhita || id == Hariyama) && energy >= 3) attacker2 = true;
        else if ((id == Riolu || id == Mega_Lucario_ex) && energy >= 2) attacker1 = true;
    };
    for (CardRef ref : my_state.active) count_lucario_field(ref);
    for (CardRef ref : my_state.bench) count_lucario_field(ref);
    for (CardRef ref : my_state.hand) {
        int id = card_id_or_zero(safe_card(state, ref));
        if (id >= 0 && id < (int)hand_counts.size()) hand_counts[id]++;
    }
    for (CardRef ref : my_state.trash) {
        int id = card_id_or_zero(safe_card(state, ref));
        if (id >= 0 && id < (int)discard_counts.size()) discard_counts[id]++;
    }

    int stadium_id = 0;
    for (CardRef ref : state.stadium) {
        stadium_id = card_id_or_zero(safe_card(state, ref));
    }

    bool can_attack = false;
    if (state.selectContext == SelectContext::Main) {
        bool can_switch = false;
        bool can_op_switch = false;
        bool can_use_mega_brave = false;
        for (const SelectOption& option : state.options) {
            if (option.type == SelectOptionType::Play) {
                const Card* card = safe_card(state, safe_card_ref(state, AreaType::Hand, option.param0, my_index));
                int id = card_id_or_zero(card);
                if (id == Switch) can_switch = true;
                else if (id == Boss_Orders) can_op_switch = true;
            } else if (option.type == SelectOptionType::Evolve) {
                const Card* card = safe_card(state, safe_card_ref(state, AreaType::Hand, option.param1, my_index));
                if (card_id_or_zero(card) == Hariyama) can_op_switch = true;
            } else if (option.type == SelectOptionType::Retreat) {
                can_switch = true;
            } else if (option.type == SelectOptionType::Attack) {
                can_attack = true;
                if (option.param0 == 983) can_use_mega_brave = true;
            }
        }

        std::vector<CardRef> my_cards;
        if (!my_state.active.empty()) my_cards.push_back(my_state.active.at(0));
        for (CardRef ref : my_state.bench) my_cards.push_back(ref);
        std::vector<CardRef> op_cards;
        if (!op_state.active.empty()) op_cards.push_back(op_state.active.at(0));
        for (CardRef ref : op_state.bench) op_cards.push_back(ref);

        if (state.turn >= 2) {
            double best_score = -1;
            for (int i = 0; i < (int)my_cards.size(); i++) {
                if (i != 0 && !can_switch) break;
                const Card* my_pokemon = safe_card(state, my_cards[i]);
                int my_id = card_id_or_zero(my_pokemon);
                for (int a = 0; a < 2; a++) {
                    int energy_required = 0;
                    int base_damage = 0;
                    double base_score = 0;
                    if (my_id == Mega_Lucario_ex) {
                        if (a == 0) {
                            energy_required = 1;
                            base_damage = 130;
                            base_score += 60 * std::min(3, discard_counts[Basic_Fighting_Energy]);
                        } else {
                            energy_required = 2;
                            base_damage = 270;
                        }
                        if (my_prize == 2 || my_prize == 3) base_score -= 500;
                    } else if (a == 1) {
                        break;
                    } else if (my_id == Hariyama) {
                        energy_required = 3;
                        base_damage = 210;
                    } else if (my_id == Makuhita) {
                        bool can_evolve_this = false;
                        for (const SelectOption& option : state.options) {
                            if (option.type == SelectOptionType::Evolve) {
                                int index = option.param3;
                                if ((AreaType)option.param2 == AreaType::Bench) index += 1;
                                if (index == i) {
                                    can_evolve_this = true;
                                    break;
                                }
                            }
                        }
                        if (!can_evolve_this) break;
                        base_score -= 100;
                        energy_required = 3;
                        base_damage = 210;
                    } else if (my_id == Solrock) {
                        if (field_counts[Lunatone] >= 1) {
                            energy_required = 1;
                            base_damage = 70;
                        }
                    }
                    if (base_damage <= 0) continue;

                    bool more_energy = false;
                    int energy_count = safe_energy_count(state, my_index, my_cards[i]);
                    if (a == 1 && i == 0 && energy_count >= 2 && !can_use_mega_brave) break;
                    if (energy_count < energy_required) {
                        if (hand_counts[Basic_Fighting_Energy] >= 1 && !state.energyPlayed) {
                            energy_count += 1;
                            if (energy_count < energy_required) continue;
                            more_energy = true;
                        } else {
                            continue;
                        }
                    }

                    for (int j = 0; j < (int)op_cards.size(); j++) {
                        if (j != 0 && !can_op_switch) break;
                        const Card* op_pokemon = safe_card(state, op_cards[j]);
                        if (op_pokemon == nullptr) continue;
                        int damage = base_damage;
                        const CardMaster& data = op_pokemon->getMaster();
                        if (MatchEnergyType(data.weakness, EnergyType::Fighting)) damage *= 2;
                        else if (MatchEnergyType(data.resistance, EnergyType::Fighting)) damage -= 30;
                        int op_hp = remaining_hp_or_zero(state, op_pokemon);
                        int prize = 0;
                        double score = kiyota_lucario_pokemon_score(state, op_cards[j], op_pokemon, 1 - my_index);
                        if (op_hp <= damage) {
                            prize = kiyota_lucario_prize_count(state, op_cards[j], op_pokemon);
                        } else if (op_hp > 0) {
                            score *= (double)damage / (double)op_hp;
                        }
                        score += base_score;
                        if ((int)op_state.prize.size() <= prize) score = 50000;
                        if (i == 0) score += 220;
                        if (j == 0) score += 300;
                        score += energy_count;
                        if (best_score < score) {
                            best_score = score;
                            plan.attacker = i;
                            plan.target = j;
                            plan.attack_index = a;
                            plan.remain_hp = op_hp - damage;
                            plan.energy = more_energy;
                        }
                    }
                }
            }
        }
    }

    auto energy_score = [&](CardRef ref, const Card* pokemon, bool active) {
        int id = card_id_or_zero(pokemon);
        int energy_count = safe_energy_count(state, my_index, ref);
        int score = 8000;
        if (active) score += 10;
        if (id == Makuhita || id == Hariyama) {
            if (id == Hariyama) score += 1;
            if (energy_count < 3) score += 100;
            if (attacker2) score -= 50;
        } else if (id == Lunatone) {
            score -= 100;
        } else if (id == Solrock) {
            if (energy_count < 1) score += 20;
            else score -= 100;
        } else if (id == Riolu || id == Mega_Lucario_ex) {
            if (id == Mega_Lucario_ex) score += 1;
            if (energy_count < 2) score += 100;
            if (attacker1) score -= 50;
        }
        return score;
    };

    std::vector<std::pair<double, int>> scores;
    scores.reserve(state.options.size());
    for (int option_index = 0; option_index < (int)state.options.size(); option_index++) {
        const SelectOption& option = state.options[option_index];
        double score = 0;
        if (option.type == SelectOptionType::Number) {
            score = option.param0;
        } else if (option.type == SelectOptionType::Yes) {
            score = 1;
        } else if (option.type == SelectOptionType::Card) {
            AreaType area = (AreaType)option.param0;
            CardRef ref = safe_card_ref(state, area, option.param1, option.param2);
            const Card* card = safe_card(state, ref);
            int id = card_id_or_zero(card);
            int energy_count = safe_energy_count(state, option.param2, ref);
            if (card != nullptr) {
                if (state.selectContext == SelectContext::Switch
                    || state.selectContext == SelectContext::ToActive) {
                    if (option.param2 == my_index) {
                        score += energy_count * 2;
                        if (option.param1 == plan.attacker - 1) score += 100;
                        if (id == Mega_Lucario_ex) score += (my_prize == 2 || my_prize == 3) ? 8 : 20;
                        else if (id == Hariyama && energy_count >= 2) score += 15;
                        else if (id == Makuhita && energy_count >= 2) score += 10;
                        else if (id == Solrock) score += 5;
                        else if (id == Riolu) score += 4;
                    } else {
                        if (option.param1 == plan.target - 1) score += 100;
                    }
                } else if (state.selectContext == SelectContext::SetupActivePokemon) {
                    if (id == Solrock) score = state.firstPlayer == my_index ? 2 : 4;
                    else if (id == Riolu) score = 3;
                    else if (id == Makuhita) score = 1;
                } else if (state.selectContext == SelectContext::ToHand) {
                    score = 200 - hand_counts[id] * 100;
                    if (id == Makuhita) {
                        if (field_counts[id] >= 1) score -= 10;
                        else score += 10;
                    } else if (id == Hariyama) {
                        if (field_counts[Makuhita] >= 1) score += 20;
                        else score -= 20;
                    } else if (id == Lunatone) {
                        if (field_counts[id] >= 1) score -= 250;
                        else score += 60;
                    } else if (id == Solrock) {
                        if (field_counts[id] >= 1) score -= 250;
                        else score += 50;
                    } else if (id == Riolu) {
                        if (field_counts[id] + field_counts[Mega_Lucario_ex] >= 2) score -= 150;
                        else if (field_counts[id] + field_counts[Mega_Lucario_ex] >= 1) score -= 3;
                        else score += 40;
                    } else if (id == Mega_Lucario_ex) {
                        if (field_counts[Riolu] >= 1) score += 40;
                        else score -= 15;
                    } else if (id == Basic_Fighting_Energy) {
                        if (!memory.ability_used || !state.energyPlayed) score += 30;
                        else score -= 1;
                    }
                } else if (state.selectContext == SelectContext::AttachFrom) {
                    score = energy_score(ref, card, area == AreaType::Active);
                }
            }
        } else if (option.type == SelectOptionType::Play) {
            const Card* card = safe_card(state, safe_card_ref(state, AreaType::Hand, option.param0, my_index));
            int id = card_id_or_zero(card);
            int type = safe_card_type(card);
            if (type == (int)CardType::Pokemon) {
                score = 20000;
                if (id == Lunatone || id == Solrock) {
                    if (field_counts[id] >= 1) score = -1;
                } else if (id == Riolu) {
                    if (field_counts[id] + field_counts[Mega_Lucario_ex] >= 2) score = -1;
                }
            } else {
                score = 10000;
                if (id == Switch) {
                    score = plan.attacker <= 0 ? -1 : 6000;
                } else if (id == Premium_Power_Pro) {
                    if (state.supporterPlayed && plan.remain_hp <= 0) score = -1;
                    else if (!can_attack) {
                        if (!state.supporterPlayed && hand_counts[Carmine] > 0 && hand_counts[Lillie_Determination] == 0) score = 3050;
                        else score = -1;
                    } else {
                        score = 5000;
                    }
                } else if (id == Boss_Orders) {
                    score = plan.target >= 1 ? 3200 : -1;
                } else if (id == Carmine) {
                    score = 3000;
                } else if (id == Lillie_Determination) {
                    score = 3100;
                } else if (id == Gravity_Mountain) {
                    if (stadium_id == 0) score = -1;
                }
            }
        } else if (option.type == SelectOptionType::Attach) {
            const Card* hand_card = safe_card(state, safe_card_ref(state, AreaType::Hand, option.param1, my_index));
            AreaType target_area = (AreaType)option.param2;
            CardRef target_ref = safe_card_ref(state, target_area, option.param3, my_index);
            const Card* pokemon = safe_card(state, target_ref);
            int hand_id = card_id_or_zero(hand_card);
            int poke_id = card_id_or_zero(pokemon);
            if (hand_id == Hero_Cape) {
                score = 7000;
                if (poke_id == Riolu) score += 100;
                else if (poke_id == Mega_Lucario_ex) score += 200;
            } else {
                score = energy_score(target_ref, pokemon, target_area == AreaType::Active);
                if (target_area == AreaType::Active) {
                    if (plan.attacker == 0 && plan.energy) score += 200;
                } else {
                    if (plan.attacker == 1 + option.param3 && plan.energy) score += 200;
                }
            }
        } else if (option.type == SelectOptionType::Evolve) {
            CardRef target_ref = safe_card_ref(state, (AreaType)option.param2, option.param3, my_index);
            const Card* pokemon = safe_card(state, target_ref);
            score = 9000 + safe_energy_count(state, my_index, target_ref);
            if (card_id_or_zero(pokemon) == Makuhita && plan.target == 0) score = -1;
        } else if (option.type == SelectOptionType::Ability) {
            const Card* card = safe_card(state, safe_card_ref(state, (AreaType)option.param0, option.param1, my_index));
            if (card_id_or_zero(card) == Lumiose_City) score = 1;
            else score = 30000;
        } else if (option.type == SelectOptionType::Retreat) {
            score = plan.attacker >= 1 ? 2000 : -1;
        } else if (option.type == SelectOptionType::Attack) {
            score = 1000;
            if (plan.attack_index == 1) {
                if (option.param0 == 983) score += 100;
            } else if (option.param0 != 983) {
                score += 100;
            }
        }
        scores.push_back({ score, option_index });
    }

    std::stable_sort(scores.begin(), scores.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });

    if (state.selectContext == SelectContext::Main && !scores.empty()) {
        int best_index = scores[0].second;
        const SelectOption& option = state.options[best_index];
        if (option.type == SelectOptionType::Ability) {
            const Card* card = safe_card(state, safe_card_ref(state, (AreaType)option.param0, option.param1, my_index));
            if (card_id_or_zero(card) == Lunatone) memory.ability_used = true;
        }
    }

    state.selected.clear();
    int count = std::min(state.selectMax, (int)scores.size());
    for (int i = 0; i < count; i++) {
        state.selected.push_back(scores[i].second);
    }
}

inline int generic_card_value(const State& state, CardRef ref, int owner_index) {
    const Card* card = safe_card(state, ref);
    if (card == nullptr) return 0;
    const CardMaster& master = card->getMaster();
    int value = 0;
    if (master.cardType == CardType::Pokemon) {
        value = 120 + remaining_hp_or_zero(state, card);
        value += safe_energy_count(state, owner_index, ref) * 80;
        value += (int)master.attacks.size() * 35;
        if (master.evolutionType == EvolutionType::Stage1) value += 80;
        if (master.evolutionType == EvolutionType::Stage2) value += 140;
        if (master.pokemonType == PokemonType::Ex) value += 120;
        if (master.pokemonType == PokemonType::MegaEx) value += 180;
    } else if (master.cardType == CardType::Item) {
        value = 210;
    } else if (master.cardType == CardType::Supporter) {
        value = 240;
    } else if (master.cardType == CardType::Tool) {
        value = 160;
    } else if (master.cardType == CardType::Stadium) {
        value = 130;
    } else if (IsEnergy(master.cardType)) {
        value = 190;
    }
    return value;
}

inline CardRef generic_option_primary_ref(const State& state, const SelectOption& option, int default_player) {
    try {
        switch (option.type) {
            case SelectOptionType::Card:
            case SelectOptionType::ToolCard:
            case SelectOptionType::EnergyCard:
            case SelectOptionType::Energy:
                return safe_card_ref(state, (AreaType)option.param0, option.param1, option.param2);
            case SelectOptionType::Play:
                return state.getPlayCardRef(option, default_player);
            case SelectOptionType::Attach:
                return state.getAttachCardRef(option, default_player);
            case SelectOptionType::Evolve:
                return safe_card_ref(state, AreaType::Hand, option.param1, default_player);
            case SelectOptionType::Ability:
                return state.getAbilityCardRef(option, default_player);
            case SelectOptionType::Discard:
                return safe_card_ref(state, (AreaType)option.param0, option.param1, default_player);
            case SelectOptionType::Attack:
                if (!state.players[default_player].active.empty()) return state.players[default_player].active.at(0);
                return CardRef();
            default:
                return CardRef();
        }
    } catch (...) {
        return CardRef();
    }
}

inline CardRef generic_option_target_ref(const State& state, const SelectOption& option, int default_player) {
    try {
        switch (option.type) {
            case SelectOptionType::Attach:
            case SelectOptionType::Evolve:
                return safe_card_ref(state, (AreaType)option.param2, option.param3, default_player);
            case SelectOptionType::Card:
            case SelectOptionType::ToolCard:
            case SelectOptionType::EnergyCard:
            case SelectOptionType::Energy:
                return safe_card_ref(state, (AreaType)option.param0, option.param1, option.param2);
            default:
                return CardRef();
        }
    } catch (...) {
        return CardRef();
    }
}

inline int generic_discard_score(const State& state, CardRef ref, int owner_index) {
    const Card* card = safe_card(state, ref);
    if (card == nullptr) return 0;
    const CardMaster& master = card->getMaster();
    if (IsEnergy(master.cardType)) return 240;
    if (master.cardType == CardType::Stadium) return 160;
    if (master.cardType == CardType::Item || master.cardType == CardType::Tool) return 120;
    if (master.cardType == CardType::Supporter) return 70;
    if (master.cardType == CardType::Pokemon && safe_energy_count(state, owner_index, ref) == 0) return 30;
    return -100;
}

inline void selected_dashimaki_crustle(State& state) {
    constexpr int Hero_Cape = 1159;
    constexpr int Jumbo_Ice = 1147;
    constexpr int Cook = 1212;
    constexpr int Cheren = 1224;
    constexpr int Battle_Colosseum = 1264;

    int my_index = state.selectPlayer;
    std::vector<std::pair<int, int>> scores;
    scores.reserve(state.options.size());

    for (int option_index = 0; option_index < (int)state.options.size(); option_index++) {
        const SelectOption& option = state.options[option_index];
        int score = 0;

        if (state.selectContext == SelectContext::Main) {
            if (option.type == SelectOptionType::Attach) {
                score = 1000;
                const Card* card = safe_card(state, state.getAttachCardRef(option, my_index));
                if (card_id_or_zero(card) == Hero_Cape) {
                    score = (AreaType)option.param2 == AreaType::Active ? 2100 : 0;
                }
            } else if (option.type == SelectOptionType::Evolve) {
                score = 800;
            } else if (option.type == SelectOptionType::Play) {
                score = 600;
                const Card* card = safe_card(state, safe_card_ref(state, AreaType::Hand, option.param0, my_index));
                int id = card_id_or_zero(card);
                if (id == Jumbo_Ice) {
                    int active_hp = 0;
                    int active_max_hp = 0;
                    int active_energy = 0;
                    if (!state.players[my_index].active.empty()) {
                        CardRef ref = state.players[my_index].active.at(0);
                        const Card* active = safe_card(state, ref);
                        if (active != nullptr) {
                            active_hp = remaining_hp_or_zero(state, active);
                            active_max_hp = state.getMaxHp(*active);
                            active_energy = safe_energy_count(state, my_index, ref);
                        }
                    }
                    score = active_hp < active_max_hp && active_energy >= 3 ? 2000 : 0;
                } else if (id == Cook) {
                    int active_hp = 0;
                    int active_max_hp = 0;
                    if (!state.players[my_index].active.empty()) {
                        const Card* active = safe_card(state, state.players[my_index].active.at(0));
                        if (active != nullptr) {
                            active_hp = remaining_hp_or_zero(state, active);
                            active_max_hp = state.getMaxHp(*active);
                        }
                    }
                    score = active_hp < active_max_hp ? 1500 : 0;
                } else if (id == Cheren) {
                    score = 1400;
                } else if (id == Battle_Colosseum) {
                    score = 1300;
                }
            } else if (option.type == SelectOptionType::Ability) {
                score = 400;
            } else if (option.type == SelectOptionType::Attack) {
                score = 100;
            } else if (option.type == SelectOptionType::Retreat) {
                score = -1;
            }
        } else {
            score = 2000;
            if (option.type == SelectOptionType::Card) {
                CardRef ref = safe_card_ref(state, (AreaType)option.param0, option.param1, option.param2);
                const Card* card = safe_card(state, ref);
                if (card != nullptr) {
                    if (state.selectContext == SelectContext::Evolve
                        || state.selectContext == SelectContext::ToBench) {
                        score += 500;
                    }
                    if (safe_card_type(card) == (int)CardType::Pokemon) {
                        if (option.param2 != my_index) {
                            score += (AreaType)option.param0 == AreaType::Active ? 500 : 100;
                            score += safe_energy_count(state, option.param2, ref) * 50;
                        } else {
                            score += remaining_hp_or_zero(state, card);
                        }
                    }
                }
            } else if (option.type == SelectOptionType::Yes) {
                score += 100;
            } else if (option.type == SelectOptionType::Number) {
                score += option.param0;
            }
        }

        scores.push_back({score, option_index});
    }

    std::stable_sort(scores.begin(), scores.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });

    state.selected.clear();
    int max_count = std::min(state.selectMax, (int)scores.size());
    for (int i = 0; i < max_count; i++) {
        if (scores[i].first >= 0 || (int)state.selected.size() < state.selectMin) {
            state.selected.push_back(scores[i].second);
        }
    }
}

inline void selected_by_scores(State& state, std::vector<std::pair<int, int>>& scores) {
    std::stable_sort(scores.begin(), scores.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });

    state.selected.clear();
    int max_count = std::min(state.selectMax, (int)scores.size());
    int required = std::min(state.selectMin, max_count);
    for (int i = 0; i < max_count; i++) {
        if ((int)state.selected.size() < required || scores[i].first > 0) {
            state.selected.push_back(scores[i].second);
        }
    }
}

inline int safe_max_hp_or_zero(const State& state, const Card* card) {
    if (card == nullptr) return 0;
    try {
        return state.getMaxHp(*card);
    } catch (...) {
        return 0;
    }
}

inline CardRef option_primary_ref(const State& state, const SelectOption& option, int my_index) {
    if (option.type == SelectOptionType::Play) {
        return safe_card_ref(state, AreaType::Hand, option.param0, my_index);
    }
    if (option.type == SelectOptionType::Card
        || option.type == SelectOptionType::ToolCard
        || option.type == SelectOptionType::EnergyCard
        || option.type == SelectOptionType::Energy) {
        return safe_card_ref(state, (AreaType)option.param0, option.param1, option.param2);
    }
    return CardRef();
}

inline CardRef option_target_ref(const State& state, const SelectOption& option, int my_index) {
    if (option.type == SelectOptionType::Attach || option.type == SelectOptionType::Evolve) {
        return safe_card_ref(state, (AreaType)option.param2, option.param3, my_index);
    }
    if (option.type == SelectOptionType::Card
        || option.type == SelectOptionType::ToolCard
        || option.type == SelectOptionType::EnergyCard
        || option.type == SelectOptionType::Energy) {
        return safe_card_ref(state, (AreaType)option.param0, option.param1, option.param2);
    }
    return CardRef();
}

template <typename RefList>
inline int count_id_in_zone(const State& state, const RefList& refs, int card_id) {
    int count = 0;
    for (CardRef ref : refs) {
        if (card_id_or_zero(safe_card(state, ref)) == card_id) count++;
    }
    return count;
}

inline int count_id_in_play(const State& state, int player_index, int card_id) {
    int count = 0;
    const PlayerState& player = state.players[player_index];
    count += count_id_in_zone(state, player.active, card_id);
    count += count_id_in_zone(state, player.bench, card_id);
    return count;
}

inline bool id_in_list(int id, std::initializer_list<int> ids) {
    for (int candidate : ids) {
        if (id == candidate) return true;
    }
    return false;
}

inline int target_pressure_score(const State& state, const Card* card, CardRef ref, int player_index) {
    if (card == nullptr) return 0;
    int hp = remaining_hp_or_zero(state, card);
    int energy = safe_energy_count(state, player_index, ref);
    int id = card_id_or_zero(card);
    int score = 900 - hp / 2 + energy * 90;
    if (id_in_list(id, {119, 120, 169, 190, 235, 344, 345, 677, 741, 742})) score += 350;
    if (hp <= 80) score += 250;
    return score;
}

inline void selected_dragapult_exact(State& state) {
    constexpr int Dreepy = 119;
    constexpr int Drakloak = 120;
    constexpr int Dragapult_ex = 121;
    constexpr int Fezandipiti_ex = 140;
    constexpr int Budew = 235;
    constexpr int Meowth_ex = 1071;
    constexpr int Rare_Candy = 1079;
    constexpr int Unfair_Stamp = 1080;
    constexpr int Buddy_Buddy_Poffin = 1086;
    constexpr int Night_Stretcher = 1097;
    constexpr int Crushing_Hammer = 1120;
    constexpr int Ultra_Ball = 1121;
    constexpr int Poke_Pad = 1152;
    constexpr int Boss_Orders = 1182;
    constexpr int Crispin = 1198;
    constexpr int Lillie_Determination = 1227;
    constexpr int Team_Rocket_Watchtower = 1256;
    constexpr int Basic_Fire_Energy = 2;
    constexpr int Basic_Psychic_Energy = 5;

    int my_index = state.selectPlayer;
    int op_index = 1 - my_index;
    const PlayerState& my_state = state.players[my_index];
    int dreepy_play = count_id_in_play(state, my_index, Dreepy);
    int drakloak_play = count_id_in_play(state, my_index, Drakloak);
    int dragapult_play = count_id_in_play(state, my_index, Dragapult_ex);
    int budew_play = count_id_in_play(state, my_index, Budew);

    std::vector<std::pair<int, int>> scores;
    scores.reserve(state.options.size());
    for (int option_index = 0; option_index < (int)state.options.size(); option_index++) {
        const SelectOption& option = state.options[option_index];
        int score = 0;

        if (option.type == SelectOptionType::Play) {
            const Card* card = safe_card(state, option_primary_ref(state, option, my_index));
            int id = card_id_or_zero(card);
            score = 500;
            if (id == Buddy_Buddy_Poffin && (dreepy_play < 2 || budew_play == 0)) score = 2500;
            else if (id == Rare_Candy && dreepy_play > 0 && dragapult_play < 1) score = 2100;
            else if (id == Ultra_Ball || id == Poke_Pad) score = 1800;
            else if (id == Crispin) score = 1700;
            else if (id == Lillie_Determination) score = 1600;
            else if (id == Crushing_Hammer) score = 1500;
            else if (id == Boss_Orders && !state.players[op_index].bench.empty()) score = 1450;
            else if (id == Unfair_Stamp) score = 1350;
            else if (id == Night_Stretcher) score = 900;
            else if (id == Team_Rocket_Watchtower) score = 850;
        } else if (option.type == SelectOptionType::Attach) {
            CardRef target_ref = option_target_ref(state, option, my_index);
            const Card* target = safe_card(state, target_ref);
            int target_id = card_id_or_zero(target);
            int energy_count = safe_energy_count(state, my_index, target_ref);
            score = 600;
            if (id_in_list(target_id, {Dragapult_ex, Drakloak, Dreepy})) score += 900 - energy_count * 140;
            const Card* attach = nullptr;
            try {
                attach = safe_card(state, state.getAttachCardRef(option, my_index));
            } catch (...) {
            }
            int attach_id = card_id_or_zero(attach);
            if (attach_id == Basic_Fire_Energy || attach_id == Basic_Psychic_Energy) score += 250;
        } else if (option.type == SelectOptionType::Evolve) {
            const Card* target = safe_card(state, option_target_ref(state, option, my_index));
            int target_id = card_id_or_zero(target);
            score = 1600;
            if (target_id == Drakloak || target_id == Dreepy) score += 700;
        } else if (option.type == SelectOptionType::Ability) {
            score = 1200;
        } else if (option.type == SelectOptionType::Attack) {
            score = 1800 + option.param0;
        } else if (option.type == SelectOptionType::End) {
            score = state.selectContext == SelectContext::Main ? 30 : 0;
        } else if (option.type == SelectOptionType::Retreat) {
            score = dragapult_play > 0 ? 200 : -20;
        } else if (option.type == SelectOptionType::Card
            || option.type == SelectOptionType::ToolCard
            || option.type == SelectOptionType::EnergyCard
            || option.type == SelectOptionType::Energy) {
            CardRef ref = option_primary_ref(state, option, my_index);
            const Card* card = safe_card(state, ref);
            int id = card_id_or_zero(card);
            int owner = option.type == SelectOptionType::Card
                || option.type == SelectOptionType::ToolCard
                || option.type == SelectOptionType::EnergyCard
                || option.type == SelectOptionType::Energy ? option.param2 : my_index;
            if (owner != my_index) {
                score = target_pressure_score(state, card, ref, owner);
            } else if (state.selectContext == SelectContext::Discard
                || state.selectContext == SelectContext::DiscardCardOrAttachedCard
                || state.selectContext == SelectContext::DiscardEnergy
                || state.selectContext == SelectContext::DiscardEnergyCard) {
                score = id_in_list(id, {Basic_Fire_Energy, Basic_Psychic_Energy, Meowth_ex}) ? 400 : 120;
            } else if (id_in_list(id, {Dreepy, Drakloak, Dragapult_ex, Budew, Fezandipiti_ex, Rare_Candy, Buddy_Buddy_Poffin, Ultra_Ball, Crispin, Lillie_Determination})) {
                score = 1500;
            } else if (id_in_list(id, {Basic_Fire_Energy, Basic_Psychic_Energy})) {
                score = 900;
            } else {
                score = 300;
            }
        } else if (option.type == SelectOptionType::Yes) {
            score = 200;
        } else if (option.type == SelectOptionType::No) {
            score = state.selectMin == 0 ? 10 : 0;
        } else if (option.type == SelectOptionType::Number) {
            score = option.param0;
        }

        scores.push_back({score, option_index});
    }

    selected_by_scores(state, scores);
}

inline void selected_archaludon_exact(State& state) {
    constexpr int Duraludon = 169;
    constexpr int Archaludon_ex = 190;
    constexpr int Cinderace = 666;
    constexpr int Relicanth = 57;
    constexpr int Metal_Energy = 8;
    constexpr int Poke_Pad = 1152;
    constexpr int Ultra_Ball = 1121;
    constexpr int Pokegear = 1122;
    constexpr int Night_Stretcher = 1097;
    constexpr int Jumbo_Ice_Cream = 1147;
    constexpr int Hero_Cape = 1159;
    constexpr int Boss_Orders = 1182;
    constexpr int Explorer_Guidance = 1185;
    constexpr int Lillie_Determination = 1227;
    constexpr int Full_Metal_Lab = 1244;
    constexpr int Judge = 1213;

    int my_index = state.selectPlayer;
    int op_index = 1 - my_index;
    int duraludon_play = count_id_in_play(state, my_index, Duraludon);
    int archaludon_play = count_id_in_play(state, my_index, Archaludon_ex);

    std::vector<std::pair<int, int>> scores;
    scores.reserve(state.options.size());
    for (int option_index = 0; option_index < (int)state.options.size(); option_index++) {
        const SelectOption& option = state.options[option_index];
        int score = 0;

        if (option.type == SelectOptionType::Play) {
            const Card* card = safe_card(state, option_primary_ref(state, option, my_index));
            int id = card_id_or_zero(card);
            score = 450;
            if (id == Full_Metal_Lab) score = 2100;
            else if (id == Ultra_Ball || id == Poke_Pad || id == Pokegear) score = 1900;
            else if (id == Lillie_Determination || id == Explorer_Guidance) score = 1700;
            else if (id == Judge) score = 1550;
            else if (id == Boss_Orders && !state.players[op_index].bench.empty()) score = 1450;
            else if (id == Night_Stretcher) score = 950;
            else if (id == Jumbo_Ice_Cream && !state.players[my_index].active.empty()) {
                const Card* active = safe_card(state, state.players[my_index].active.at(0));
                score = remaining_hp_or_zero(state, active) < safe_max_hp_or_zero(state, active) ? 1500 : 0;
            }
        } else if (option.type == SelectOptionType::Attach) {
            CardRef target_ref = option_target_ref(state, option, my_index);
            const Card* target = safe_card(state, target_ref);
            int target_id = card_id_or_zero(target);
            int energy_count = safe_energy_count(state, my_index, target_ref);
            score = 500;
            if (id_in_list(target_id, {Archaludon_ex, Duraludon})) score += 1200 - energy_count * 180;
            const Card* attach = nullptr;
            try {
                attach = safe_card(state, state.getAttachCardRef(option, my_index));
            } catch (...) {
            }
            if (card_id_or_zero(attach) == Hero_Cape && (AreaType)option.param2 == AreaType::Active) score += 1200;
        } else if (option.type == SelectOptionType::Evolve) {
            const Card* target = safe_card(state, option_target_ref(state, option, my_index));
            score = card_id_or_zero(target) == Duraludon ? 2600 : 1500;
        } else if (option.type == SelectOptionType::Ability) {
            score = 1700;
        } else if (option.type == SelectOptionType::Attack) {
            score = 1700 + option.param0;
        } else if (option.type == SelectOptionType::End) {
            score = state.selectContext == SelectContext::Main ? 35 : 0;
        } else if (option.type == SelectOptionType::Retreat) {
            score = archaludon_play > 0 ? 240 : -20;
        } else if (option.type == SelectOptionType::Card
            || option.type == SelectOptionType::ToolCard
            || option.type == SelectOptionType::EnergyCard
            || option.type == SelectOptionType::Energy) {
            CardRef ref = option_primary_ref(state, option, my_index);
            const Card* card = safe_card(state, ref);
            int id = card_id_or_zero(card);
            int owner = option.param2;
            if (owner != my_index) {
                score = target_pressure_score(state, card, ref, owner);
                if (id_in_list(id, {119, 120, 121, 344, 345, 741, 742, 743})) score += 300;
            } else if (state.selectContext == SelectContext::SetupActivePokemon) {
                if (id == Cinderace) score = 4000;
                else if (id == Duraludon) score = 2500;
                else if (id == Relicanth) score = 1200;
                else score = 300;
            } else if (state.selectContext == SelectContext::SetupBenchPokemon || state.selectContext == SelectContext::ToBench) {
                if (id == Duraludon && duraludon_play < 2) score = 3000;
                else if (id == Relicanth) score = 1600;
                else if (id == Cinderace) score = 1200;
                else score = 400;
            } else if (state.selectContext == SelectContext::Discard
                || state.selectContext == SelectContext::DiscardCardOrAttachedCard
                || state.selectContext == SelectContext::DiscardEnergy
                || state.selectContext == SelectContext::DiscardEnergyCard) {
                score = id == Metal_Energy || id == Cinderace ? 700 : 120;
            } else if (id_in_list(id, {Duraludon, Archaludon_ex, Relicanth, Metal_Energy, Ultra_Ball, Poke_Pad, Pokegear, Lillie_Determination, Explorer_Guidance, Full_Metal_Lab})) {
                score = 1500;
            } else {
                score = 300;
            }
        } else if (option.type == SelectOptionType::Yes) {
            score = 200;
        } else if (option.type == SelectOptionType::No) {
            score = state.selectMin == 0 ? 10 : 0;
        } else if (option.type == SelectOptionType::Number) {
            score = option.param0;
        }

        scores.push_back({score, option_index});
    }

    selected_by_scores(state, scores);
}

inline void selected_magcargo_exact(State& state) {
    constexpr int Magcargo_ex = 30;
    constexpr int Slugma = 76;
    constexpr int Heatmor = 572;
    constexpr int Hearthflame_Ogerpon_ex = 358;
    constexpr int Basic_Fire_Energy = 2;
    constexpr int Sparkling_Crystal = 1165;
    constexpr int Academy_At_Night = 1248;
    constexpr int Tera_Orb = 1127;
    constexpr int Ultra_Ball = 1121;
    constexpr int Dusk_Ball = 1102;
    constexpr int Hilda = 1225;
    constexpr int Lillie_Determination = 1227;
    constexpr int Carmine = 1192;
    constexpr int Boss_Orders = 1182;
    constexpr int Switch = 1123;
    constexpr int Night_Stretcher = 1097;

    int my_index = state.selectPlayer;
    int op_index = 1 - my_index;
    int slugma_play = count_id_in_play(state, my_index, Slugma);
    int magcargo_play = count_id_in_play(state, my_index, Magcargo_ex);
    int heatmor_play = count_id_in_play(state, my_index, Heatmor);

    std::vector<std::pair<int, int>> scores;
    scores.reserve(state.options.size());
    for (int option_index = 0; option_index < (int)state.options.size(); option_index++) {
        const SelectOption& option = state.options[option_index];
        int score = 0;

        if (option.type == SelectOptionType::Play) {
            const Card* card = safe_card(state, option_primary_ref(state, option, my_index));
            int id = card_id_or_zero(card);
            score = 450;
            if (id == Tera_Orb || id == Ultra_Ball || id == Dusk_Ball) score = 2300;
            else if (id == Hilda || id == Lillie_Determination || id == Carmine) score = 1800;
            else if (id == Boss_Orders && !state.players[op_index].bench.empty()) score = 1650;
            else if (id == Academy_At_Night) score = 1250;
            else if (id == Night_Stretcher) score = 950;
            else if (id == Switch) score = 800;
        } else if (option.type == SelectOptionType::Attach) {
            CardRef target_ref = option_target_ref(state, option, my_index);
            const Card* target = safe_card(state, target_ref);
            int target_id = card_id_or_zero(target);
            int energy_count = safe_energy_count(state, my_index, target_ref);
            score = 500;
            if (target_id == Magcargo_ex) score += 1400 - energy_count * 120;
            else if (target_id == Slugma) score += 1000 - energy_count * 120;
            else if (target_id == Hearthflame_Ogerpon_ex || target_id == Heatmor) score += 850 - energy_count * 100;
            const Card* attach = nullptr;
            try {
                attach = safe_card(state, state.getAttachCardRef(option, my_index));
            } catch (...) {
            }
            int attach_id = card_id_or_zero(attach);
            if (attach_id == Basic_Fire_Energy) score += 350;
            if (attach_id == Sparkling_Crystal && (target_id == Magcargo_ex || target_id == Hearthflame_Ogerpon_ex)) score += 1400;
        } else if (option.type == SelectOptionType::Evolve) {
            const Card* target = safe_card(state, option_target_ref(state, option, my_index));
            score = card_id_or_zero(target) == Slugma ? 2800 : 1300;
        } else if (option.type == SelectOptionType::Ability) {
            score = 1550;
        } else if (option.type == SelectOptionType::Attack) {
            score = 2100 + option.param0;
        } else if (option.type == SelectOptionType::End) {
            score = state.selectContext == SelectContext::Main ? 25 : 0;
        } else if (option.type == SelectOptionType::Retreat) {
            score = magcargo_play > 0 ? 250 : -20;
        } else if (option.type == SelectOptionType::Card
            || option.type == SelectOptionType::ToolCard
            || option.type == SelectOptionType::EnergyCard
            || option.type == SelectOptionType::Energy) {
            CardRef ref = option_primary_ref(state, option, my_index);
            const Card* card = safe_card(state, ref);
            int id = card_id_or_zero(card);
            int owner = option.param2;
            if (owner != my_index) {
                score = target_pressure_score(state, card, ref, owner);
                if (id_in_list(id, {119, 120, 121, 169, 190, 344, 345, 741, 742, 743})) score += 350;
            } else if (state.selectContext == SelectContext::SetupActivePokemon) {
                if (id == Slugma) score = 3600;
                else if (id == Heatmor) score = 2400;
                else if (id == Hearthflame_Ogerpon_ex) score = 1600;
                else score = 300;
            } else if (state.selectContext == SelectContext::SetupBenchPokemon || state.selectContext == SelectContext::ToBench) {
                if (id == Slugma && slugma_play < 2) score = 3300;
                else if (id == Heatmor && heatmor_play < 1) score = 1800;
                else if (id == Hearthflame_Ogerpon_ex) score = 1500;
                else score = 400;
            } else if (state.selectContext == SelectContext::Discard
                || state.selectContext == SelectContext::DiscardCardOrAttachedCard
                || state.selectContext == SelectContext::DiscardEnergy
                || state.selectContext == SelectContext::DiscardEnergyCard) {
                score = id == Basic_Fire_Energy || id == Heatmor ? 650 : 120;
            } else if (id_in_list(id, {Slugma, Magcargo_ex, Heatmor, Hearthflame_Ogerpon_ex, Basic_Fire_Energy, Sparkling_Crystal, Tera_Orb, Ultra_Ball, Dusk_Ball, Hilda, Lillie_Determination, Carmine, Boss_Orders})) {
                score = 1550;
            } else {
                score = 300;
            }
        } else if (option.type == SelectOptionType::Yes) {
            score = 200;
        } else if (option.type == SelectOptionType::No) {
            score = state.selectMin == 0 ? 10 : 0;
        } else if (option.type == SelectOptionType::Number) {
            score = option.param0;
        }

        scores.push_back({score, option_index});
    }

    selected_by_scores(state, scores);
}

inline void selected_iron_thorns_exact(State& state) {
    constexpr int Iron_Thorns_ex = 37;
    constexpr int Miraidon = 87;
    constexpr int Miraidon_ex = 313;
    constexpr int Iron_Crown_ex = 80;
    constexpr int Mist_Energy = 11;
    constexpr int Basic_Lightning_Energy = 4;
    constexpr int Basic_Psychic_Energy = 5;
    constexpr int Reboot_Pod = 1089;
    constexpr int Night_Stretcher = 1097;
    constexpr int Energy_Switch = 1116;
    constexpr int Energy_Retrieval = 1118;
    constexpr int Energy_Search = 1119;
    constexpr int Ultra_Ball = 1121;
    constexpr int Switch = 1123;
    constexpr int Powerglass = 1163;
    constexpr int Boss_Orders = 1182;
    constexpr int Carmine = 1192;
    constexpr int Lillie_Determination = 1227;

    int my_index = state.selectPlayer;
    int op_index = 1 - my_index;
    int thorns_play = count_id_in_play(state, my_index, Iron_Thorns_ex);
    int miraidon_play = count_id_in_play(state, my_index, Miraidon_ex) + count_id_in_play(state, my_index, Miraidon);
    int crown_play = count_id_in_play(state, my_index, Iron_Crown_ex);

    std::vector<std::pair<int, int>> scores;
    scores.reserve(state.options.size());
    for (int option_index = 0; option_index < (int)state.options.size(); option_index++) {
        const SelectOption& option = state.options[option_index];
        int score = 0;

        if (option.type == SelectOptionType::Play) {
            const Card* card = safe_card(state, option_primary_ref(state, option, my_index));
            int id = card_id_or_zero(card);
            score = 450;
            if (id == Ultra_Ball) score = 2300;
            else if (id == Carmine || id == Lillie_Determination) score = 1850;
            else if (id == Boss_Orders && !state.players[op_index].bench.empty()) score = 1700;
            else if (id == Energy_Search || id == Energy_Retrieval || id == Energy_Switch || id == Reboot_Pod) score = 1450;
            else if (id == Night_Stretcher) score = 950;
            else if (id == Switch) score = 850;
        } else if (option.type == SelectOptionType::Attach) {
            CardRef target_ref = option_target_ref(state, option, my_index);
            const Card* target = safe_card(state, target_ref);
            int target_id = card_id_or_zero(target);
            int energy_count = safe_energy_count(state, my_index, target_ref);
            score = 500;
            if (target_id == Iron_Thorns_ex || target_id == Miraidon_ex) score += 1350 - energy_count * 150;
            else if (target_id == Miraidon || target_id == Iron_Crown_ex) score += 950 - energy_count * 120;
            const Card* attach = nullptr;
            try {
                attach = safe_card(state, state.getAttachCardRef(option, my_index));
            } catch (...) {
            }
            int attach_id = card_id_or_zero(attach);
            if (id_in_list(attach_id, {Basic_Lightning_Energy, Basic_Psychic_Energy, Mist_Energy})) score += 350;
            if (attach_id == Powerglass && id_in_list(target_id, {Iron_Thorns_ex, Miraidon_ex})) score += 1200;
        } else if (option.type == SelectOptionType::Evolve) {
            score = 900;
        } else if (option.type == SelectOptionType::Ability) {
            score = 1400;
        } else if (option.type == SelectOptionType::Attack) {
            score = 2050 + option.param0;
        } else if (option.type == SelectOptionType::End) {
            score = state.selectContext == SelectContext::Main ? 25 : 0;
        } else if (option.type == SelectOptionType::Retreat) {
            score = (thorns_play + miraidon_play) > 0 ? 260 : -20;
        } else if (option.type == SelectOptionType::Card
            || option.type == SelectOptionType::ToolCard
            || option.type == SelectOptionType::EnergyCard
            || option.type == SelectOptionType::Energy) {
            CardRef ref = option_primary_ref(state, option, my_index);
            const Card* card = safe_card(state, ref);
            int id = card_id_or_zero(card);
            int owner = option.param2;
            if (owner != my_index) {
                score = target_pressure_score(state, card, ref, owner);
                if (id_in_list(id, {119, 120, 121, 169, 190, 344, 345, 741, 742, 743})) score += 450;
            } else if (state.selectContext == SelectContext::SetupActivePokemon) {
                if (id == Iron_Thorns_ex) score = 4200;
                else if (id == Miraidon_ex) score = 3300;
                else if (id == Miraidon) score = 2200;
                else if (id == Iron_Crown_ex) score = 1500;
                else score = 300;
            } else if (state.selectContext == SelectContext::SetupBenchPokemon || state.selectContext == SelectContext::ToBench) {
                if (id == Iron_Thorns_ex && thorns_play < 2) score = 3300;
                else if ((id == Miraidon_ex || id == Miraidon) && miraidon_play < 2) score = 2600;
                else if (id == Iron_Crown_ex && crown_play < 1) score = 1800;
                else score = 400;
            } else if (state.selectContext == SelectContext::Discard
                || state.selectContext == SelectContext::DiscardCardOrAttachedCard
                || state.selectContext == SelectContext::DiscardEnergy
                || state.selectContext == SelectContext::DiscardEnergyCard) {
                score = id_in_list(id, {Basic_Lightning_Energy, Basic_Psychic_Energy, Mist_Energy, Miraidon}) ? 550 : 120;
            } else if (id_in_list(id, {Iron_Thorns_ex, Miraidon, Miraidon_ex, Iron_Crown_ex, Basic_Lightning_Energy, Basic_Psychic_Energy, Mist_Energy, Ultra_Ball, Carmine, Lillie_Determination, Boss_Orders, Energy_Switch, Powerglass})) {
                score = 1550;
            } else {
                score = 300;
            }
        } else if (option.type == SelectOptionType::Yes) {
            score = 200;
        } else if (option.type == SelectOptionType::No) {
            score = state.selectMin == 0 ? 10 : 0;
        } else if (option.type == SelectOptionType::Number) {
            score = option.param0;
        }

        scores.push_back({score, option_index});
    }

    selected_by_scores(state, scores);
}

inline void selected_great_tusk_crustle_exact(State& state) {
    constexpr int Great_Tusk = 58;
    constexpr int Dwebble = 344;
    constexpr int Crustle = 345;
    constexpr int Terrakion = 607;
    constexpr int Mist_Energy = 11;
    constexpr int Rock_Fighting_Energy = 20;
    constexpr int Buddy_Buddy_Poffin = 1086;
    constexpr int Ultra_Ball = 1121;
    constexpr int Pokegear = 1122;
    constexpr int Switch = 1123;
    constexpr int Fight_Gong = 1142;
    constexpr int Jumbo_Ice_Cream = 1147;
    constexpr int Poke_Pad = 1152;
    constexpr int Boss_Orders = 1182;
    constexpr int Explorer_Guidance = 1185;
    constexpr int Colress_Tenacity = 1194;
    constexpr int Xerosic_Machinations = 1197;
    constexpr int Lisia_Appeal = 1204;
    constexpr int Neutral_Center = 1247;
    constexpr int Land_Collapse = 62;
    constexpr int Giant_Tusk = 63;
    constexpr int Ascension = 478;
    constexpr int Superb_Scissors = 479;
    constexpr int Retaliate = 873;
    constexpr int Land_Crush = 874;

    int my_index = state.selectPlayer;
    int op_index = 1 - my_index;
    int tusk_play = count_id_in_play(state, my_index, Great_Tusk);
    int dwebble_play = count_id_in_play(state, my_index, Dwebble);
    int crustle_play = count_id_in_play(state, my_index, Crustle);

    std::vector<std::pair<int, int>> scores;
    scores.reserve(state.options.size());
    for (int option_index = 0; option_index < (int)state.options.size(); option_index++) {
        const SelectOption& option = state.options[option_index];
        int score = 0;

        if (option.type == SelectOptionType::Play) {
            const Card* card = safe_card(state, option_primary_ref(state, option, my_index));
            int id = card_id_or_zero(card);
            score = 450;
            if (id == Buddy_Buddy_Poffin && (tusk_play + dwebble_play) < 4) score = 2600;
            else if (id == Fight_Gong) score = 2500;
            else if (id == Pokegear || id == Poke_Pad || id == Ultra_Ball) score = 2250;
            else if (id == Explorer_Guidance || id == Colress_Tenacity) score = 2000;
            else if (id == Xerosic_Machinations) score = 1900;
            else if (id == Boss_Orders && !state.players[op_index].bench.empty()) score = 1850;
            else if (id == Lisia_Appeal) score = 1600;
            else if (id == Switch) score = 1200;
            else if (id == Neutral_Center) score = 1100;
            else if (id == Jumbo_Ice_Cream && !state.players[my_index].active.empty()) {
                const Card* active = safe_card(state, state.players[my_index].active.at(0));
                score = remaining_hp_or_zero(state, active) < safe_max_hp_or_zero(state, active) ? 1700 : 10;
            }
        } else if (option.type == SelectOptionType::Attach) {
            CardRef target_ref = option_target_ref(state, option, my_index);
            const Card* target = safe_card(state, target_ref);
            int target_id = card_id_or_zero(target);
            int energy_count = safe_energy_count(state, my_index, target_ref);
            score = 450;
            if (target_id == Great_Tusk) score += 1500 - energy_count * 170;
            else if (target_id == Crustle) score += 1300 - energy_count * 140;
            else if (target_id == Dwebble) score += 900 - energy_count * 120;
            else if (target_id == Terrakion) score += 800 - energy_count * 100;
            const Card* attach = nullptr;
            try {
                attach = safe_card(state, state.getAttachCardRef(option, my_index));
            } catch (...) {
            }
            int attach_id = card_id_or_zero(attach);
            if (attach_id == Rock_Fighting_Energy && id_in_list(target_id, {Great_Tusk, Terrakion, Crustle})) score += 450;
            if (attach_id == Mist_Energy && target_id == Crustle) score += 500;
        } else if (option.type == SelectOptionType::Evolve) {
            const Card* target = safe_card(state, option_target_ref(state, option, my_index));
            score = card_id_or_zero(target) == Dwebble ? 2850 : 1300;
        } else if (option.type == SelectOptionType::Ability) {
            score = 1300;
        } else if (option.type == SelectOptionType::Attack) {
            score = 1500 + option.param0;
            if (option.param0 == Land_Collapse) score = 2550;
            else if (option.param0 == Ascension && crustle_play < 2) score = 2450;
            else if (option.param0 == Giant_Tusk || option.param0 == Superb_Scissors || option.param0 == Retaliate || option.param0 == Land_Crush) score = 2150;
        } else if (option.type == SelectOptionType::End) {
            score = state.selectContext == SelectContext::Main ? 25 : 0;
        } else if (option.type == SelectOptionType::Retreat) {
            score = (tusk_play + crustle_play) > 0 ? 500 : -20;
        } else if (option.type == SelectOptionType::Card
            || option.type == SelectOptionType::ToolCard
            || option.type == SelectOptionType::EnergyCard
            || option.type == SelectOptionType::Energy) {
            CardRef ref = option_primary_ref(state, option, my_index);
            const Card* card = safe_card(state, ref);
            int id = card_id_or_zero(card);
            int owner = option.param2;
            if (owner != my_index) {
                score = target_pressure_score(state, card, ref, owner);
                if (id_in_list(id, {119, 120, 121, 169, 190, 344, 345, 741, 742, 743})) score += 450;
            } else if (state.selectContext == SelectContext::SetupActivePokemon
                || state.selectContext == SelectContext::ToActive
                || state.selectContext == SelectContext::Switch) {
                if (id == Great_Tusk) score = 3500;
                else if (id == Crustle) score = 3300;
                else if (id == Dwebble) score = 2300;
                else if (id == Terrakion) score = 1600;
                else score = 300;
                score += safe_energy_count(state, my_index, ref) * 180 + remaining_hp_or_zero(state, card) / 3;
            } else if (state.selectContext == SelectContext::SetupBenchPokemon
                || state.selectContext == SelectContext::ToBench
                || state.selectContext == SelectContext::ToField) {
                if (id == Dwebble && dwebble_play < 3) score = 3200;
                else if (id == Great_Tusk && tusk_play < 2) score = 3000;
                else if (id == Terrakion) score = 1400;
                else score = 350;
            } else if (state.selectContext == SelectContext::Discard
                || state.selectContext == SelectContext::DiscardCardOrAttachedCard
                || state.selectContext == SelectContext::DiscardEnergy
                || state.selectContext == SelectContext::DiscardEnergyCard
                || state.selectContext == SelectContext::DiscardToolCard) {
                if (id_in_list(id, {Great_Tusk, Crustle, Dwebble, Rock_Fighting_Energy, Mist_Energy})) score = 25;
                else score = generic_discard_score(state, ref, my_index);
            } else if (id_in_list(id, {Great_Tusk, Dwebble, Crustle, Fight_Gong, Pokegear, Poke_Pad, Buddy_Buddy_Poffin, Boss_Orders, Explorer_Guidance, Xerosic_Machinations})) {
                score = 1600;
            } else if (id_in_list(id, {Rock_Fighting_Energy, Mist_Energy})) {
                score = 1200;
            } else {
                score = 300;
            }
        } else if (option.type == SelectOptionType::Yes) {
            score = 200;
        } else if (option.type == SelectOptionType::No) {
            score = state.selectMin == 0 ? 10 : 0;
        } else if (option.type == SelectOptionType::Number) {
            score = option.param0;
        }

        scores.push_back({score, option_index});
    }

    selected_by_scores(state, scores);
}

inline int generic_option_score(const State& state, const SelectOption& option, int option_index) {
    int my_index = state.selectPlayer;
    CardRef primary_ref = generic_option_primary_ref(state, option, my_index);
    CardRef target_ref = generic_option_target_ref(state, option, my_index);
    int primary_owner = my_index;
    if (option.type == SelectOptionType::Card
        || option.type == SelectOptionType::ToolCard
        || option.type == SelectOptionType::EnergyCard
        || option.type == SelectOptionType::Energy) {
        primary_owner = option.param2;
    }

    int score = 0;
    switch (option.type) {
        case SelectOptionType::Number:
            return option.param0;
        case SelectOptionType::Yes:
            return 120;
        case SelectOptionType::No:
            return state.selectMin == 0 ? 5 : 0;
        case SelectOptionType::End:
            return state.selectContext == SelectContext::Main ? 20 : 0;
        case SelectOptionType::Play:
            return 750 + generic_card_value(state, primary_ref, my_index);
        case SelectOptionType::Attach: {
            const Card* target_card = safe_card(state, target_ref);
            int target_energy = safe_energy_count(state, my_index, target_ref);
            int target_bonus = target_card == nullptr ? 0 : remaining_hp_or_zero(state, target_card) + 260 - 45 * target_energy;
            return 650 + target_bonus;
        }
        case SelectOptionType::Evolve:
            return 900 + generic_card_value(state, primary_ref, my_index)
                + generic_card_value(state, target_ref, my_index) / 4;
        case SelectOptionType::Ability:
            return 1000 + generic_card_value(state, primary_ref, my_index) / 5;
        case SelectOptionType::Retreat:
            return 260;
        case SelectOptionType::Attack:
            return 1500 + option.param0;
        case SelectOptionType::Discard:
            return generic_discard_score(state, primary_ref, my_index);
        case SelectOptionType::Card:
        case SelectOptionType::ToolCard:
        case SelectOptionType::EnergyCard:
        case SelectOptionType::Energy: {
            const Card* card = safe_card(state, primary_ref);
            int value = generic_card_value(state, primary_ref, primary_owner);
            if (primary_owner != my_index) {
                return 800 + value - remaining_hp_or_zero(state, card) / 2;
            }
            if (state.selectContext == SelectContext::SetupActivePokemon
                || state.selectContext == SelectContext::SetupBenchPokemon
                || state.selectContext == SelectContext::ToActive
                || state.selectContext == SelectContext::Switch) {
                return 400 + value + safe_energy_count(state, my_index, primary_ref) * 120;
            }
            if (state.selectContext == SelectContext::ToBench
                || state.selectContext == SelectContext::ToField) {
                return card != nullptr && card->getMaster().cardType == CardType::Pokemon ? 500 + value : value / 2;
            }
            if (state.selectContext == SelectContext::Discard
                || state.selectContext == SelectContext::DiscardCardOrAttachedCard
                || state.selectContext == SelectContext::DiscardEnergy
                || state.selectContext == SelectContext::DiscardEnergyCard
                || state.selectContext == SelectContext::DiscardToolCard) {
                return generic_discard_score(state, primary_ref, primary_owner);
            }
            if (state.selectContext == SelectContext::Damage
                || state.selectContext == SelectContext::DamageCounter
                || state.selectContext == SelectContext::DamageCounterAny
                || state.selectContext == SelectContext::Attack
                || state.selectContext == SelectContext::EffectTarget) {
                return primary_owner != my_index ? 850 + value - remaining_hp_or_zero(state, card) / 2 : value / 4;
            }
            return 200 + value;
        }
        case SelectOptionType::Skill:
            return 600 + option.param0;
        case SelectOptionType::SpecialCondition:
            return 100 + option.param0;
        default:
            return option_index;
    }
}

inline void selected_generic_public_agent(State& state) {
    std::vector<std::pair<int, int>> scores;
    scores.reserve(state.options.size());
    for (int option_index = 0; option_index < (int)state.options.size(); option_index++) {
        scores.push_back({ generic_option_score(state, state.options[option_index], option_index), option_index });
    }
    std::stable_sort(scores.begin(), scores.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });

    state.selected.clear();
    int max_count = std::min(state.selectMax, (int)scores.size());
    int required = std::min(state.selectMin, max_count);
    for (int i = 0; i < max_count; i++) {
        if ((int)state.selected.size() < required || scores[i].first > 0) {
            state.selected.push_back(scores[i].second);
        }
    }
}

inline bool selected_public_agent(State& state, int opponent) {
    if (opponent == 2) {
        selected_kiyota_abomasnow(state);
        return true;
    }
    if (opponent == 3) {
        selected_kiyota_iono(state);
        return true;
    }
    if (opponent == 4) {
        selected_kiyota_lucario(state);
        return true;
    }
    if (opponent == 6) {
        selected_dashimaki_crustle(state);
        return true;
    }
    if (opponent == 19) {
        selected_dragapult_exact(state);
        return true;
    }
    if (opponent == 20) {
        selected_archaludon_exact(state);
        return true;
    }
    if (opponent == 21) {
        selected_magcargo_exact(state);
        return true;
    }
    if (opponent == 22) {
        selected_iron_thorns_exact(state);
        return true;
    }
    if (opponent == 24) {
        selected_great_tusk_crustle_exact(state);
        return true;
    }
    if (opponent >= 5 && opponent <= 14) {
        selected_generic_public_agent(state);
        return true;
    }
    return false;
}

} // namespace ptcg_scorers
