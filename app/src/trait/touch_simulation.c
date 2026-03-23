#include "trait/touch_simulation.h"

static struct sc_touch_simulation_config
sc_touch_simulation_config_make(uint16_t sample_hz,
                                uint16_t sync_window_ms,
                                uint16_t release_hold_ms,
                                float position_smoothing,
                                float min_move_distance,
                                float velocity_gain,
                                float pressure_scale,
                                uint16_t touch_major_default,
                                uint16_t touch_minor_default,
                                enum sc_touch_orientation_mode orientation_mode,
                                enum sc_touch_motion_profile motion_profile) {
    struct sc_touch_simulation_config config = {
        .sample_hz = sample_hz,
        .sync_window_ms = sync_window_ms,
        .release_hold_ms = release_hold_ms,
        .position_smoothing = position_smoothing,
        .min_move_distance = min_move_distance,
        .velocity_gain = velocity_gain,
        .pressure_scale = pressure_scale,
        .touch_major_default = touch_major_default,
        .touch_minor_default = touch_minor_default,
        .orientation_mode = orientation_mode,
        .motion_profile = motion_profile,
    };
    return config;
}

struct sc_touch_simulation_config
sc_touch_simulation_config_default(enum sc_touch_motion_profile profile) {
    switch (profile) {
        case SC_TOUCH_MOTION_PROFILE_STABLE:
            return sc_touch_simulation_config_make(60, 8, 10,
                                                   0.45f, 1.5f, 0.90f, 1.00f,
                                                   1000, 1400,
                                                   SC_TOUCH_ORIENTATION_FIXED,
                                                   profile);
        case SC_TOUCH_MOTION_PROFILE_RESPONSIVE:
            return sc_touch_simulation_config_make(120, 4, 4,
                                                   0.20f, 0.5f, 1.20f, 1.00f,
                                                   1000, 1400,
                                                   SC_TOUCH_ORIENTATION_FOLLOW_MOTION,
                                                   profile);
        case SC_TOUCH_MOTION_PROFILE_NATURAL:
        case SC_TOUCH_MOTION_PROFILE_CUSTOM:
        default:
            return sc_touch_simulation_config_make(90, 6, 8,
                                                   0.30f, 1.0f, 1.00f, 1.00f,
                                                   1000, 1400,
                                                   SC_TOUCH_ORIENTATION_HALF_RANGE,
                                                   SC_TOUCH_MOTION_PROFILE_NATURAL);
    }
}

void
sc_touch_simulation_config_apply_profile(struct sc_touch_simulation_config *config,
                                         enum sc_touch_motion_profile profile) {
    *config = sc_touch_simulation_config_default(profile);
}

const char *
sc_touch_motion_profile_name(enum sc_touch_motion_profile profile) {
    switch (profile) {
        case SC_TOUCH_MOTION_PROFILE_STABLE:
            return "stable";
        case SC_TOUCH_MOTION_PROFILE_NATURAL:
            return "natural";
        case SC_TOUCH_MOTION_PROFILE_RESPONSIVE:
            return "responsive";
        case SC_TOUCH_MOTION_PROFILE_CUSTOM:
            return "custom";
        default:
            return "unknown";
    }
}

const char *
sc_touch_orientation_mode_name(enum sc_touch_orientation_mode mode) {
    switch (mode) {
        case SC_TOUCH_ORIENTATION_FIXED:
            return "fixed";
        case SC_TOUCH_ORIENTATION_FOLLOW_MOTION:
            return "follow_motion";
        case SC_TOUCH_ORIENTATION_HALF_RANGE:
            return "half_range";
        default:
            return "unknown";
    }
}
