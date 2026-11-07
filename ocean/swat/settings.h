#ifndef SWAT_SETTINGS_H
#define SWAT_SETTINGS_H
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SWAT_SETTINGS_PATH_SIZE 1024
#define SWAT_SENSITIVITY_MIN 0.005f
#define SWAT_SENSITIVITY_MAX 0.15f

// Player controls. Each action has a primary and a secondary input. A code is
// a raylib 5.5 KeyboardKey, SWAT_BIND_MOUSE+MouseButton, or SWAT_BIND_NONE.
// Scope and planning keys reuse these actions (Use/Command mark, Jump executes,
// weapon keys pick feeds, strafe orbits); Modifier+key combos stay combos.
#define SWAT_BIND_SLOTS 2
#define SWAT_BIND_NONE 0
#define SWAT_BIND_MOUSE 1000
#define SWAT_BIND_MOUSE_BUTTONS 7
typedef enum SwatBindAction {
    SWAT_BIND_FORWARD, SWAT_BIND_BACK, SWAT_BIND_LEFT, SWAT_BIND_RIGHT,
    SWAT_BIND_LEAN_LEFT, SWAT_BIND_LEAN_RIGHT, SWAT_BIND_CROUCH, SWAT_BIND_SPRINT,
    SWAT_BIND_MODIFIER, SWAT_BIND_JUMP, SWAT_BIND_FIRE, SWAT_BIND_AIM_CONTEXT,
    SWAT_BIND_AIM, SWAT_BIND_USE, SWAT_BIND_RELOAD, SWAT_BIND_CANCEL_RELOAD,
    SWAT_BIND_PRIMARY, SWAT_BIND_SIDEARM, SWAT_BIND_FLASHBANG, SWAT_BIND_GAS,
    SWAT_BIND_PEPPER_SPRAY, SWAT_BIND_PLACE_CHARGE, SWAT_BIND_DISARM, SWAT_BIND_WEDGE,
    SWAT_BIND_DETONATE, SWAT_BIND_LOCKPICK, SWAT_BIND_TASER, SWAT_BIND_INSPECT,
    SWAT_BIND_COMMAND, SWAT_BIND_MELEE, SWAT_BIND_SELECTOR, SWAT_BIND_LOW_READY,
    SWAT_BIND_HIGH_READY, SWAT_BIND_SQUAD_MENU, SWAT_BIND_SQUAD_EXECUTE,
    SWAT_BIND_SNIPER_EXECUTE, SWAT_BIND_SNIPER_HOLD, SWAT_BIND_CAMERA, SWAT_BIND_PLANNING,
    SWAT_BIND_ACTIONS
} SwatBindAction;

typedef struct SwatSettings {
    float sensitivity;          // degrees per mouse pixel, independent of FPS
    float vertical_multiplier;
    float ads_multiplier;
    float vertical_fov;         // human camera only; RL sensors keep their FOV
    int frame_limit;
    bool invert_x, invert_y;
    float master_volume; // player output only; agent hearing is never muted
    float weapon_size,weapon_horizontal,weapon_vertical; // first-person view only
    float weapon_ads_relief; // eye to rear aperture, metres; carbine view only
    bool crouch_toggle, aim_toggle; // press-to-toggle instead of hold; officer only
    int bindings[SWAT_BIND_ACTIONS][SWAT_BIND_SLOTS];
} SwatSettings;

typedef struct SwatLookDelta { float yaw, pitch; } SwatLookDelta;

SwatSettings swat_settings_defaults(void);
void swat_settings_sanitize(SwatSettings* settings);
bool swat_settings_equal(const SwatSettings* a, const SwatSettings* b);
bool swat_settings_default_path(char* out, size_t capacity);
// Missing/unsupported files leave defaults and report false with errno.
// Unknown keys and malformed values are ignored; finite values are bounded.
bool swat_settings_load(SwatSettings* settings, const char* path);
// Writes beside the destination and atomically replaces it after close succeeds.
bool swat_settings_save(const SwatSettings* settings, const char* path);
bool swat_settings_prepare_path(const char* path);
SwatLookDelta swat_settings_look(const SwatSettings* settings,
                                float pixels_x, float pixels_y, float ads);

// Restores the default bindings and hold behaviour; other settings are kept.
void swat_settings_reset_controls(SwatSettings* settings);
const char* swat_bind_action_label(int action); // menu text, NULL when out of range
const char* swat_bind_action_key(int action);   // settings file key after "bind_"
const char* swat_bind_code_name(int code);      // "W", "Ctrl", "LMB"; NULL if unknown
int swat_bind_code_parse(const char* name);     // case-insensitive; -1 if unknown
// Escape, Tab, Enter, Backspace, Delete, F3, F5 and F9 keep fixed menu/save roles.
bool swat_bind_code_reserved(int code);
bool swat_bind_code_valid(int code);            // named and not reserved
// Another action using this action's slot code, or -1. Unbound slots never conflict.
int swat_bindings_conflict(const SwatSettings* settings, int action, int slot);
// First conflicting pair in table order; false when every bound code is unique.
bool swat_bindings_first_conflict(const SwatSettings* settings, int* action, int* other, int* code);

#ifdef __cplusplus
}
#endif
#endif
