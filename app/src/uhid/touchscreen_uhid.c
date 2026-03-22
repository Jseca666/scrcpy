#include "touchscreen_uhid.h"

#include <assert.h>
#include <string.h>
#include <SDL2/SDL.h>
#include "control_msg.h"
#include "hid/hid_event.h"
#include "util/log.h"

#define SC_HID_ID_TOUCHSCREEN 10

static const char *SC_TOUCHSCREEN_NAME = "Synaptics_ts";
/*e
 * Test profile:
 * - Touch Screen top-level collection
 * - 10 fixed finger blocks
 * - vivo-like raw scale: 20639 x 30959
 * - fuller HONOR-like fields: pressure / width / height / azimuth
 *
 * For now this is intentionally hard-coded for testing.
 * Later, replace SC_TS_X_MAX / SC_TS_Y_MAX by display_resolution * 10 - 1.
 */
#define SC_TS_CONTACTS             10
#define SC_TS_REPORT_ID            0x01

#define SC_TS_X_MAX                20639
#define SC_TS_Y_MAX                30959
#define SC_TS_SIZE_MAX             30959

#define SC_TS_FINGER_SIZE          14
#define SC_TS_REPORT_SIZE          (1 + SC_TS_CONTACTS * SC_TS_FINGER_SIZE + 1 + 2)

#define SC_TS_CONTACT_COUNT_OFFSET (1 + SC_TS_CONTACTS * SC_TS_FINGER_SIZE)
#define SC_TS_SCAN_TIME_OFFSET     (SC_TS_CONTACT_COUNT_OFFSET + 1)

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
    0x25, SC_TS_CONTACTS,       /*   Logical Maximum (10) */
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

static bool
sc_touchscreen_uhid_send_input(struct sc_touchscreen_uhid *touchscreen, const uint8_t *data,
                          size_t size) {
    assert(size <= SC_HID_MAX_SIZE);

    struct sc_control_msg msg;
    msg.type = SC_CONTROL_MSG_TYPE_UHID_INPUT;
    msg.uhid_input.id = SC_HID_ID_TOUCHSCREEN;
    memcpy(msg.uhid_input.data, data, size);
    msg.uhid_input.size = (uint16_t) size;

    if (!sc_controller_push_msg(touchscreen->controller, &msg)) {
        LOGE("Could not push UHID_INPUT message (touchscreen)");
        return false;
    }

    return true;
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
    assert(index < SC_TS_CONTACTS);

    size_t off = 1 + index * SC_TS_FINGER_SIZE;

    if (!active) {
        memset(&report[off], 0, SC_TS_FINGER_SIZE);
        return;
    }

    /* bit0 TipSwitch, bit1 InRange, bit2 Confidence */
    report[off + 0] = 0x07;

    sc_write16le(&report[off + 1], contact_id);
    sc_write16le(&report[off + 3], x);
    sc_write16le(&report[off + 5], y);
    sc_write16le(&report[off + 7], width);
    sc_write16le(&report[off + 9], height);
    report[off + 11] = pressure;
    sc_write16le(&report[off + 12], azimuth);
}

static bool
sc_touchscreen_send_frame(struct sc_touchscreen_uhid *touchscreen, uint8_t *report,
                          uint16_t *scan_time) {
    sc_write16le(&report[SC_TS_SCAN_TIME_OFFSET], *scan_time);
    ++*scan_time;
    return sc_touchscreen_uhid_send_input(touchscreen, report, SC_TS_REPORT_SIZE);
}

