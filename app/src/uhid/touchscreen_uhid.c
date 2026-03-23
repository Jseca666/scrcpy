#include "touchscreen_uhid.h"

#include <assert.h>
#include <inttypes.h>
#include <string.h>

#include "control_msg.h"
#include "hid/hid_event.h"
#include "touchscreen_uhid_test.h"
#include "util/log.h"

static const char *SC_TOUCHSCREEN_NAME = "Synaptics_ts";

static void
sc_touchscreen_uhid_commit_or_defer(struct sc_touchscreen_uhid *touchscreen);

static void
sc_touchscreen_uhid_begin_touch_update(struct sc_touch_processor *tp);

static void
sc_touchscreen_uhid_end_touch_update(struct sc_touch_processor *tp);

static void
sc_touchscreen_uhid_finalize_releases(struct sc_touchscreen_uhid *touchscreen);

static bool
sc_touchscreen_uhid_commit_and_finalize(struct sc_touchscreen_uhid *touchscreen,
                                        bool from_deferred_flush);

static void
sc_touchscreen_phasea_reset_batch_counters(struct sc_touchscreen_uhid *touchscreen) {
    touchscreen->current_batch_unique_slots = 0;
    touchscreen->current_batch_redundant_slot_updates = 0;
    touchscreen->current_batch_event_count = 0;
    memset(touchscreen->current_batch_slot_updates, 0,
           sizeof(touchscreen->current_batch_slot_updates));
}

static void
sc_touchscreen_phasea_note_slot_update(struct sc_touchscreen_uhid *touchscreen,
                                       unsigned slot) {
    assert(slot < SC_HID_TOUCHSCREEN_CONTACTS);

    if (touchscreen->update_depth == 0) {
        return;
    }

    ++touchscreen->current_batch_event_count;
    ++touchscreen->phasea_stats.batch_slot_updates_total;

    if (!touchscreen->current_batch_slot_updates[slot]) {
        ++touchscreen->current_batch_unique_slots;
        ++touchscreen->phasea_stats.batch_unique_slots_total;
    } else {
        ++touchscreen->current_batch_redundant_slot_updates;
        ++touchscreen->phasea_stats.batch_redundant_slot_updates_total;
    }

    if (touchscreen->current_batch_unique_slots
            > touchscreen->phasea_stats.max_batch_unique_slots) {
        touchscreen->phasea_stats.max_batch_unique_slots =
            touchscreen->current_batch_unique_slots;
    }

    if (touchscreen->current_batch_redundant_slot_updates
            > touchscreen->phasea_stats.max_batch_redundant_slot_updates) {
        touchscreen->phasea_stats.max_batch_redundant_slot_updates =
            touchscreen->current_batch_redundant_slot_updates;
    }

    ++touchscreen->current_batch_slot_updates[slot];
}

static void
sc_touchscreen_phasea_log_batch_summary(struct sc_touchscreen_uhid *touchscreen,
                                        bool flushed) {
    LOGI("[ts-phaseA] batch=%" PRIu32 " events=%u unique_slots=%u redundant_updates=%u dirty=%s flushed=%s",
         touchscreen->batch_seq,
         touchscreen->current_batch_event_count,
         touchscreen->current_batch_unique_slots,
         touchscreen->current_batch_redundant_slot_updates,
         touchscreen->dirty ? "true" : "false",
         flushed ? "true" : "false");
}

static void
sc_touchscreen_phasea_log_reset_summary(struct sc_touchscreen_uhid *touchscreen,
                                        unsigned active_slots_before_reset,
                                        bool sent_release_frame) {
    LOGI("[ts-phaseA] reset active_slots=%u sent_release_frame=%s total_commits=%" PRIu64 " deferred_flushes=%" PRIu64,
         active_slots_before_reset,
         sent_release_frame ? "true" : "false",
         touchscreen->phasea_stats.commit_sent,
         touchscreen->phasea_stats.deferred_flushes);
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
    if (value > 18000) {
        return 18000;
    }
    return value;
}

