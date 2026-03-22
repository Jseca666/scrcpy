#ifndef SC_TOUCHSCREEN_UHID_H
#define SC_TOUCHSCREEN_UHID_H

#include "common.h"
#include "controller.h"
#include "hid/hid_touchscreen.h"
#include "trait/touch_processor.h"

struct sc_touchscreen_uhid {
    struct sc_touch_processor touch_processor; // touch processor trait
    struct sc_hid_touchscreen hid;
    struct sc_controller *controller;
};

bool
sc_touchscreen_uhid_init(struct sc_touchscreen_uhid *touchscreen,
                         struct sc_controller *controller);

#endif