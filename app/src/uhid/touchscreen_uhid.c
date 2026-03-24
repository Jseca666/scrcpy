#include "touchscreen_uhid.h"

#include <assert.h>
#include <inttypes.h>
#include <string.h>

#include "control_msg.h"
#include "hid/hid_event.h"
#include "touchscreen_uhid_test.h"
#include "util/log.h"

static const char *SC_TOUCHSCREEN_NAME = "Synaptics_ts";

static uint16_t
sc_touchscreen_uhid_map_motion_orientation(uint16_t prev_x, uint16_t prev_y,
                                           uint16_t x, uint16_t y,
                                           uint16_t fallback) {
    int32_t dx = (int32_t) x - prev_x;
    int32_t dy = (int32_t) y - prev_y;
    int32_t adx = dx < 0 ? -dx : dx;
    int32_t ady = dy < 0 ? -dy : dy;
    int32_t norm = adx + ady;
    if (!norm) {
        return fallback;
    }

    int32_t azimuth = 9000 + (int32_t) ((int64_t) dy * 9000 / norm);
    if (azimuth < 0) {
        return 0;
    }
    if (azimuth > 18000) {
        return 18000;
    }
    return (uint16_t) azimuth;
}

static uint16_t
sc_touchscreen_uhid_pick_oriented_azimuth(struct sc_touchscreen_uhid *touchscreen,
                                          uint16_t event_azimuth, int slot,
                                          uint16_t x, uint16_t y,
                                          uint16_t fallback) {
    if (touchscreen->sim_config.orientation_mode
            == SC_TOUCH_ORIENTATION_FOLLOW_MOTION
            && slot >= 0 && (unsigned) slot < SC_HID_TOUCHSCREEN_CONTACTS) {
        const struct sc_hid_touchscreen_contact *contact =
            &touchscreen->hid.contacts[slot];
        if (contact->present) {
            return sc_touchscreen_uhid_map_motion_orientation(contact->x,
                                                              contact->y,
                                                              x, y, fallback);
        }
    }

    if (!event_azimuth) {
        return fallback;
    }
    return event_azimuth > 18000 ? 18000 : event_azimuth;
}

static int
sc_touchscreen_uhid_find_slot(struct sc_touchscreen_uhid *touchscreen,
                              uint64_t pointer_id, bool include_releasing) {
    for (unsigned i = 0; i < SC_HID_TOUCHSCREEN_CONTACTS; ++i) {
        if (!touchscreen->slots[i].active
                || touchscreen->slots[i].pointer_id != pointer_id) {
            continue;
        }
        if (!include_releasing
                && (touchscreen->slots[i].pending_release
                    || touchscreen->slots[i].finalize_pending_commit)) {
            continue;
        }
        return (int) i;
    }
    return -1;
}

static int
sc_touchscreen_uhid_acquire_slot(struct sc_touchscreen_uhid *touchscreen,
                                 uint64_t pointer_id) {
    int free_slot = -1;

    for (unsigned i = 0; i < SC_HID_TOUCHSCREEN_CONTACTS; ++i) {
        struct sc_touchscreen_slot *slot = &touchscreen->slots[i];
        if (!slot->active) {
            if (free_slot < 0) {
                free_slot = (int) i;
            }
            continue;
        }
        if (!slot->pending_release && !slot->finalize_pending_commit
                && slot->pointer_id == pointer_id) {
            return (int) i;
        }
    }

    if (free_slot < 0) {
        return -1;
    }

    struct sc_touchscreen_slot *slot = &touchscreen->slots[free_slot];
    slot->active = true;
    slot->pending_release = false;
    slot->finalize_pending_commit = false;
    slot->pointer_id = pointer_id;
    slot->contact_id = touchscreen->next_contact_id++;
    if (!slot->contact_id) {
        slot->contact_id = touchscreen->next_contact_id++;
    }
    return free_slot;
}

static void
sc_touchscreen_uhid_release_slot(struct sc_touchscreen_uhid *touchscreen,
                                 unsigned slot) {
    assert(slot < SC_HID_TOUCHSCREEN_CONTACTS);
    memset(&touchscreen->slots[slot], 0, sizeof(touchscreen->slots[slot]));
    sc_touch_simulation_clear_slot(&touchscreen->sampling_slots[slot]);
}

static uint8_t
sc_touchscreen_uhid_normalize_pressure(
        const struct sc_touchscreen_uhid *touchscreen, float pressure) {
    float scaled = pressure * touchscreen->sim_config.pressure_scale;
    if (scaled <= 0.0f) {
        return 1;
    }
    return scaled >= 1.0f ? 100 : (uint8_t) (scaled * 100.0f);
}

