#ifndef SC_TOUCHSCREEN_UHID_H
#define SC_TOUCHSCREEN_UHID_H

#include "common.h"
#include "controller.h"
#include "hid/hid_touchscreen.h"
#include "trait/touch_processor.h"
#include "trait/touch_simulation.h"

#include <stdbool.h>
#include <stdint.h>

struct sc_touchscreen_slot {
    bool active;
    bool pending_release;
    uint64_t pointer_id;
    uint16_t contact_id;
};

struct sc_touch_sampling_runtime;

struct sc_touchscreen_uhid {
    struct sc_touch_processor touch_processor;
    struct sc_hid_touchscreen hid;
    struct sc_controller *controller;

    struct sc_touchscreen_slot slots[SC_HID_TOUCHSCREEN_CONTACTS];
    uint16_t next_contact_id;
    unsigned update_depth;
    unsigned explicit_frame_depth;
    bool dirty;

    uint16_t touch_major;
    uint16_t touch_minor;
    uint16_t azimuth;

    struct sc_touch_simulation_config sim_config;
    struct sc_touch_sampling_runtime sim_runtime;
    struct sc_touch_sampling_slot_state sampling_slots[SC_HID_TOUCHSCREEN_CONTACTS];
    uint64_t batch_sync_anchor_us;
    bool batch_sync_anchor_active;
    uint64_t last_sync_tick_us;
};

bool
sc_touchscreen_uhid_init(struct sc_touchscreen_uhid *touchscreen,
                         struct sc_controller *controller);

void
sc_touchscreen_uhid_set_default_contact_profile(
        struct sc_touchscreen_uhid *touchscreen,
        uint16_t width, uint16_t height, uint16_t azimuth);

void
sc_touchscreen_uhid_set_simulation_config(
        struct sc_touchscreen_uhid *touchscreen,
        const struct sc_touch_simulation_config *config);

const struct sc_touch_simulation_config *
sc_touchscreen_uhid_get_simulation_config(
        const struct sc_touchscreen_uhid *touchscreen);

void
sc_touchscreen_uhid_set_pressure_scale(
        struct sc_touchscreen_uhid *touchscreen, float pressure_scale);

void
sc_touchscreen_uhid_set_orientation_mode(
        struct sc_touchscreen_uhid *touchscreen,
        enum sc_touch_orientation_mode orientation_mode);

void
sc_touchscreen_uhid_set_motion_profile(
        struct sc_touchscreen_uhid *touchscreen,
        enum sc_touch_motion_profile profile);

enum sc_touch_motion_profile
sc_touchscreen_uhid_get_motion_profile(
        const struct sc_touchscreen_uhid *touchscreen);

void
sc_touchscreen_uhid_begin_touch_frame(struct sc_touchscreen_uhid *touchscreen);

void
sc_touchscreen_uhid_end_touch_frame(struct sc_touchscreen_uhid *touchscreen);

bool
sc_touchscreen_uhid_ensure_pointer(struct sc_touchscreen_uhid *touchscreen,
                                   uint64_t pointer_id);

bool
sc_touchscreen_uhid_pointer_down(struct sc_touchscreen_uhid *touchscreen,
                                 uint64_t pointer_id,
                                 uint16_t x, uint16_t y,
                                 float pressure,
                                 uint16_t touch_major, uint16_t touch_minor,
                                 uint16_t azimuth);

bool
sc_touchscreen_uhid_pointer_move(struct sc_touchscreen_uhid *touchscreen,
                                 uint64_t pointer_id,
                                 uint16_t x, uint16_t y,
                                 float pressure,
                                 uint16_t touch_major, uint16_t touch_minor,
                                 uint16_t azimuth);

bool
sc_touchscreen_uhid_pointer_release(struct sc_touchscreen_uhid *touchscreen,
                                    uint64_t pointer_id,
                                    uint16_t x, uint16_t y,
                                    float pressure,
                                    uint16_t touch_major, uint16_t touch_minor,
                                    uint16_t azimuth);

bool
sc_touchscreen_uhid_finalize_pointer(struct sc_touchscreen_uhid *touchscreen,
                                     uint64_t pointer_id);

bool
sc_touchscreen_uhid_end_pointer(struct sc_touchscreen_uhid *touchscreen,
                                uint64_t pointer_id,
                                uint16_t x, uint16_t y,
                                float pressure,
                                uint16_t touch_major, uint16_t touch_minor,
                                uint16_t azimuth);

void
sc_touchscreen_uhid_clear_all_pointers(struct sc_touchscreen_uhid *touchscreen);

void
sc_touchscreen_uhid_reset(struct sc_touchscreen_uhid *touchscreen);

#endif
