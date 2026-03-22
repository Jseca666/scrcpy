#ifndef SC_TOUCHSCREEN_UHID_H
#define SC_TOUCHSCREEN_UHID_H

#include "common.h"
#include "controller.h"
#include "hid/hid_touchscreen.h"

#define SC_TOUCHSCREEN_CONTACTS SC_HID_TOUCHSCREEN_CONTACTS

struct sc_touchscreen_uhid {
    struct sc_controller *controller;
    struct sc_hid_touchscreen hid;
};

bool
sc_touchscreen_uhid_init(struct sc_touchscreen_uhid *touchscreen,
                         struct sc_controller *controller);

void
sc_touchscreen_uhid_set_contact(struct sc_touchscreen_uhid *touchscreen,
                                unsigned index, bool active,
                                uint16_t contact_id,
                                uint16_t x, uint16_t y,
                                uint16_t width, uint16_t height,
                                uint8_t pressure, uint16_t azimuth);

void
sc_touchscreen_uhid_clear_contact(struct sc_touchscreen_uhid *touchscreen,
                                  unsigned index);

void
sc_touchscreen_uhid_clear_all(struct sc_touchscreen_uhid *touchscreen);

bool
sc_touchscreen_uhid_commit(struct sc_touchscreen_uhid *touchscreen);

#endif