static uint64_t
sc_touchscreen_uhid_pick_sync_tick_us(struct sc_touchscreen_uhid *touchscreen,
                                      uint64_t now_us) {
    if (!touchscreen->sim_runtime.sync_window_us) {
        return now_us;
    }

    if (!touchscreen->sync_tick_us
            || (!touchscreen->explicit_frame_depth
                && now_us - touchscreen->sync_tick_us
                   > touchscreen->sim_runtime.sync_window_us)) {
        touchscreen->sync_tick_us = now_us;
    }
    return touchscreen->sync_tick_us;
}

static void
sc_touchscreen_uhid_store_contact(struct sc_touchscreen_uhid *touchscreen,
                                  int slot, uint16_t x, uint16_t y,
                                  float pressure_value,
                                  uint16_t touch_major,
                                  uint16_t touch_minor,
                                  uint16_t azimuth_value,
                                  uint64_t now_us) {
    uint16_t width = touch_major ? touch_major : touchscreen->touch_major;
    uint16_t height = touch_minor ? touch_minor : touchscreen->touch_minor;
    uint8_t pressure = sc_touchscreen_uhid_normalize_pressure(touchscreen,
                                                              pressure_value);
    uint16_t azimuth = sc_touchscreen_uhid_pick_oriented_azimuth(
            touchscreen, azimuth_value, slot, x, y, touchscreen->azimuth);

    sc_hid_touchscreen_set_contact(&touchscreen->hid, (unsigned) slot,
                                   touchscreen->slots[slot].contact_id,
                                   x, y, width, height, pressure, azimuth);
    sc_touch_simulation_mark_emit(&touchscreen->sampling_slots[slot],
                                  x, y, width, height, pressure, azimuth,
                                  now_us);
}

static bool
sc_touchscreen_uhid_commit(struct sc_touchscreen_uhid *touchscreen) {
    struct sc_hid_input hid_input;
    struct sc_control_msg msg;

    sc_hid_touchscreen_generate_input(&touchscreen->hid, &hid_input);
    assert(hid_input.size <= SC_HID_MAX_SIZE);

    msg.type = SC_CONTROL_MSG_TYPE_UHID_INPUT;
    msg.uhid_input.id = hid_input.hid_id;
    memcpy(msg.uhid_input.data, hid_input.data, hid_input.size);
    msg.uhid_input.size = hid_input.size;

    if (!sc_controller_push_msg(touchscreen->controller, &msg)) {
        LOGE("Could not push UHID_INPUT message (touchscreen)");
        return false;
    }
    return true;
}

static void
sc_touchscreen_uhid_release_finalized_slots(
        struct sc_touchscreen_uhid *touchscreen) {
    for (unsigned i = 0; i < SC_HID_TOUCHSCREEN_CONTACTS; ++i) {
        if (touchscreen->slots[i].active
                && touchscreen->slots[i].finalize_pending_commit
                && !touchscreen->slots[i].pending_release
                && !touchscreen->hid.contacts[i].present) {
            sc_touchscreen_uhid_release_slot(touchscreen, i);
        }
    }
}

static bool
sc_touchscreen_uhid_finalize_pending_releases(
        struct sc_touchscreen_uhid *touchscreen) {
    bool changed = false;

    for (unsigned i = 0; i < SC_HID_TOUCHSCREEN_CONTACTS; ++i) {
        if (!touchscreen->slots[i].active
                || !touchscreen->slots[i].pending_release
                || !touchscreen->slots[i].finalize_pending_commit) {
            continue;
        }

        sc_hid_touchscreen_clear_contact(&touchscreen->hid, i);
        touchscreen->slots[i].pending_release = false;
        sc_touch_simulation_clear_slot(&touchscreen->sampling_slots[i]);
        changed = true;
    }

    return changed;
}

static bool
sc_touchscreen_uhid_flush_now(struct sc_touchscreen_uhid *touchscreen) {
    if (!sc_touchscreen_uhid_commit(touchscreen)) {
        return false;
    }

    sc_touchscreen_uhid_release_finalized_slots(touchscreen);
    touchscreen->dirty = false;

    if (!sc_touchscreen_uhid_finalize_pending_releases(touchscreen)) {
        return true;
    }

    if (!sc_touchscreen_uhid_commit(touchscreen)) {
        return false;
    }

    sc_touchscreen_uhid_release_finalized_slots(touchscreen);
    return true;
}

