#include "trait/touch_simulation.h"

#include <math.h>
#include <string.h>
#include <SDL2/SDL_timer.h>

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
                                                   0.45f, 1.5f, 0.90f, 0.92f,
                                                   1120, 1520,
                                                   SC_TOUCH_ORIENTATION_FIXED,
                                                   profile);
        case SC_TOUCH_MOTION_PROFILE_RESPONSIVE:
            return sc_touch_simulation_config_make(120, 4, 4,
                                                   0.20f, 0.5f, 1.20f, 1.10f,
                                                   860, 1180,
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

void
sc_touch_simulation_config_set_pressure_scale(
        struct sc_touch_simulation_config *config, float pressure_scale) {
    if (pressure_scale < 0.1f) {
        pressure_scale = 0.1f;
    } else if (pressure_scale > 3.0f) {
        pressure_scale = 3.0f;
    }
    config->pressure_scale = pressure_scale;
    config->motion_profile = SC_TOUCH_MOTION_PROFILE_CUSTOM;
}

void
sc_touch_simulation_config_set_default_contact_profile(
        struct sc_touch_simulation_config *config,
        uint16_t touch_major_default, uint16_t touch_minor_default) {
    if (touch_major_default) {
        config->touch_major_default = touch_major_default;
    }
    if (touch_minor_default) {
        config->touch_minor_default = touch_minor_default;
    }
    config->motion_profile = SC_TOUCH_MOTION_PROFILE_CUSTOM;
}

void
sc_touch_simulation_config_set_orientation_mode(
        struct sc_touch_simulation_config *config,
        enum sc_touch_orientation_mode orientation_mode) {
    config->orientation_mode = orientation_mode;
    config->motion_profile = SC_TOUCH_MOTION_PROFILE_CUSTOM;
}

void
sc_touch_simulation_config_build_runtime(
        const struct sc_touch_simulation_config *config,
        struct sc_touch_sampling_runtime *runtime) {
    uint16_t hz = config->sample_hz ? config->sample_hz : 90;
    runtime->target_interval_us = 1000000ULL / hz;
    runtime->sync_window_us = (uint64_t) config->sync_window_ms * 1000ULL;
    runtime->release_hold_us = (uint64_t) config->release_hold_ms * 1000ULL;
}

uint64_t
sc_touch_simulation_now_us(void) {
    return SDL_GetTicks64() * 1000ULL;
}

float
sc_touch_simulation_distance(uint16_t x0, uint16_t y0,
                             uint16_t x1, uint16_t y1) {
    float dx = (float) x1 - (float) x0;
    float dy = (float) y1 - (float) y0;
    return sqrtf(dx * dx + dy * dy);
}

bool
sc_touch_simulation_should_emit_move(
        const struct sc_touch_simulation_config *config,
        const struct sc_touch_sampling_runtime *runtime,
        const struct sc_touch_sampling_slot_state *state,
        uint16_t x, uint16_t y, uint64_t now_us) {
    if (!state->has_position) {
        return true;
    }

    if (runtime->target_interval_us && state->last_emit_tick_us) {
        uint64_t elapsed_us = now_us - state->last_emit_tick_us;
        if (elapsed_us >= runtime->target_interval_us) {
            return true;
        }
    }

    if (config->min_move_distance <= 0.0f) {
        return false;
    }

    return sc_touch_simulation_distance(state->last_x, state->last_y, x, y)
           >= config->min_move_distance;
}

void
sc_touch_simulation_mark_emit(struct sc_touch_sampling_slot_state *state,
                              uint16_t x, uint16_t y,
                              uint16_t major, uint16_t minor,
                              uint8_t pressure, uint16_t azimuth,
                              uint64_t now_us) {
    state->active = true;
    state->has_position = true;
    state->last_x = x;
    state->last_y = y;
    state->last_major = major;
    state->last_minor = minor;
    state->last_pressure = pressure;
    state->last_azimuth = azimuth;
    state->last_event_tick_us = now_us;
    state->last_emit_tick_us = now_us;
}

void
sc_touch_simulation_mark_release(
        struct sc_touch_sampling_slot_state *state,
        const struct sc_touch_sampling_runtime *runtime,
        uint64_t now_us) {
    state->last_event_tick_us = now_us;
    state->release_deadline_us = now_us + runtime->release_hold_us;
}

void
sc_touch_simulation_clear_slot(struct sc_touch_sampling_slot_state *state) {
    memset(state, 0, sizeof(*state));
}
