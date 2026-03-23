#include "touchscreen_uhid.h"

#include <assert.h>
#include <inttypes.h>
#include <string.h>

#include "control_msg.h"
#include "hid/hid_event.h"
#include "touchscreen_uhid_test.h"
#include "util/log.h"

static const char *SC_TOUCHSCREEN_NAME = "Synaptics_ts";

static void sc_touchscreen_uhid_begin_touch_update(struct sc_touch_processor *tp);
static void sc_touchscreen_uhid_end_touch_update(struct sc_touch_processor *tp);
static void sc_touchscreen_uhid_process_touch_compat(struct sc_touch_processor *tp,
                                                     const struct sc_touch_event *event);

static void
sc_touchscreen_uhid_sync_runtime_from_config(struct sc_touchscreen_uhid *touchscreen) {
    sc_touch_simulation_config_build_runtime(&touchscreen->sim_config,
                                             &touchscreen->sim_runtime);
}

static uint16_t
sc_touchscreen_uhid_next_contact_id(struct sc_touchscreen_uhid *touchscreen) {
    uint16_t id = touchscreen->next_contact_id++;
    if (!id) {
        id = touchscreen->next_contact_id++;
    }
    return id;
}

static int
sc_touchscreen_uhid_find_slot_active(const struct sc_touchscreen_uhid *touchscreen,
                                     uint64_t pointer_id) {
    for (unsigned i = 0; i < SC_HID_TOUCHSCREEN_CONTACTS; ++i) {
        const struct sc_touchscreen_slot *slot = &touchscreen->slots[i];
        if (slot->active
                && !slot->pending_release
                && !slot->finalize_pending_commit
                && slot->pointer_id == pointer_id) {
            return (int) i;
        }
    }
    return -1;
}

static int
sc_touchscreen_uhid_find_slot_any(const struct sc_touchscreen_uhid *touchscreen,
                                  uint64_t pointer_id) {
    for (unsigned i = 0; i < SC_HID_TOUCHSCREEN_CONTACTS; ++i) {
        const struct sc_touchscreen_slot *slot = &touchscreen->slots[i];
        if (slot->active && slot->pointer_id == pointer_id) {
            return (int) i;
        }
    }
    return -1;
}

static int
sc_touchscreen_uhid_find_free_slot(const struct sc_touchscreen_uhid *touchscreen) {
    for (unsigned i = 0; i < SC_HID_TOUCHSCREEN_CONTACTS; ++i) {
        if (!touchscreen->slots[i].active) {
            return (int) i;
        }
    }
    return -1;
}

static int
sc_touchscreen_uhid_acquire_slot(struct sc_touchscreen_uhid *touchscreen,
                                 uint64_t pointer_id) {
    int slot = sc_touchscreen_uhid_find_slot_any(touchscreen, pointer_id);
    if (slot >= 0) {
        touchscreen->slots[slot].pending_release = false;
        touchscreen->slots[slot].finalize_pending_commit = false;
        return slot;
    }

    slot = sc_touchscreen_uhid_find_free_slot(touchscreen);
    if (slot < 0) {
        return -1;
    }

    touchscreen->slots[slot].active = true;
    touchscreen->slots[slot].pending_release = false;
    touchscreen->slots[slot].finalize_pending_commit = false;
    touchscreen->slots[slot].pointer_id = pointer_id;
    touchscreen->slots[slot].contact_id =
        sc_touchscreen_uhid_next_contact_id(touchscreen);
    return slot;
}

static void
sc_touchscreen_uhid_release_slot(struct sc_touchscreen_uhid *touchscreen,
                                 unsigned index) {
    assert(index < SC_HID_TOUCHSCREEN_CONTACTS);
    memset(&touchscreen->slots[index], 0, sizeof(touchscreen->slots[index]));
    sc_touch_simulation_clear_slot(&touchscreen->sampling_slots[index]);
}

static uint16_t
sc_touchscreen_uhid_pick_size(uint16_t value, uint16_t fallback) {
    return value ? value : fallback;
}

static uint16_t
sc_touchscreen_uhid_pick_azimuth(uint16_t value, uint16_t fallback) {
    if (!value) {
        return fallback;
    }
    return value > 18000 ? 18000 : value;
}

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

    int32_t delta = (int32_t) ((int64_t) dy * 9000 / norm);
    int32_t azimuth = 9000 + delta;
    if (azimuth < 0) {
        azimuth = 0;
    } else if (azimuth > 18000) {
        azimuth = 18000;
    }
    return (uint16_t) azimuth;
}