static int
sc_touchscreen_uhid_find_slot_by_pointer_id(
        struct sc_touchscreen_uhid *touchscreen,
        uint64_t pointer_id) {
    for (unsigned i = 0; i < SC_HID_TOUCHSCREEN_CONTACTS; ++i) {
        if (touchscreen->slots[i].active
                && !touchscreen->slots[i].pending_release
                && touchscreen->slots[i].pointer_id == pointer_id) {
            return (int) i;
        }
    }
    return -1;
}

static int
sc_touchscreen_uhid_find_free_slot(struct sc_touchscreen_uhid *touchscreen) {
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
    int slot = sc_touchscreen_uhid_find_slot_by_pointer_id(touchscreen,
                                                           pointer_id);
    if (slot >= 0) {
        ++touchscreen->phasea_stats.slot_reuses;
        return slot;
    }

    slot = sc_touchscreen_uhid_find_free_slot(touchscreen);
    if (slot < 0) {
        return -1;
    }

    struct sc_touchscreen_slot *s = &touchscreen->slots[slot];
    s->active = true;
    s->pending_release = false;
    s->pointer_id = pointer_id;
    s->contact_id = touchscreen->next_contact_id++;
    if (!s->contact_id) {
        s->contact_id = touchscreen->next_contact_id++;
    }

    ++touchscreen->phasea_stats.slot_assignments;
    return slot;
}

static void
sc_touchscreen_uhid_release_slot(struct sc_touchscreen_uhid *touchscreen,
                                 unsigned slot) {
    assert(slot < SC_HID_TOUCHSCREEN_CONTACTS);
    memset(&touchscreen->slots[slot], 0, sizeof(touchscreen->slots[slot]));
    ++touchscreen->phasea_stats.slot_releases;
}

