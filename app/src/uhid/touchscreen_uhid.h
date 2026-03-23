#ifndef SC_TOUCHSCREEN_UHID_H
#define SC_TOUCHSCREEN_UHID_H

#include "common.h"

#include "controller.h"
#include "hid/hid_touchscreen.h"
#include "trait/touch_processor.h"

#include <stdbool.h>
#include <stdint.h>

struct sc_touchscreen_slot {
    bool active;
    bool pending_release;
    uint64_t pointer_id;
    uint16_t contact_id;
};

struct sc_touchscreen_phasea_stats {
    uint64_t touch_events_total;
    uint64_t down_events;
    uint64_t move_events;
    uint64_t up_events;

    uint64_t commit_requested;
    uint64_t commit_sent;
    uint64_t commit_deferred;
    uint64_t deferred_flushes;

    uint64_t batches_started;
    uint64_t batches_finished;
    uint64_t batches_flushed;
    uint64_t batches_no_flush;

    uint64_t slot_assignments;
    uint64_t slot_reuses;
    uint64_t slot_releases;

    uint64_t release_frames;
    uint64_t reset_release_frames;

    uint64_t batch_slot_updates_total;
    uint64_t batch_unique_slots_total;
    uint64_t batch_redundant_slot_updates_total;

    unsigned max_batch_unique_slots;
    unsigned max_batch_redundant_slot_updates;
};

struct sc_touchscreen_uhid {
    struct sc_touch_processor touch_processor;
    struct sc_hid_touchscreen hid;
    struct sc_controller *controller;

    struct sc_touchscreen_slot slots[SC_HID_TOUCHSCREEN_CONTACTS];
    uint16_t next_contact_id;

    unsigned update_depth;
    bool dirty;

    uint16_t touch_major;
    uint16_t touch_minor;
    uint16_t azimuth;

    struct sc_touchscreen_phasea_stats phasea_stats;
    uint32_t batch_seq;
    unsigned current_batch_unique_slots;
    unsigned current_batch_redundant_slot_updates;
    unsigned current_batch_event_count;
    uint8_t current_batch_slot_updates[SC_HID_TOUCHSCREEN_CONTACTS];
};

bool
sc_touchscreen_uhid_init(struct sc_touchscreen_uhid *touchscreen,
                         struct sc_controller *controller);

void
sc_touchscreen_uhid_set_default_contact_profile(
        struct sc_touchscreen_uhid *touchscreen,
        uint16_t width, uint16_t height, uint16_t azimuth);

void
sc_touchscreen_uhid_reset(struct sc_touchscreen_uhid *touchscreen);

#endif