static uint16_t
sc_touchscreen_uhid_pick_oriented_azimuth(const struct sc_touchscreen_uhid *touchscreen,
                                          int slot,
                                          uint16_t x, uint16_t y,
                                          uint16_t event_azimuth,
                                          uint16_t fallback) {
    switch (touchscreen->sim_config.orientation_mode) {
        case SC_TOUCH_ORIENTATION_FIXED:
            return fallback;
        case SC_TOUCH_ORIENTATION_FOLLOW_MOTION:
            if (slot >= 0 && (unsigned) slot < SC_HID_TOUCHSCREEN_CONTACTS) {
                const struct sc_hid_touchscreen_contact *contact =
                    &touchscreen->hid.contacts[slot];
                if (contact->present) {
                    return sc_touchscreen_uhid_map_motion_orientation(
                            contact->x, contact->y, x, y, fallback);
                }
            }
            return sc_touchscreen_uhid_pick_azimuth(event_azimuth, fallback);
        case SC_TOUCH_ORIENTATION_HALF_RANGE:
        default:
            return sc_touchscreen_uhid_pick_azimuth(event_azimuth, fallback);
    }
}

static uint8_t
sc_touchscreen_uhid_normalize_pressure(const struct sc_touchscreen_uhid *touchscreen,
                                       float pressure) {
    float scaled = pressure * touchscreen->sim_config.pressure_scale;
    if (scaled <= 0.0f) {
        return 1;
    }
    if (scaled >= 1.0f) {
        return 100;
    }
    return (uint8_t) (scaled * 100.0f);
}

static bool
sc_touchscreen_uhid_send_input(struct sc_touchscreen_uhid *touchscreen,
                               const struct sc_hid_input *hid_input) {
    assert(hid_input->size <= SC_HID_MAX_SIZE);

    struct sc_control_msg msg;
    msg.type = SC_CONTROL_MSG_TYPE_UHID_INPUT;
    msg.uhid_input.id = hid_input->hid_id;
    memcpy(msg.uhid_input.data, hid_input->data, hid_input->size);
    msg.uhid_input.size = hid_input->size;

    if (!sc_controller_push_msg(touchscreen->controller, &msg)) {
        LOGE("Could not push UHID_INPUT message (touchscreen)");
        return false;
    }

    return true;
}

static bool
sc_touchscreen_uhid_commit(struct sc_touchscreen_uhid *touchscreen) {
    struct sc_hid_input hid_input;
    sc_hid_touchscreen_generate_input(&touchscreen->hid, &hid_input);
    return sc_touchscreen_uhid_send_input(touchscreen, &hid_input);
}

static void
sc_touchscreen_uhid_post_commit_cleanup(struct sc_touchscreen_uhid *touchscreen) {
    for (unsigned i = 0; i < SC_HID_TOUCHSCREEN_CONTACTS; ++i) {
        if (touchscreen->slots[i].active
                && touchscreen->slots[i].finalize_pending_commit) {
            sc_touchscreen_uhid_release_slot(touchscreen, i);
        }
    }
}

static bool
sc_touchscreen_uhid_flush_now(struct sc_touchscreen_uhid *touchscreen) {
    if (!touchscreen->dirty) {
        return true;
    }
    if (!sc_touchscreen_uhid_commit(touchscreen)) {
        return false;
    }
    sc_touchscreen_uhid_post_commit_cleanup(touchscreen);
    touchscreen->dirty = false;
    return true;
}

static void
sc_touchscreen_uhid_mark_dirty(struct sc_touchscreen_uhid *touchscreen) {
    touchscreen->dirty = true;
}

static void
sc_touchscreen_uhid_commit_or_defer(struct sc_touchscreen_uhid *touchscreen) {
    sc_touchscreen_uhid_mark_dirty(touchscreen);
    if (touchscreen->explicit_frame_depth || touchscreen->update_depth) {
        return;
    }
    if (!sc_touchscreen_uhid_flush_now(touchscreen)) {
        LOGW("Could not commit touchscreen state");
    }
}

static void
sc_touchscreen_uhid_begin_touch_update(struct sc_touch_processor *tp) {
    struct sc_touchscreen_uhid *touchscreen =
        container_of(tp, struct sc_touchscreen_uhid, touch_processor);
    ++touchscreen->update_depth;
}

static void
sc_touchscreen_uhid_end_touch_update(struct sc_touch_processor *tp) {
    struct sc_touchscreen_uhid *touchscreen =
        container_of(tp, struct sc_touchscreen_uhid, touch_processor);

    if (!touchscreen->update_depth) {
        LOGW("Ignoring unmatched end_touch_update()");
        return;
    }

    --touchscreen->update_depth;
    if (!touchscreen->update_depth && !touchscreen->explicit_frame_depth) {
        if (!sc_touchscreen_uhid_flush_now(touchscreen)) {
            LOGW("Could not flush deferred touchscreen update");
        }
    }
}

