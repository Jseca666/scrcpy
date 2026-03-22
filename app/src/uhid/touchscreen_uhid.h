#ifndef SC_TOUCHSCREEN_UHID_UHID_H
#define SC_TOUCHSCREEN_UHID_UHID_H

#include "common.h"
#include "controller.h"

struct sc_touchscreen_uhid {
    struct sc_controller *controller;
};

bool
sc_touchscreen_uhid_init(struct sc_touchscreen_uhid *touchscreen,
                    struct sc_controller *controller);

#endif