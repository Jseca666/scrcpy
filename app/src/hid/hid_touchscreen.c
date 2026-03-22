#include "hid_touchscreen.h"

#include <assert.h>
#include <string.h>

#define SC_TS_REPORT_ID 0x01

#define SC_TS_X_MAX 20639
#define SC_TS_Y_MAX 30959
#define SC_TS_SIZE_MAX 30959

#define SC_TS_FINGER_SIZE 14
#define SC_TS_REPORT_SIZE \
    (1 + SC_HID_TOUCHSCREEN_CONTACTS * SC_TS_FINGER_SIZE + 1 + 2)

#define SC_TS_CONTACT_COUNT_OFFSET \
    (1 + SC_HID_TOUCHSCREEN_CONTACTS * SC_TS_FINGER_SIZE)
#define SC_TS_SCAN_TIME_OFFSET (SC_TS_CONTACT_COUNT_OFFSET + 1)

#define U16(v) (uint8_t) ((v) & 0xFF), (uint8_t) (((v) >> 8) & 0xFF)

#define FINGER_BLOCK(X_MAX, Y_MAX, SIZE_MAX)                              \
    0x09, 0x22,              /* Usage (Finger) */                         \
    0xA1, 0x02,              /* Collection (Logical) */                   \
                                                                          \
      0x09, 0x42,            /* Usage (Tip Switch) */                     \
      0x09, 0x32,            /* Usage (In Range) */                       \
      0x09, 0x47,            /* Usage (Confidence) */                     \
      0x15, 0x00,            /*   Logical Minimum (0) */                  \
      0x25, 0x01,            /*   Logical Maximum (1) */                  \
      0x75, 0x01,            /*   Report Size (1) */                      \
      0x95, 0x03,            /*   Report Count (3) */                     \
      0x81, 0x02,            /*   Input (Data,Var,Abs) */                 \
      0x75, 0x05,            /*   Report Size (5) */                      \
      0x95, 0x01,            /*   Report Count (1) */                     \
      0x81, 0x03,            /*   Input (Const,Var,Abs) */                \
                                                                          \
      0x09, 0x51,            /* Usage (Contact Identifier) */             \
      0x16, 0x00, 0x00,      /*   Logical Minimum (0) */                  \
      0x26, 0xFF, 0xFF,      /*   Logical Maximum (65535) */              \
      0x75, 0x10,            /*   Report Size (16) */                     \
      0x95, 0x01,            /*   Report Count (1) */                     \
      0x81, 0x02,            /*   Input (Data,Var,Abs) */                 \
                                                                          \
      0x05, 0x01,            /* Usage Page (Generic Desktop) */           \
      0x09, 0x30,            /* Usage (X) */                              \
      0x16, 0x00, 0x00,      /*   Logical Minimum (0) */                  \
      0x26, U16(X_MAX),      /*   Logical Maximum */                      \
      0x75, 0x10,            /*   Report Size (16) */                     \
      0x95, 0x01,            /*   Report Count (1) */                     \
      0x81, 0x02,            /*   Input (Data,Var,Abs) */                 \
                                                                          \
      0x09, 0x31,            /* Usage (Y) */                              \
      0x16, 0x00, 0x00,      /*   Logical Minimum (0) */                  \
      0x26, U16(Y_MAX),      /*   Logical Maximum */                      \
      0x75, 0x10,            /*   Report Size (16) */                     \
      0x95, 0x01,            /*   Report Count (1) */                     \
      0x81, 0x02,            /*   Input (Data,Var,Abs) */                 \
                                                                          \
      0x05, 0x0D,            /* Usage Page (Digitizers) */                \
      0x09, 0x48,            /* Usage (Width) */                          \
      0x16, 0x00, 0x00,      /*   Logical Minimum (0) */                  \
      0x26, U16(SIZE_MAX),   /*   Logical Maximum */                      \
      0x75, 0x10,            /*   Report Size (16) */                     \
      0x95, 0x01,            /*   Report Count (1) */                     \
      0x81, 0x02,            /*   Input (Data,Var,Abs) */                 \
                                                                          \
      0x09, 0x49,            /* Usage (Height) */                         \
      0x16, 0x00, 0x00,      /*   Logical Minimum (0) */                  \
      0x26, U16(SIZE_MAX),   /*   Logical Maximum */                      \
      0x75, 0x10,            /*   Report Size (16) */                     \
      0x95, 0x01,            /*   Report Count (1) */                     \
      0x81, 0x02,            /*   Input (Data,Var,Abs) */                 \
                                                                          \
      0x09, 0x30,            /* Usage (Tip Pressure) */                   \
      0x15, 0x00,            /*   Logical Minimum (0) */                  \
      0x25, 0x64,            /*   Logical Maximum (100) */                \
      0x75, 0x08,            /*   Report Size (8) */                      \
      0x95, 0x01,            /*   Report Count (1) */                     \
      0x81, 0x02,            /*   Input (Data,Var,Abs) */                 \
                                                                          \
      0x09, 0x3F,            /* Usage (Azimuth) */                        \
      0x16, 0x00, 0x00,      /*   Logical Minimum (0) */                  \
      0x26, U16(18000),      /*   Logical Maximum (18000) */              \
      0x75, 0x10,            /*   Report Size (16) */                     \
      0x95, 0x01,            /*   Report Count (1) */                     \
      0x81, 0x02,            /*   Input (Data,Var,Abs) */                 \
                                                                          \
    0xC0                     /* End Collection */