static void
sc_touchscreen_uhid_process_touch_compat(struct sc_touch_processor *tp,
                                         const struct sc_touch_event *event) {
    struct sc_touchscreen_uhid *touchscreen =
        container_of(tp, struct sc_touchscreen_uhid, touch_processor);

    uint16_t x = (uint16_t) event->position.point.x;
    uint16_t y = (uint16_t) event->position.point.y;

    switch (event->action) {
        case SC_TOUCH_ACTION_DOWN:
            (void) sc_touchscreen_uhid_pointer_down(touchscreen,
                                                    event->pointer_id,
                                                    x, y,
                                                    event->pressure,
                                                    event->touch_major,
                                                    event->touch_minor,
                                                    event->azimuth);
            break;
        case SC_TOUCH_ACTION_MOVE:
            (void) sc_touchscreen_uhid_pointer_move(touchscreen,
                                                    event->pointer_id,
                                                    x, y,
                                                    event->pressure,
                                                    event->touch_major,
                                                    event->touch_minor,
                                                    event->azimuth);
            break;
        case SC_TOUCH_ACTION_UP:
            (void) sc_touchscreen_uhid_end_pointer(touchscreen,
                                                   event->pointer_id,
                                                   x, y,
                                                   event->pressure,
                                                   event->touch_major,
                                                   event->touch_minor,
                                                   event->azimuth);
            break;
        default:
            LOGW("Ignoring unknown touch action");
            break;
    }
}

static const struct sc_touch_processor_ops sc_touchscreen_uhid_touch_processor_ops = {
    .process_touch = sc_touchscreen_uhid_process_touch_compat,
    .begin_touch_update = sc_touchscreen_uhid_begin_touch_update,
    .end_touch_update = sc_touchscreen_uhid_end_touch_update,
};

void
sc_touchscreen_uhid_begin_touch_frame(struct sc_touchscreen_uhid *touchscreen) {
    ++touchscreen->explicit_frame_depth;
}

void
sc_touchscreen_uhid_end_touch_frame(struct sc_touchscreen_uhid *touchscreen) {
    if (!touchscreen->explicit_frame_depth) {
        LOGW("Ignoring unmatched end_touch_frame()");
        return;
    }

    --touchscreen->explicit_frame_depth;
    if (!touchscreen->explicit_frame_depth && !touchscreen->update_depth) {
        if (!sc_touchscreen_uhid_flush_now(touchscreen)) {
            LOGW("Could not flush explicit touchscreen frame");
        }
    }
}

