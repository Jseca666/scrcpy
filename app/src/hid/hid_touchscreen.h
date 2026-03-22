#ifndef SC_HID_TOUCHSCREEN_H
#define SC_HID_TOUCHSCREEN_H

#include <stdbool.h>
#include <stdint.h>

#include "common.h"
#include "hid/hid_event.h"

#define SC_HID_ID_TOUCHSCREEN 10
#define SC_HID_TOUCHSCREEN_CONTACTS 10

struct sc_hid_touchscreen_contact {
    bool present;
    bool tip_switch;
    bool in_range;
    bool confidence;
    uint16_t contact_id;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint8_t pressure;
    uint16_t azimuth;
};

struct sc_hid_touchscreen {
    struct sc_hid_touchscreen_contact contacts[SC_HID_TOUCHSCREEN_CONTACTS];
    uint16_t scan_time;
};

void
sc_hid_touchscreen_init(struct sc_hid_touchscreen *hid);

void
sc_hid_touchscreen_generate_open(struct sc_hid_open *hid_open);

void
sc_hid_touchscreen_set_contact(struct sc_hid_touchscreen *hid, unsigned index,
                               uint16_t contact_id,
                               uint16_t x, uint16_t y,
                               uint16_t width, uint16_t height,
                               uint8_t pressure, uint16_t azimuth);

void
sc_hid_touchscreen_release_contact(struct sc_hid_touchscreen *hid,
                                   unsigned index,
                                   uint16_t contact_id,
                                   uint16_t x, uint16_t y,
                                   uint16_t width, uint16_t height,
                                   uint16_t azimuth);

void
sc_hid_touchscreen_clear_contact(struct sc_hid_touchscreen *hid, unsigned index);

void
sc_hid_touchscreen_clear_all(struct sc_hid_touchscreen *hid);

void
sc_hid_touchscreen_generate_input(struct sc_hid_touchscreen *hid,
                                  struct sc_hid_input *hid_input);

#endif
