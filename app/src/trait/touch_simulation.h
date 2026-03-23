#ifndef SC_TOUCH_SIMULATION_H
#define SC_TOUCH_SIMULATION_H

#include "common.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum sc_touch_orientation_mode {
    SC_TOUCH_ORIENTATION_FIXED = 0,
    SC_TOUCH_ORIENTATION_FOLLOW_MOTION,
    SC_TOUCH_ORIENTATION_HALF_RANGE,
};

enum sc_touch_motion_profile {
    SC_TOUCH_MOTION_PROFILE_STABLE = 0,
    SC_TOUCH_MOTION_PROFILE_NATURAL,
    SC_TOUCH_MOTION_PROFILE_RESPONSIVE,
    SC_TOUCH_MOTION_PROFILE_CUSTOM,
};

struct sc_touch_simulation_config {
    uint16_t sample_hz;
    uint16_t sync_window_ms;
    uint16_t release_hold_ms;

    float position_smoothing;
    float min_move_distance;
    float velocity_gain;
    float pressure_scale;

    uint16_t touch_major_default;
    uint16_t touch_minor_default;

    enum sc_touch_orientation_mode orientation_mode;
    enum sc_touch_motion_profile motion_profile;
};

struct sc_touch_simulation_config
sc_touch_simulation_config_default(enum sc_touch_motion_profile profile);

void
sc_touch_simulation_config_apply_profile(struct sc_touch_simulation_config *config,
                                        enum sc_touch_motion_profile profile);

const char *
sc_touch_motion_profile_name(enum sc_touch_motion_profile profile);

const char *
sc_touch_orientation_mode_name(enum sc_touch_orientation_mode mode);

#ifdef __cplusplus
}
#endif

#endif
