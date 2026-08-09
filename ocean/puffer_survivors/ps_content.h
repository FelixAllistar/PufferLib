#pragma once

#include "ps_constants.h"

static inline const char* ps_upgrade_name(int type) {
    switch (type) {
        case PS_UPGRADE_BUBBLE: return "Bubble";
        case PS_UPGRADE_WHIRLPOOL: return "Whirlpool";
        case PS_UPGRADE_ORBIT: return "Orbit";
        case PS_UPGRADE_INK: return "Poison Oil";
        case PS_UPGRADE_SONAR: return "Sonar";
        case PS_UPGRADE_SPEED: return "Speed";
        case PS_UPGRADE_MAGNET: return "Magnet";
        case PS_UPGRADE_HEALTH: return "Health";
        case PS_UPGRADE_MIGHT: return "Might";
        case PS_UPGRADE_COOLDOWN: return "Cooldown";
        case PS_UPGRADE_AREA: return "Area";
        case PS_UPGRADE_PIERCE: return "Pierce";
        default: return "-";
    }
}

static inline const char* ps_upgrade_description(int type) {
    switch (type) {
        case PS_UPGRADE_BUBBLE: return "Faster bubbles\nthat pop harder.";
        case PS_UPGRADE_WHIRLPOOL: return "Bigger burst\nand knockback.";
        case PS_UPGRADE_ORBIT: return "More pearls for\nclose defense.";
        case PS_UPGRADE_INK: return "Poison pools and\na longer trail.";
        case PS_UPGRADE_SONAR: return "Huge pulse when\nswarmed.";
        case PS_UPGRADE_SPEED: return "Move faster out\nof danger.";
        case PS_UPGRADE_MAGNET: return "Pull XP and hearts\nfrom farther away.";
        case PS_UPGRADE_HEALTH: return "Gain max HP and\nheal now.";
        case PS_UPGRADE_MIGHT: return "All weapons deal\nmore damage.";
        case PS_UPGRADE_COOLDOWN: return "Weapons fire\nmore often.";
        case PS_UPGRADE_AREA: return "Larger hitboxes\nand pools.";
        case PS_UPGRADE_PIERCE: return "Bubbles pierce\nmore enemies.";
        default: return "Upgrade your survival odds.";
    }
}
