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

typedef struct SwatSettings {
    float sensitivity;          // degrees per mouse pixel, independent of FPS
    float vertical_multiplier;
    float ads_multiplier;
    float vertical_fov;         // human camera only; RL sensors keep their FOV
    int frame_limit;
    bool invert_x, invert_y;
    float master_volume; // player output only; agent hearing is never muted
    float weapon_size,weapon_horizontal,weapon_vertical; // first-person view only
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

#ifdef __cplusplus
}
#endif
#endif