static int
sc_touchscreen_uhid_test_thread(void *userdata) {
    struct sc_touchscreen_uhid *touchscreen = userdata;

    uint8_t report[SC_TS_REPORT_SIZE];
    uint16_t scan_time = 0;

    LOGI("touchscreen test thread started");
    SDL_Delay(3000);

    for (int i = 0; i < 20; ++i) {
        LOGI("touchscreen test round %d: finger0 down", i);
        sc_touchscreen_report_reset(report, scan_time);
        sc_touchscreen_report_set_finger(report, 0, true,
                                         100,
                                         6000, 9000,
                                         900, 1300,
                                         45, 9000);
        sc_touchscreen_report_set_contact_count(report, 1);
        if (!sc_touchscreen_send_frame(touchscreen, report, &scan_time)) {
            return 0;
        }
        SDL_Delay(500);

        LOGI("touchscreen test round %d: finger0 move", i);
        sc_touchscreen_report_reset(report, scan_time);
        sc_touchscreen_report_set_finger(report, 0, true,
                                         100,
                                         9000, 12000,
                                         1000, 1400,
                                         52, 9000);
        sc_touchscreen_report_set_contact_count(report, 1);
        if (!sc_touchscreen_send_frame(touchscreen, report, &scan_time)) {
            return 0;
        }
        SDL_Delay(500);

        LOGI("touchscreen test round %d: finger1 down", i);
        sc_touchscreen_report_reset(report, scan_time);
        sc_touchscreen_report_set_finger(report, 0, true,
                                         100,
                                         9000, 12000,
                                         1000, 1400,
                                         52, 9000);
        sc_touchscreen_report_set_finger(report, 1, true,
                                         101,
                                         15000, 20000,
                                         950, 1350,
                                         48, 9000);
        sc_touchscreen_report_set_contact_count(report, 2);
        if (!sc_touchscreen_send_frame(touchscreen, report, &scan_time)) {
            return 0;
        }
        SDL_Delay(500);

        LOGI("touchscreen test round %d: two-finger move", i);
        sc_touchscreen_report_reset(report, scan_time);
        sc_touchscreen_report_set_finger(report, 0, true,
                                         100,
                                         7000, 10000,
                                         1100, 1500,
                                         58, 9000);
        sc_touchscreen_report_set_finger(report, 1, true,
                                         101,
                                         17000, 22000,
                                         1000, 1450,
                                         54, 9000);
        sc_touchscreen_report_set_contact_count(report, 2);
        if (!sc_touchscreen_send_frame(touchscreen, report, &scan_time)) {
            return 0;
        }
        SDL_Delay(500);

        LOGI("touchscreen test round %d: finger1 up", i);
        sc_touchscreen_report_reset(report, scan_time);
        sc_touchscreen_report_set_finger(report, 0, true,
                                         100,
                                         7000, 10000,
                                         1100, 1500,
                                         58, 9000);
        sc_touchscreen_report_set_contact_count(report, 1);
        if (!sc_touchscreen_send_frame(touchscreen, report, &scan_time)) {
            return 0;
        }
        SDL_Delay(500);

        LOGI("touchscreen test round %d: all up", i);
        sc_touchscreen_report_reset(report, scan_time);
        sc_touchscreen_report_set_contact_count(report, 0);
        if (!sc_touchscreen_send_frame(touchscreen, report, &scan_time)) {
            return 0;
        }
        SDL_Delay(800);
    }

    LOGI("touchscreen test thread finished");
    return 0;
}

bool
sc_touchscreen_uhid_init(struct sc_touchscreen_uhid *touchscreen,
                    struct sc_controller *controller) {
    assert(SC_TS_REPORT_SIZE <= SC_HID_MAX_SIZE);

    touchscreen->controller = controller;

    struct sc_control_msg msg;
    msg.type = SC_CONTROL_MSG_TYPE_UHID_CREATE;
    msg.uhid_create.id = SC_HID_ID_TOUCHSCREEN;
    msg.uhid_create.vendor_id = 0x06cb;
    msg.uhid_create.product_id = 0x0006;
    msg.uhid_create.name = SC_TOUCHSCREEN_NAME;
    msg.uhid_create.report_desc = SC_HID_TOUCHSCREEN_REPORT_DESC;
    msg.uhid_create.report_desc_size =
        (uint16_t) sizeof(SC_HID_TOUCHSCREEN_REPORT_DESC);

    if (!sc_controller_push_msg(controller, &msg)) {
        LOGE("Could not push UHID_CREATE message (touchscreen)");
        return false;
    }

    if (!SDL_CreateThread(sc_touchscreen_uhid_test_thread, "touchscreen-uhid-test",
                          touchscreen)) {
        LOGE("Could not create touchscreen test thread: %s", SDL_GetError());
        return false;
    }

    return true;
}