static uint8_t
sc_touchscreen_uhid_normalize_pressure(float pressure) {
    if (pressure <= 0.0f) {
        return 1;
    }
    if (pressure >= 1.0f) {
        return 100;
    }
    return (uint8_t) (pressure * 100.0f);
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

static void
sc_touchscreen_uhid_set_contact(struct sc_touchscreen_uhid *touchscreen,
                                unsigned index,
                                uint16_t contact_id,
                                uint16_t x, uint16_t y,
                                uint16_t width, uint16_t height,
                                uint8_t pressure,
                                uint16_t azimuth) {
    sc_hid_touchscreen_set_contact(&touchscreen->hid, index, contact_id,
                                   x, y, width, height, pressure, azimuth);
}

static void
sc_touchscreen_uhid_release_contact(struct sc_touchscreen_uhid *touchscreen,
                                    unsigned index,
                                    uint16_t contact_id,
                                    uint16_t x, uint16_t y,
                                    uint16_t width, uint16_t height,
                                    uint16_t azimuth) {
    sc_hid_touchscreen_release_contact(&touchscreen->hid, index, contact_id,
                                       x, y, width, height, azimuth);
}

static void
sc_touchscreen_uhid_clear_contact(struct sc_touchscreen_uhid *touchscreen,
                                  unsigned index) {
    sc_hid_touchscreen_clear_contact(&touchscreen->hid, index);
}

static void
sc_touchscreen_uhid_clear_all(struct sc_touchscreen_uhid *touchscreen) {
    sc_hid_touchscreen_clear_all(&touchscreen->hid);
}

static bool
sc_touchscreen_uhid_commit(struct sc_touchscreen_uhid *touchscreen) {
    struct sc_hid_input hid_input;
    sc_hid_touchscreen_generate_input(&touchscreen->hid, &hid_input);
    return sc_touchscreen_uhid_send_input(touchscreen, &hid_input);
}

static void
sc_touchscreen_uhid_finalize_releases(struct sc_touchscreen_uhid *touchscreen) {
    for (unsigned i = 0; i < SC_HID_TOUCHSCREEN_CONTACTS; ++i) {
        if (touchscreen->slots[i].pending_release) {
            sc_touchscreen_uhid_clear_contact(touchscreen, i);
            sc_touchscreen_uhid_release_slot(touchscreen, i);
        }
    }
}

static bool
sc_touchscreen_uhid_commit_and_finalize(struct sc_touchscreen_uhid *touchscreen,
                                        bool from_deferred_flush) {
    ++touchscreen->phasea_stats.commit_requested;

    if (!sc_touchscreen_uhid_commit(touchscreen)) {
        return false;
    }

    ++touchscreen->phasea_stats.commit_sent;
    if (from_deferred_flush) {
        ++touchscreen->phasea_stats.deferred_flushes;
    }

    sc_touchscreen_uhid_finalize_releases(touchscreen);
    touchscreen->dirty = false;
    return true;
}

static void
sc_touchscreen_uhid_begin_touch_update(struct sc_touch_processor *tp) {
    struct sc_touchscreen_uhid *touchscreen = container_of(tp,
            struct sc_touchscreen_uhid, touch_processor);

    if (touchscreen->update_depth == 0) {
        ++touchscreen->batch_seq;
        ++touchscreen->phasea_stats.batches_started;
        sc_touchscreen_phasea_reset_batch_counters(touchscreen);
    }

    ++touchscreen->update_depth;
}

static void
sc_touchscreen_uhid_end_touch_update(struct sc_touch_processor *tp) {
    struct sc_touchscreen_uhid *touchscreen = container_of(tp,
            struct sc_touchscreen_uhid, touch_processor);

    if (!touchscreen->update_depth) {
        LOGW("Ignoring unmatched end_touch_update()");
        return;
    }

    --touchscreen->update_depth;
    if (touchscreen->update_depth != 0) {
        return;
    }

    ++touchscreen->phasea_stats.batches_finished;

    if (touchscreen->dirty) {
        if (!sc_touchscreen_uhid_commit_and_finalize(touchscreen, true)) {
            LOGW("Could not flush deferred touchscreen state");
            return;
        }
        ++touchscreen->phasea_stats.batches_flushed;
        sc_touchscreen_phasea_log_batch_summary(touchscreen, true);
    } else {
        ++touchscreen->phasea_stats.batches_no_flush;
        sc_touchscreen_phasea_log_batch_summary(touchscreen, false);
    }
}

static void
sc_touchscreen_uhid_process_touch(struct sc_touch_processor *tp,
                                  const struct sc_touch_event *event) {
    struct sc_touchscreen_uhid *touchscreen = container_of(tp,
            struct sc_touchscreen_uhid, touch_processor);

    int slot;
    uint16_t x = (uint16_t) event->position.point.x;
    uint16_t y = (uint16_t) event->position.point.y;
    uint16_t width = sc_touchscreen_uhid_pick_size(event->touch_major,
                                                   touchscreen->touch_major);
    uint16_t height = sc_touchscreen_uhid_pick_size(event->touch_minor,
                                                    touchscreen->touch_minor);
    uint16_t azimuth = sc_touchscreen_uhid_pick_azimuth(event->azimuth,
                                                        touchscreen->azimuth);
    uint8_t pressure = sc_touchscreen_uhid_normalize_pressure(event->pressure);

    ++touchscreen->phasea_stats.touch_events_total;

    switch (event->action) {
        case SC_TOUCH_ACTION_DOWN:
            ++touchscreen->phasea_stats.down_events;
            slot = sc_touchscreen_uhid_acquire_slot(touchscreen,
                                                    event->pointer_id);
            if (slot < 0) {
                LOGW("Ignoring touch DOWN: no free touchscreen slot");
                return;
            }
            sc_touchscreen_phasea_note_slot_update(touchscreen, (unsigned) slot);
            sc_touchscreen_uhid_set_contact(touchscreen, (unsigned) slot,
                                            touchscreen->slots[slot].contact_id,
                                            x, y, width, height, pressure,
                                            azimuth);
            sc_touchscreen_uhid_commit_or_defer(touchscreen);
            break;
        case SC_TOUCH_ACTION_MOVE:
            ++touchscreen->phasea_stats.move_events;
            slot = sc_touchscreen_uhid_find_slot_by_pointer_id(touchscreen,
                                                               event->pointer_id);
            if (slot < 0) {
                LOGW("Ignoring touch MOVE: pointer_id %" PRIu64 " not active",
                     event->pointer_id);
                return;
            }
            sc_touchscreen_phasea_note_slot_update(touchscreen, (unsigned) slot);
            sc_touchscreen_uhid_set_contact(touchscreen, (unsigned) slot,
                                            touchscreen->slots[slot].contact_id,
                                            x, y, width, height, pressure,
                                            azimuth);
            sc_touchscreen_uhid_commit_or_defer(touchscreen);
            break;
        case SC_TOUCH_ACTION_UP:
            ++touchscreen->phasea_stats.up_events;
            slot = sc_touchscreen_uhid_find_slot_by_pointer_id(touchscreen,
                                                               event->pointer_id);
            if (slot < 0) {
                LOGW("Ignoring touch UP: pointer_id %" PRIu64 " not active",
                     event->pointer_id);
                return;
            }
            sc_touchscreen_phasea_note_slot_update(touchscreen, (unsigned) slot);
            ++touchscreen->phasea_stats.release_frames;
            sc_touchscreen_uhid_release_contact(touchscreen, (unsigned) slot,
                                                touchscreen->slots[slot].contact_id,
                                                x, y, width, height, azimuth);
            touchscreen->slots[slot].pending_release = true;
            sc_touchscreen_uhid_commit_or_defer(touchscreen);
            break;
        default:
            LOGW("Ignoring unknown touch action");
            break;
    }
}

static const struct sc_touch_processor_ops sc_touchscreen_uhid_touch_processor_ops = {
    .process_touch = sc_touchscreen_uhid_process_touch,
    .begin_touch_update = sc_touchscreen_uhid_begin_touch_update,
    .end_touch_update = sc_touchscreen_uhid_end_touch_update,
};

void
sc_touchscreen_uhid_reset(struct sc_touchscreen_uhid *touchscreen) {
    bool had_active = false;
    unsigned active_slots_before_reset = 0;

    for (unsigned i = 0; i < SC_HID_TOUCHSCREEN_CONTACTS; ++i) {
        if (!touchscreen->slots[i].active) {
            continue;
        }

        ++active_slots_before_reset;

        const struct sc_hid_touchscreen_contact *contact =
            &touchscreen->hid.contacts[i];
        if (!contact->present) {
            continue;
        }

        ++touchscreen->phasea_stats.release_frames;
        ++touchscreen->phasea_stats.reset_release_frames;
        sc_touchscreen_uhid_release_contact(touchscreen, i,
                                            contact->contact_id,
                                            contact->x, contact->y,
                                            contact->width, contact->height,
                                            contact->azimuth);
        touchscreen->slots[i].pending_release = true;
        had_active = true;
    }

    touchscreen->update_depth = 0;
    touchscreen->dirty = false;

    if (had_active) {
        if (!sc_touchscreen_uhid_commit_and_finalize(touchscreen, false)) {
            LOGW("Could not send touchscreen release frame during reset");
        }
    }

    memset(touchscreen->slots, 0, sizeof(touchscreen->slots));
    sc_touchscreen_uhid_clear_all(touchscreen);
    touchscreen->update_depth = 0;
    touchscreen->dirty = false;
    touchscreen->next_contact_id = 1;

    ++touchscreen->phasea_stats.commit_requested;
    if (!sc_touchscreen_uhid_commit(touchscreen)) {
        LOGW("Could not reset touchscreen state");
    } else {
        ++touchscreen->phasea_stats.commit_sent;
    }

    sc_touchscreen_phasea_log_reset_summary(touchscreen,
                                            active_slots_before_reset,
                                            had_active);
}

void
sc_touchscreen_uhid_set_default_contact_profile(
        struct sc_touchscreen_uhid *touchscreen,
        uint16_t width, uint16_t height, uint16_t azimuth) {
    if (width) {
        touchscreen->touch_major = width;
    }
    if (height) {
        touchscreen->touch_minor = height;
    }
    if (azimuth) {
        touchscreen->azimuth = azimuth > 18000 ? 18000 : azimuth;
    }
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

    LOGI("[ts-phaseA] stats enabled on touchscreen_uhid");
    sc_touchscreen_uhid_test_schedule(touchscreen);
    return true;
}

static void
sc_touchscreen_uhid_commit_or_defer(struct sc_touchscreen_uhid *touchscreen) {
    if (touchscreen->update_depth > 0) {
        ++touchscreen->phasea_stats.commit_deferred;
        touchscreen->dirty = true;
        return;
    }

    if (!sc_touchscreen_uhid_commit_and_finalize(touchscreen, false)) {
        LOGW("Could not commit touchscreen state");
        return;
    }
}
