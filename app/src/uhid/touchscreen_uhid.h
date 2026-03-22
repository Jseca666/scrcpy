#ifndef SC_TOUCHSCREEN_UHID_H
#define SC_TOUCHSCREEN_UHID_H

#include <stdbool.h>

#include "common.h"
#include "controller.h"
#include "hid/hid_touchscreen.h"
#include "trait/touch_processor.h"

struct sc_touchscreen_slot {
    bool active;
    uint64_t pointer_id;
    uint16_t contact_id;
};

struct sc_touchscreen_uhid {
    struct sc_touch_processor touch_processor;
    struct sc_hid_touchscreen hid;
    struct sc_controller *controller;

    struct sc_touchscreen_slot slots[SC_HID_TOUCHSCREEN_CONTACTS];
    uint16_t next_contact_id;

    unsigned update_depth;
    bool dirty;
};

bool
sc_touchscreen_uhid_init(struct sc_touchscreen_uhid *touchscreen,
                         struct sc_controller *controller);

#endif