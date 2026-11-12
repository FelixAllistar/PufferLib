// The interface's design system: one set of sizes, spacings, colours and
// timings that the HUD (hud.c, hunt_hud.c), the touch controls (touch.c) and
// the menus (menu.c) all draw with, so every screen reads as one system.
//
// Everything is in 1080-line layout units (hud.c scales them to the window
// and the player's Interface size), so a 15-unit line is 15 px at 1080p and
// about 11 px on a phone held sideways.
//
// The look is quiet and naturalistic: warm off-white ink on soft, dark,
// slightly transparent panels with a faint light edge, so text holds over
// sunlit ferns and over night scenes alike. Colour is kept for meaning:
// amber marks what to act on (the selection, a ready ability, the quest),
// green your group, red danger, and the vitals keep a colour each.
#ifndef CRET_UI_STYLE_H
#define CRET_UI_STYLE_H
#include "raylib.h"

// ---------------------------------------------------------------- type scale
// Four sizes, plus the game's name on the title.
#define UI_T_SMALL 12.0f     // captions, labels in spaced capitals, key caps, numbers on bars
#define UI_T_BODY 15.0f      // text in panels, prompts, nameplates
#define UI_T_ROW 20.0f       // menu rows and panel titles
#define UI_T_HEAD 40.0f      // page headings ("Settings", "You died")
#define UI_T_HERO 76.0f      // the game's name on the title
#define UI_TRACK 0.22f       // letter spacing of spaced capitals, as a share of the size

// ---------------------------------------------------------------- spacing and shape
#define UI_SP 4.0f           // the spacing unit: paddings and gaps are multiples of it
#define UI_PAD (3 * UI_SP)   // inside a panel
#define UI_GAP (2 * UI_SP)   // between items in a row
#define UI_RADIUS 6.0f       // corner radius of panels, chips and key caps
#define UI_EDGE 1.25f        // panel edge width
#define UI_BAR 5.0f          // the thickness of a meter (vitals, quest progress, sliders)
#define UI_STROKE 2.0f       // icon line weight at a 16-unit icon (it grows with the icon)
#define UI_ICON 16.0f        // the usual icon size (vitals, chips, prompts)
#define UI_MARGIN 28.0f      // from the screen edges to the HUD's panels

// ---------------------------------------------------------------- colours
// Roles, not hues: draw with these and nothing else (the vitals' own four
// are below). Alpha is part of the role: dim text is quieter, panels let
// a little of the world through.
static const Color UI_INK = {238, 233, 222, 245};      // primary text and icons
static const Color UI_DIM = {196, 189, 174, 190};      // secondary text, idle icons, captions
static const Color UI_FAINT = {196, 189, 174, 96};     // disabled rows, tracks, separators
static const Color UI_ACCENT = {232, 186, 98, 255};    // selection, ready, quest: what to act on
static const Color UI_GOOD = {146, 204, 118, 240};     // your group, herd and nest; mending
static const Color UI_WARN = {230, 156, 72, 245};      // a stat getting low, a slowed or afraid animal
static const Color UI_DANGER = {222, 78, 60, 250};     // bleeding, low health, marked predators, death
static const Color UI_PANEL = {16, 17, 15, 172};       // panel fill: dark and warm, slightly see-through
static const Color UI_PANEL_EDGE = {255, 248, 230, 26};// the panel's faint light edge
static const Color UI_SHADOW = {0, 0, 0, 70};          // the soft shadow round panels and under loose text
static const Color UI_HILITE = {255, 246, 224, 22};    // the selected row's wash
// The vitals: health, stamina, hunger, thirst.
static const Color UI_HEALTH = {204, 66, 52, 245}, UI_STAMINA = {222, 210, 172, 235}, UI_FOOD = {214, 152, 70, 240}, UI_WATER = {92, 156, 208, 240};

// ---------------------------------------------------------------- timing (seconds)
#define UI_FADE 0.15f        // page and panel fades, the selection sliding to its row
#define UI_HINT_HOLD 5.0f    // a long hint (how to stalk) shows this long, then fades
#define UI_HELP_HINT 60.0f   // the "F1 help" hint shows for the first minute of play
#define UI_PULSE 2.4f        // a warning's pulse, cycles per second (calm, not flashing)

// ---------------------------------------------------------------- icons
// Drawn from lines, arcs and polygons at any size (cret_ui_icon, hud.c), so
// they stay crisp and need no image files. One stroke weight for all.
enum {
    UI_I_NONE,
    // vitals
    UI_I_HEART, UI_I_LUNGS, UI_I_MEAT, UI_I_LEAF, UI_I_DROP,
    // attacks and actions
    UI_I_BITE, UI_I_RAM, UI_I_TAIL, UI_I_SLASH, UI_I_JUMP, UI_I_CARRY, UI_I_SNIFF, UI_I_NEST, UI_I_CALL, UI_I_MENU, UI_I_MAP,
    // abilities
    UI_I_HAMSTRING, UI_I_CRUSH, UI_I_REND, UI_I_SPIKES, UI_I_SWEEP, UI_I_CHARGE, UI_I_SHOVE, UI_I_POUNCE, UI_I_DODGE,
    UI_I_ROAR, UI_I_ALERT, UI_I_BELLOW, UI_I_SPRINT, UI_I_SHIELD, UI_I_HUNKER, UI_I_MEND, UI_I_LOCK,
    // markers and states
    UI_I_DIAMOND, UI_I_PREY, UI_I_EGG, UI_I_SKULL, UI_I_BONE, UI_I_STUN, UI_I_SLOW, UI_I_FEAR, UI_I_MARK, UI_I_WIND,
    UI_I_COUNT
};
#endif