static const uint8_t SC_HID_TOUCHSCREEN_REPORT_DESC[] = {
    0x05, 0x0D,                 /* Usage Page (Digitizers) */
    0x09, 0x04,                 /* Usage (Touch Screen) */
    0xA1, 0x01,                 /* Collection (Application) */

    0x85, SC_TS_REPORT_ID,      /* Report ID (1) */

    FINGER_BLOCK(SC_TS_X_MAX, SC_TS_Y_MAX, SC_TS_SIZE_MAX),
    FINGER_BLOCK(SC_TS_X_MAX, SC_TS_Y_MAX, SC_TS_SIZE_MAX),
    FINGER_BLOCK(SC_TS_X_MAX, SC_TS_Y_MAX, SC_TS_SIZE_MAX),
    FINGER_BLOCK(SC_TS_X_MAX, SC_TS_Y_MAX, SC_TS_SIZE_MAX),
    FINGER_BLOCK(SC_TS_X_MAX, SC_TS_Y_MAX, SC_TS_SIZE_MAX),
    FINGER_BLOCK(SC_TS_X_MAX, SC_TS_Y_MAX, SC_TS_SIZE_MAX),
    FINGER_BLOCK(SC_TS_X_MAX, SC_TS_Y_MAX, SC_TS_SIZE_MAX),
    FINGER_BLOCK(SC_TS_X_MAX, SC_TS_Y_MAX, SC_TS_SIZE_MAX),
    FINGER_BLOCK(SC_TS_X_MAX, SC_TS_Y_MAX, SC_TS_SIZE_MAX),
    FINGER_BLOCK(SC_TS_X_MAX, SC_TS_Y_MAX, SC_TS_SIZE_MAX),

    0x05, 0x0D,                 /* Usage Page (Digitizers) */
    0x09, 0x54,                 /* Usage (Contact Count) */
    0x15, 0x00,                 /*   Logical Minimum (0) */
    0x25, SC_HID_TOUCHSCREEN_CONTACTS, /* Logical Maximum (10) */
    0x75, 0x08,                 /*   Report Size (8) */
    0x95, 0x01,                 /*   Report Count (1) */
    0x81, 0x02,                 /*   Input (Data,Var,Abs) */

    0x09, 0x56,                 /* Usage (Scan Time) */
    0x16, 0x00, 0x00,           /*   Logical Minimum (0) */
    0x26, 0xFF, 0xFF,           /*   Logical Maximum (65535) */
    0x75, 0x10,                 /*   Report Size (16) */
    0x95, 0x01,                 /*   Report Count (1) */
    0x81, 0x02,                 /*   Input (Data,Var,Abs) */

    0xC0                        /* End Collection */
};

static inline void
sc_write16le(uint8_t *buf, uint16_t value) {
    buf[0] = (uint8_t) (value & 0xFF);
    buf[1] = (uint8_t) ((value >> 8) & 0xFF);
}