static void
sc_touchscreen_uhid_commit_or_defer(struct sc_touchscreen_uhid *touchscreen) {
    if (touchscreen->explicit_frame_depth) {
        touchscreen->dirty = true;
        return;
    }

    if (!sc_touchscreen_uhid_flush_now(touchscreen)) {
        LOGW("Could not commit touchscreen state");
    }
}

void
sc_touchscreen_uhid_begin_touch_frame(struct sc_touchscreen_uhid *touchscreen) {
    if (!touchscreen->explicit_frame_depth) {
        touchscreen->sync_tick_us = 0;
    }
    ++touchscreen->explicit_frame_depth;
}

void
sc_touchscreen_uhid_end_touch_frame(struct sc_touchscreen_uhid *touchscreen) {
    if (!touchscreen->explicit_frame_depth) {
        LOGW("Ignoring unmatched end_touch_frame()");
        return;
    }

    if (--touchscreen->explicit_frame_depth) {
        return;
    }
    touchscreen->sync_tick_us = 0;

    if (touchscreen->dirty) {
        if (!sc_touchscreen_uhid_commit(touchscreen)) {
            LOGW("Could not flush explicit touchscreen frame");
            return;
        }
        sc_touchscreen_uhid_release_finalized_slots(touchscreen);
        touchscreen->dirty = false;
        return;
    }

    if (sc_touchscreen_uhid_finalize_pending_releases(touchscreen)) {
        if (!sc_touchscreen_uhid_commit(touchscreen)) {
            LOGW("Could not flush finalized touchscreen releases");
            return;
        }
        sc_touchscreen_uhid_release_finalized_slots(touchscreen);
    }
}

bool
sc_touchscreen_uhid_pointer_down(struct sc_touchscreen_uhid *touchscreen,
                                 uint64_t pointer_id,
                                 uint16_t x, uint16_t y,
                                 float pressure_value,
                                 uint16_t touch_major, uint16_t touch_minor,
                                 uint16_t azimuth_value) {
    int slot = sc_touchscreen_uhid_acquire_slot(touchscreen, pointer_id);
    if (slot < 0) {
        LOGW("Ignoring touch DOWN: no free touchscreen slot");
        return false;
    }

    sc_touchscreen_uhid_store_contact(
            touchscreen, slot, x, y, pressure_value, touch_major, touch_minor,
            azimuth_value, sc_touchscreen_uhid_pick_sync_tick_us(
                    touchscreen, sc_touch_simulation_now_us()));
    sc_touchscreen_uhid_commit_or_defer(touchscreen);
    return true;
}

bool
sc_touchscreen_uhid_pointer_move(struct sc_touchscreen_uhid *touchscreen,
                                 uint64_t pointer_id,
                                 uint16_t x, uint16_t y,
                                 float pressure_value,
                                 uint16_t touch_major, uint16_t touch_minor,
                                 uint16_t azimuth_value) {
    int slot = sc_touchscreen_uhid_find_slot(touchscreen, pointer_id, false);
    uint64_t now_us = sc_touch_simulation_now_us();

    if (slot < 0) {
        LOGW("Ignoring touch MOVE: pointer_id %" PRIu64 " not active",
             pointer_id);
        return false;
    }

    {
        unsigned active = 0;
        for (unsigned i = 0; i < SC_HID_TOUCHSCREEN_CONTACTS; ++i) {
            if (touchscreen->slots[i].active
                    && !touchscreen->slots[i].pending_release
                    && !touchscreen->slots[i].finalize_pending_commit) {
                ++active;
            }
        }
        if (active == 1) {
            const struct sc_touch_sampling_slot_state *state =
                &touchscreen->sampling_slots[slot];
            if (!sc_touch_simulation_should_emit_move(&touchscreen->sim_config,
                                                      &touchscreen->sim_runtime,
                                                      state, x, y, now_us)) {
                return true;
            }
            sc_touch_simulation_apply_position_smoothing(
                    &touchscreen->sim_config, state, &x, &y);
        }
    }

    sc_touchscreen_uhid_store_contact(
            touchscreen, slot, x, y, pressure_value, touch_major, touch_minor,
            azimuth_value,
            sc_touchscreen_uhid_pick_sync_tick_us(touchscreen, now_us));
    sc_touchscreen_uhid_commit_or_defer(touchscreen);
    return true;
}

