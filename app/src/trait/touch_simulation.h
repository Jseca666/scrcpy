#ifndef SC_TOUCH_SIMULATION_H
#define SC_TOUCH_SIMULATION_H

#include "common.h"

#include <stdbool.h>
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

struct sc_touch_sampling_slot_state {
    bool active;
    bool has_position;

    uint16_t last_x;
    uint16_t last_y;
    uint16_t last_major;
    uint16_t last_minor;
    uint16_t last_azimuth;
    uint8_t last_pressure;

    uint64_t last_event_tick_us;
    uint64_t last_emit_tick_us;
    uint64_t release_deadline_us;
};

struct sc_touch_sampling_runtime {
    uint64_t target_interval_us;
    uint64_t sync_window_us;
    uint64_t release_hold_us;
};

struct sc_touch_simulation_config
sc_touch_simulation_config_default(enum sc_touch_motion_profile profile);

void
sc_touch_simulation_config_apply_profile(struct sc_touch_simulation_config *config,
                                        enum sc_touch_motion_profile profile);

void
sc_touch_simulation_config_set_pressure_scale(
        struct sc_touch_simulation_config *config, float pressure_scale);

void
sc_touch_simulation_config_set_default_contact_profile(
        struct sc_touch_simulation_config *config,
        uint16_t touch_major_default, uint16_t touch_minor_default);

void
sc_touch_simulation_config_set_orientation_mode(
        struct sc_touch_simulation_config *config,
        enum sc_touch_orientation_mode orientation_mode);

void
sc_touch_simulation_config_build_runtime(
        const struct sc_touch_simulation_config *config,
        struct sc_touch_sampling_runtime *runtime);

uint64_t
sc_touch_simulation_now_us(void);

uint64_t
sc_touch_simulation_effective_interval_us(
        const struct sc_touch_simulation_config *config,
        const struct sc_touch_sampling_runtime *runtime,
        const struct sc_touch_sampling_slot_state *state,
        uint16_t x, uint16_t y, uint64_t now_us);

bool
sc_touch_simulation_release_ready(
        const struct sc_touch_sampling_slot_state *state,
        uint64_t now_us);

float
sc_touch_simulation_distance(uint16_t x0, uint16_t y0,
                             uint16_t x1, uint16_t y1);

bool
sc_touch_simulation_should_emit_move(
        const struct sc_touch_simulation_config *config,
        const struct sc_touch_sampling_runtime *runtime,
        const struct sc_touch_sampling_slot_state *state,
        uint16_t x, uint16_t y, uint64_t now_us);


void
sc_touch_simulation_apply_position_smoothing(
        const struct sc_touch_simulation_config *config,
        const struct sc_touch_sampling_slot_state *state,
        uint16_t *x, uint16_t *y);

void
sc_touch_simulation_mark_emit(struct sc_touch_sampling_slot_state *state,
                              uint16_t x, uint16_t y,
                              uint16_t major, uint16_t minor,
                              uint8_t pressure, uint16_t azimuth,
                              uint64_t now_us);

void
sc_touch_simulation_mark_release(
        struct sc_touch_sampling_slot_state *state,
        const struct sc_touch_sampling_runtime *runtime,
        uint64_t now_us);

void
sc_touch_simulation_clear_slot(struct sc_touch_sampling_slot_state *state);

const char *
sc_touch_motion_profile_name(enum sc_touch_motion_profile profile);

const char *
sc_touch_orientation_mode_name(enum sc_touch_orientation_mode mode);


void
sc_touch_simulation_log_config(const char *prefix,
                               const struct sc_touch_simulation_config *config,
                               const struct sc_touch_sampling_runtime *runtime);

#ifdef __cplusplus
}
#endif

#endif