static void
sc_touchscreen_report_reset(uint8_t *report, uint16_t scan_time) {
    memset(report, 0, SC_TS_REPORT_SIZE);
    report[0] = SC_TS_REPORT_ID;
    report[SC_TS_CONTACT_COUNT_OFFSET] = 0;
    sc_write16le(&report[SC_TS_SCAN_TIME_OFFSET], scan_time);
}

static void
sc_touchscreen_report_set_contact_count(uint8_t *report, uint8_t count) {
    report[SC_TS_CONTACT_COUNT_OFFSET] = count;
}

static void
sc_touchscreen_report_set_finger(uint8_t *report, unsigned index, bool active,
                                 uint16_t contact_id,
                                 uint16_t x, uint16_t y,
                                 uint16_t width, uint16_t height,
                                 uint8_t pressure, uint16_t azimuth) {
    assert(index < SC_HID_TOUCHSCREEN_CONTACTS);

    size_t off = 1 + index * SC_TS_FINGER_SIZE;

    if (!active) {
        memset(&report[off], 0, SC_TS_FINGER_SIZE);
        return;
    }

    report[off + 0] = 0x07; /* TipSwitch | InRange | Confidence */

    sc_write16le(&report[off + 1], contact_id);
    sc_write16le(&report[off + 3], x);
    sc_write16le(&report[off + 5], y);
    sc_write16le(&report[off + 7], width);
    sc_write16le(&report[off + 9], height);
    report[off + 11] = pressure;
    sc_write16le(&report[off + 12], azimuth);
}

void
sc_hid_touchscreen_init(struct sc_hid_touchscreen *hid) {
    memset(hid, 0, sizeof(*hid));
}

void
sc_hid_touchscreen_generate_open(struct sc_hid_open *hid_open) {
    hid_open->hid_id = SC_HID_ID_TOUCHSCREEN;
    hid_open->report_desc = SC_HID_TOUCHSCREEN_REPORT_DESC;
    hid_open->report_desc_size = sizeof(SC_HID_TOUCHSCREEN_REPORT_DESC);
}

void
sc_hid_touchscreen_set_contact(struct sc_hid_touchscreen *hid,
                               unsigned index, bool active,
                               uint16_t contact_id,
                               uint16_t x, uint16_t y,
                               uint16_t width, uint16_t height,
                               uint8_t pressure, uint16_t azimuth) {
    assert(index < SC_HID_TOUCHSCREEN_CONTACTS);

    struct sc_hid_touchscreen_contact *contact = &hid->contacts[index];

    if (!active) {
        memset(contact, 0, sizeof(*contact));
        return;
    }

    contact->active = true;
    contact->contact_id = contact_id;
    contact->x = x;
    contact->y = y;
    contact->width = width;
    contact->height = height;
    contact->pressure = pressure;
    contact->azimuth = azimuth;
}

void
sc_hid_touchscreen_clear_contact(struct sc_hid_touchscreen *hid,
                                 unsigned index) {
    assert(index < SC_HID_TOUCHSCREEN_CONTACTS);
    memset(&hid->contacts[index], 0, sizeof(hid->contacts[index]));
}

void
sc_hid_touchscreen_clear_all(struct sc_hid_touchscreen *hid) {
    memset(hid->contacts, 0, sizeof(hid->contacts));
}

void
sc_hid_touchscreen_generate_input(struct sc_hid_touchscreen *hid,
                                  struct sc_hid_input *hid_input) {
    assert(SC_TS_REPORT_SIZE <= SC_HID_MAX_SIZE);

    hid_input->hid_id = SC_HID_ID_TOUCHSCREEN;
    hid_input->size = SC_TS_REPORT_SIZE;

    uint8_t *report = hid_input->data;
    uint8_t contact_count = 0;

    sc_touchscreen_report_reset(report, hid->scan_time);

    for (unsigned i = 0; i < SC_HID_TOUCHSCREEN_CONTACTS; ++i) {
        const struct sc_hid_touchscreen_contact *contact = &hid->contacts[i];
        if (!contact->active) {
            continue;
        }

        sc_touchscreen_report_set_finger(report, i, true,
                                         contact->contact_id,
                                         contact->x, contact->y,
                                         contact->width, contact->height,
                                         contact->pressure,
                                         contact->azimuth);
        ++contact_count;
    }

    sc_touchscreen_report_set_contact_count(report, contact_count);
    ++hid->scan_time;
}