bool
sc_touchscreen_uhid_pointer_release(struct sc_touchscreen_uhid *touchscreen,
                                    uint64_t pointer_id,
                                    uint16_t x, uint16_t y,
                                    float pressure_value,
                                    uint16_t touch_major, uint16_t touch_minor,
                                    uint16_t azimuth_value) {
    int slot = sc_touchscreen_uhid_find_slot(touchscreen, pointer_id, false);
    uint16_t width;
    uint16_t height;
    uint16_t azimuth;

    (void) pressure_value;
    if (slot < 0) {
        LOGW("Ignoring touch UP: pointer_id %" PRIu64 " not active",
             pointer_id);
        return false;
    }

    width = touch_major ? touch_major : touchscreen->touch_major;
    height = touch_minor ? touch_minor : touchscreen->touch_minor;
    azimuth = sc_touchscreen_uhid_pick_oriented_azimuth(touchscreen,
                                                        azimuth_value,
                                                        slot, x, y,
                                                        touchscreen->azimuth);

    sc_hid_touchscreen_release_contact(&touchscreen->hid, (unsigned) slot,
                                       touchscreen->slots[slot].contact_id,
                                       x, y, width, height, azimuth);
    touchscreen->slots[slot].pending_release = true;
    touchscreen->slots[slot].finalize_pending_commit = false;
    sc_touch_simulation_mark_release(&touchscreen->sampling_slots[slot],
                                     &touchscreen->sim_runtime,
                                     sc_touchscreen_uhid_pick_sync_tick_us(
                                             touchscreen,
                                             sc_touch_simulation_now_us()));
    sc_touchscreen_uhid_commit_or_defer(touchscreen);
    return true;
}

bool
sc_touchscreen_uhid_finalize_pointer(struct sc_touchscreen_uhid *touchscreen,
                                     uint64_t pointer_id) {
    int slot = sc_touchscreen_uhid_find_slot(touchscreen, pointer_id, true);
    if (slot < 0) {
        return false;
    }

    touchscreen->slots[slot].finalize_pending_commit = true;

    if (!touchscreen->slots[slot].pending_release
            || !touchscreen->explicit_frame_depth) {
        sc_hid_touchscreen_clear_contact(&touchscreen->hid, (unsigned) slot);
        if (touchscreen->slots[slot].pending_release) {
            touchscreen->slots[slot].pending_release = false;
            sc_touch_simulation_clear_slot(&touchscreen->sampling_slots[slot]);
        }
        sc_touchscreen_uhid_commit_or_defer(touchscreen);
    }

    return true;
}

static bool
sc_touchscreen_uhid_mark_all_releasing(struct sc_touchscreen_uhid *touchscreen,
                                       uint64_t now_us) {
    bool changed = false;

    for (unsigned i = 0; i < SC_HID_TOUCHSCREEN_CONTACTS; ++i) {
        if (!touchscreen->slots[i].active) {
            continue;
        }

        const struct sc_hid_touchscreen_contact *contact =
            &touchscreen->hid.contacts[i];
        if (contact->present) {
            sc_hid_touchscreen_release_contact(&touchscreen->hid, i,
                                               contact->contact_id,
                                               contact->x, contact->y,
                                               contact->width, contact->height,
                                               contact->azimuth);
        }
        touchscreen->slots[i].pending_release = true;
        touchscreen->slots[i].finalize_pending_commit = true;
        sc_touch_simulation_mark_release(&touchscreen->sampling_slots[i],
                                         &touchscreen->sim_runtime, now_us);
        changed = true;
    }

    return changed;
}

void
sc_touchscreen_uhid_clear_all_pointers(struct sc_touchscreen_uhid *touchscreen) {
    if (!touchscreen->explicit_frame_depth) {
        sc_touchscreen_uhid_reset(touchscreen);
        return;
    }

    if (sc_touchscreen_uhid_mark_all_releasing(
            touchscreen, sc_touchscreen_uhid_pick_sync_tick_us(
                    touchscreen, sc_touch_simulation_now_us()))) {
        touchscreen->dirty = true;
    }
}

void
sc_touchscreen_uhid_reset(struct sc_touchscreen_uhid *touchscreen) {
    bool had_active = sc_touchscreen_uhid_mark_all_releasing(
            touchscreen, sc_touch_simulation_now_us());

    touchscreen->explicit_frame_depth = 0;
    touchscreen->sync_tick_us = 0;
    touchscreen->dirty = false;
    if (had_active && !sc_touchscreen_uhid_flush_now(touchscreen)) {
        LOGW("Could not send touchscreen release frame during reset");
    }

    memset(touchscreen->slots, 0, sizeof(touchscreen->slots));
    memset(touchscreen->sampling_slots, 0, sizeof(touchscreen->sampling_slots));
    sc_hid_touchscreen_clear_all(&touchscreen->hid);
    touchscreen->explicit_frame_depth = 0;
    touchscreen->sync_tick_us = 0;
    touchscreen->dirty = false;
    touchscreen->next_contact_id = 1;

    if (!sc_touchscreen_uhid_commit(touchscreen)) {
        LOGW("Could not reset touchscreen state");
    }
}