bool
sc_touchscreen_uhid_ensure_pointer(struct sc_touchscreen_uhid *touchscreen,
                                   uint64_t pointer_id) {
    return sc_touchscreen_uhid_acquire_slot(touchscreen, pointer_id) >= 0;
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

    uint16_t width = sc_touchscreen_uhid_pick_size(touch_major, touchscreen->touch_major);
    uint16_t height = sc_touchscreen_uhid_pick_size(touch_minor, touchscreen->touch_minor);
    uint16_t azimuth = sc_touchscreen_uhid_pick_oriented_azimuth(
            touchscreen, slot, x, y, azimuth_value, touchscreen->azimuth);
    uint8_t pressure = sc_touchscreen_uhid_normalize_pressure(touchscreen, pressure_value);

    sc_hid_touchscreen_set_contact(&touchscreen->hid, (unsigned) slot,
                                   touchscreen->slots[slot].contact_id,
                                   x, y, width, height, pressure, azimuth);

    touchscreen->slots[slot].pending_release = false;
    touchscreen->slots[slot].finalize_pending_commit = false;
    sc_touch_simulation_mark_emit(&touchscreen->sampling_slots[slot],
                                  x, y, width, height, pressure, azimuth,
                                  sc_touch_simulation_now_us());
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
    int slot = sc_touchscreen_uhid_find_slot_active(touchscreen, pointer_id);
    if (slot < 0) {
        LOGW("Ignoring touch MOVE: pointer_id %" PRIu64 " not active", pointer_id);
        return false;
    }

    uint16_t width = sc_touchscreen_uhid_pick_size(touch_major, touchscreen->touch_major);
    uint16_t height = sc_touchscreen_uhid_pick_size(touch_minor, touchscreen->touch_minor);
    uint16_t azimuth = sc_touchscreen_uhid_pick_oriented_azimuth(
            touchscreen, slot, x, y, azimuth_value, touchscreen->azimuth);
    uint8_t pressure = sc_touchscreen_uhid_normalize_pressure(touchscreen, pressure_value);

    sc_hid_touchscreen_set_contact(&touchscreen->hid, (unsigned) slot,
                                   touchscreen->slots[slot].contact_id,
                                   x, y, width, height, pressure, azimuth);
    sc_touch_simulation_mark_emit(&touchscreen->sampling_slots[slot],
                                  x, y, width, height, pressure, azimuth,
                                  sc_touch_simulation_now_us());
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
    (void) pressure_value;
    int slot = sc_touchscreen_uhid_find_slot_active(touchscreen, pointer_id);
    if (slot < 0) {
        LOGW("Ignoring touch RELEASE: pointer_id %" PRIu64 " not active", pointer_id);
        return false;
    }

    uint16_t width = sc_touchscreen_uhid_pick_size(touch_major, touchscreen->touch_major);
    uint16_t height = sc_touchscreen_uhid_pick_size(touch_minor, touchscreen->touch_minor);
    uint16_t azimuth = sc_touchscreen_uhid_pick_oriented_azimuth(
            touchscreen, slot, x, y, azimuth_value, touchscreen->azimuth);

    sc_hid_touchscreen_release_contact(&touchscreen->hid, (unsigned) slot,
                                       touchscreen->slots[slot].contact_id,
                                       x, y, width, height, azimuth);
    touchscreen->slots[slot].pending_release = true;
    touchscreen->slots[slot].finalize_pending_commit = false;
    sc_touch_simulation_mark_release(&touchscreen->sampling_slots[slot],
                                     &touchscreen->sim_runtime,
                                     sc_touch_simulation_now_us());
    sc_touchscreen_uhid_commit_or_defer(touchscreen);
    return true;
}

bool
sc_touchscreen_uhid_finalize_pointer(struct sc_touchscreen_uhid *touchscreen,
                                     uint64_t pointer_id) {
    int slot = sc_touchscreen_uhid_find_slot_any(touchscreen, pointer_id);
    if (slot < 0) {
        return false;
    }

    sc_hid_touchscreen_clear_contact(&touchscreen->hid, (unsigned) slot);
    touchscreen->slots[slot].pending_release = false;
    touchscreen->slots[slot].finalize_pending_commit = true;
    sc_touchscreen_uhid_commit_or_defer(touchscreen);
    return true;
}

bool
sc_touchscreen_uhid_end_pointer(struct sc_touchscreen_uhid *touchscreen,
                                uint64_t pointer_id,
                                uint16_t x, uint16_t y,
                                float pressure_value,
                                uint16_t touch_major, uint16_t touch_minor,
                                uint16_t azimuth_value) {
    if (!sc_touchscreen_uhid_pointer_release(touchscreen, pointer_id,
                                             x, y,
                                             pressure_value,
                                             touch_major, touch_minor,
                                             azimuth_value)) {
        return false;
    }
    return sc_touchscreen_uhid_finalize_pointer(touchscreen, pointer_id);
}

void
sc_touchscreen_uhid_clear_all_pointers(struct sc_touchscreen_uhid *touchscreen) {
    if (!touchscreen->explicit_frame_depth && !touchscreen->update_depth) {
        sc_touchscreen_uhid_reset(touchscreen);
        return;
    }

    sc_hid_touchscreen_clear_all(&touchscreen->hid);
    for (unsigned i = 0; i < SC_HID_TOUCHSCREEN_CONTACTS; ++i) {
        if (touchscreen->slots[i].active) {
            touchscreen->slots[i].pending_release = false;
            touchscreen->slots[i].finalize_pending_commit = true;
        }
    }
    sc_touchscreen_uhid_commit_or_defer(touchscreen);
}

void
sc_touchscreen_uhid_reset(struct sc_touchscreen_uhid *touchscreen) {
    bool had_contacts = false;

    for (unsigned i = 0; i < SC_HID_TOUCHSCREEN_CONTACTS; ++i) {
        if (touchscreen->slots[i].active && touchscreen->hid.contacts[i].present) {
            had_contacts = true;
            sc_hid_touchscreen_release_contact(&touchscreen->hid, i,
                                               touchscreen->slots[i].contact_id,
                                               touchscreen->hid.contacts[i].x,
                                               touchscreen->hid.contacts[i].y,
                                               touchscreen->hid.contacts[i].width,
                                               touchscreen->hid.contacts[i].height,
                                               touchscreen->hid.contacts[i].azimuth);
        }
    }

    if (had_contacts) {
        (void) sc_touchscreen_uhid_commit(touchscreen);
    }

    sc_hid_touchscreen_clear_all(&touchscreen->hid);
    memset(touchscreen->slots, 0, sizeof(touchscreen->slots));
    memset(touchscreen->sampling_slots, 0, sizeof(touchscreen->sampling_slots));
    touchscreen->next_contact_id = 1;
    touchscreen->update_depth = 0;
    touchscreen->explicit_frame_depth = 0;
    touchscreen->dirty = false;

    if (!sc_touchscreen_uhid_commit(touchscreen)) {
        LOGW("Could not reset touchscreen state");
    }
}

