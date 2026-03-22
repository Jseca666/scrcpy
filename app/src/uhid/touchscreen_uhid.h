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

    uint16_t touch_major; // 0 表示使用模块默认值
    uint16_t touch_minor; // 0 表示使用模块默认值
    uint16_t azimuth;     // 0 表示使用模块默认值
};

bool
sc_touchscreen_uhid_init(struct sc_touchscreen_uhid *touchscreen,struct sc_controller *controller);

void
sc_touchscreen_uhid_set_default_contact_profile(
        struct sc_touchscreen_uhid *touchscreen,
        uint16_t width, uint16_t height, uint16_t azimuth);
void
sc_touchscreen_uhid_reset(struct sc_touchscreen_uhid *touchscreen);

#endif