void
sc_touchscreen_uhid_set_default_contact_profile(
        struct sc_touchscreen_uhid *touchscreen,
        uint16_t width, uint16_t height, uint16_t azimuth) {
    if (width || height) {
        sc_touch_simulation_config_set_default_contact_profile(
                &touchscreen->sim_config, width, height);
        if (width) {
            touchscreen->touch_major = width;
        }
        if (height) {
            touchscreen->touch_minor = height;
        }
    }
    if (azimuth) {
        touchscreen->azimuth = azimuth > 18000 ? 18000 : azimuth;
    }
}

void
sc_touchscreen_uhid_set_simulation_config(
        struct sc_touchscreen_uhid *touchscreen,
        const struct sc_touch_simulation_config *config) {
    assert(config);
    touchscreen->sim_config = *config;
    sc_touch_simulation_config_build_runtime(&touchscreen->sim_config,
                                             &touchscreen->sim_runtime);
    if (config->touch_major_default) {
        touchscreen->touch_major = config->touch_major_default;
    }
    if (config->touch_minor_default) {
        touchscreen->touch_minor = config->touch_minor_default;
    }
}

const struct sc_touch_simulation_config *
sc_touchscreen_uhid_get_simulation_config(
        const struct sc_touchscreen_uhid *touchscreen) {
    return &touchscreen->sim_config;
}

void
sc_touchscreen_uhid_set_pressure_scale(
        struct sc_touchscreen_uhid *touchscreen, float pressure_scale) {
    sc_touch_simulation_config_set_pressure_scale(&touchscreen->sim_config,
                                                  pressure_scale);
}

void
sc_touchscreen_uhid_set_orientation_mode(
        struct sc_touchscreen_uhid *touchscreen,
        enum sc_touch_orientation_mode orientation_mode) {
    sc_touch_simulation_config_set_orientation_mode(&touchscreen->sim_config,
                                                    orientation_mode);
}

void
sc_touchscreen_uhid_set_motion_profile(
        struct sc_touchscreen_uhid *touchscreen,
        enum sc_touch_motion_profile profile) {
    struct sc_touch_simulation_config config =
        sc_touch_simulation_config_default(profile);
    sc_touchscreen_uhid_set_simulation_config(touchscreen, &config);
    sc_touch_simulation_log_config("[touch-sim] profile applied",
                                   &touchscreen->sim_config,
                                   &touchscreen->sim_runtime);
}

enum sc_touch_motion_profile
sc_touchscreen_uhid_get_motion_profile(
        const struct sc_touchscreen_uhid *touchscreen) {
    return touchscreen->sim_config.motion_profile;
}

bool
sc_touchscreen_uhid_init(struct sc_touchscreen_uhid *touchscreen,
                         struct sc_controller *controller) {
    struct sc_hid_open hid_open;
    struct sc_control_msg msg;

    memset(touchscreen, 0, sizeof(*touchscreen));
    touchscreen->controller = controller;
    touchscreen->next_contact_id = 1;
    touchscreen->touch_major = 1000;
    touchscreen->touch_minor = 1400;
    touchscreen->azimuth = 9000;
    touchscreen->sim_config = sc_touch_simulation_config_default(
            SC_TOUCH_MOTION_PROFILE_NATURAL);
    sc_touch_simulation_config_build_runtime(&touchscreen->sim_config,
                                             &touchscreen->sim_runtime);
    sc_hid_touchscreen_init(&touchscreen->hid);

    sc_hid_touchscreen_generate_open(&hid_open);
    assert(hid_open.hid_id == SC_HID_ID_TOUCHSCREEN);

    msg.type = SC_CONTROL_MSG_TYPE_UHID_CREATE;
    msg.uhid_create.id = hid_open.hid_id;
    msg.uhid_create.vendor_id = 0x06cb;
    msg.uhid_create.product_id = 0x0006;
    msg.uhid_create.name = SC_TOUCHSCREEN_NAME;
    msg.uhid_create.report_desc = hid_open.report_desc;
    msg.uhid_create.report_desc_size = (uint16_t) hid_open.report_desc_size;

    if (!sc_controller_push_msg(controller, &msg)) {
        LOGE("Could not push UHID_CREATE message (touchscreen)");
        return false;
    }

    sc_touch_simulation_log_config("[touch-sim] sampler+sync scaffold enabled",
                                   &touchscreen->sim_config,
                                   &touchscreen->sim_runtime);
    sc_touchscreen_uhid_test_schedule(touchscreen);
    return true;
}