void
sc_touchscreen_uhid_set_default_contact_profile(struct sc_touchscreen_uhid *touchscreen,
                                                uint16_t width,
                                                uint16_t height,
                                                uint16_t azimuth) {
    if (width || height) {
        sc_touch_simulation_config_set_default_contact_profile(&touchscreen->sim_config,
                                                               width, height);
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
    sc_touchscreen_uhid_sync_runtime_from_config(touchscreen);
}

void
sc_touchscreen_uhid_set_simulation_config(struct sc_touchscreen_uhid *touchscreen,
                                          const struct sc_touch_simulation_config *config) {
    assert(config);
    touchscreen->sim_config = *config;
    sc_touchscreen_uhid_sync_runtime_from_config(touchscreen);
    if (config->touch_major_default) {
        touchscreen->touch_major = config->touch_major_default;
    }
    if (config->touch_minor_default) {
        touchscreen->touch_minor = config->touch_minor_default;
    }
}

const struct sc_touch_simulation_config *
sc_touchscreen_uhid_get_simulation_config(const struct sc_touchscreen_uhid *touchscreen) {
    return &touchscreen->sim_config;
}

void
sc_touchscreen_uhid_set_pressure_scale(struct sc_touchscreen_uhid *touchscreen,
                                       float pressure_scale) {
    sc_touch_simulation_config_set_pressure_scale(&touchscreen->sim_config,
                                                  pressure_scale);
    sc_touchscreen_uhid_sync_runtime_from_config(touchscreen);
}

void
sc_touchscreen_uhid_set_orientation_mode(struct sc_touchscreen_uhid *touchscreen,
                                         enum sc_touch_orientation_mode orientation_mode) {
    sc_touch_simulation_config_set_orientation_mode(&touchscreen->sim_config,
                                                    orientation_mode);
    sc_touchscreen_uhid_sync_runtime_from_config(touchscreen);
}

void
sc_touchscreen_uhid_set_motion_profile(struct sc_touchscreen_uhid *touchscreen,
                                       enum sc_touch_motion_profile profile) {
    struct sc_touch_simulation_config config =
        sc_touch_simulation_config_default(profile);
    sc_touchscreen_uhid_set_simulation_config(touchscreen, &config);
    sc_touch_simulation_log_config("[touch-sim] profile applied",
                                   &touchscreen->sim_config,
                                   &touchscreen->sim_runtime);
}

enum sc_touch_motion_profile
sc_touchscreen_uhid_get_motion_profile(const struct sc_touchscreen_uhid *touchscreen) {
    return touchscreen->sim_config.motion_profile;
}

bool
sc_touchscreen_uhid_init(struct sc_touchscreen_uhid *touchscreen,
                         struct sc_controller *controller) {
    memset(touchscreen, 0, sizeof(*touchscreen));
    touchscreen->controller = controller;
    touchscreen->touch_processor.ops = &sc_touchscreen_uhid_touch_processor_ops;
    touchscreen->next_contact_id = 1;
    touchscreen->touch_major = 1000;
    touchscreen->touch_minor = 1400;
    touchscreen->azimuth = 9000;
    touchscreen->sim_config =
        sc_touch_simulation_config_default(SC_TOUCH_MOTION_PROFILE_NATURAL);
    sc_touchscreen_uhid_sync_runtime_from_config(touchscreen);
    sc_hid_touchscreen_init(&touchscreen->hid);

    struct sc_hid_open hid_open;
    sc_hid_touchscreen_generate_open(&hid_open);
    assert(hid_open.hid_id == SC_HID_ID_TOUCHSCREEN);

    struct sc_control_msg msg;
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

    sc_touch_simulation_log_config("[touch-sim] rewritten explicit executor enabled",
                                   &touchscreen->sim_config,
                                   &touchscreen->sim_runtime);

    sc_touchscreen_uhid_test_schedule(touchscreen);
    return true;
}
