#include "stylus_uhid.h"

#include <assert.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "control_msg.h"
#include "hid/hid_event.h"
#include "util/log.h"

#define SC_HID_ID_STYLUS 10

static const char *SC_STYLUS_NAME = "scrcpy-stylus";

static const uint8_t SC_HID_STYLUS_REPORT_DESC[] = {
    0x05, 0x0D,       /* Usage Page (Digitizers) */
    0x09, 0x02,       /* Usage (Pen) */
    0xA1, 0x01,       /* Collection (Application) */
    0x09, 0x20,       /*   Usage (Stylus) */
    0xA1, 0x00,       /*   Collection (Physical) */

    0x09, 0x42,       /*     Usage (Tip Switch) */
    0x09, 0x32,       /*     Usage (In Range) */
    0x09, 0x44,       /*     Usage (Barrel Switch) */
    0x15, 0x00,       /*     Logical Minimum (0) */
    0x25, 0x01,       /*     Logical Maximum (1) */
    0x75, 0x01,       /*     Report Size (1) */
    0x95, 0x03,       /*     Report Count (3) */
    0x81, 0x02,       /*     Input (Data,Var,Abs) */

    0x95, 0x05,       /*     Report Count (5) */
    0x81, 0x03,       /*     Input (Const,Var,Abs) - padding */

    0x05, 0x01,       /*     Usage Page (Generic Desktop) */
    0x09, 0x30,       /*     Usage (X) */
    0x09, 0x31,       /*     Usage (Y) */
    0x16, 0x00, 0x00, /*     Logical Minimum (0) */
    0x26, 0xFF, 0x7F, /*     Logical Maximum (32767) */
    0x75, 0x10,       /*     Report Size (16) */
    0x95, 0x02,       /*     Report Count (2) */
    0x81, 0x02,       /*     Input (Data,Var,Abs) */

    0x05, 0x0D,       /*     Usage Page (Digitizers) */
    0x09, 0x30,       /*     Usage (Tip Pressure) */
    0x16, 0x00, 0x00, /*     Logical Minimum (0) */
    0x26, 0xFF, 0x03, /*     Logical Maximum (1023) */
    0x75, 0x10,       /*     Report Size (16) */
    0x95, 0x01,       /*     Report Count (1) */
    0x81, 0x02,       /*     Input (Data,Var,Abs) */

    0xC0,             /*   End Collection */
    0xC0              /* End Collection */
};

static bool
sc_stylus_uhid_send_input(struct sc_stylus_uhid *stylus,
                          const uint8_t *data, size_t size) {
    assert(size <= SC_HID_MAX_SIZE);

    struct sc_control_msg msg;
    msg.type = SC_CONTROL_MSG_TYPE_UHID_INPUT;
    msg.uhid_input.id = SC_HID_ID_STYLUS;
    memcpy(msg.uhid_input.data, data, size);
    msg.uhid_input.size = (uint16_t) size;

    if (!sc_controller_push_msg(stylus->controller, &msg)) {
        LOGE("Could not push UHID_INPUT message (stylus)");
        return false;
    }

    return true;
}

static int
sc_stylus_uhid_test_thread(void *userdata) {
    struct sc_stylus_uhid *stylus = userdata;

    /* hover at (4096, 4096), pressure = 0 */
    static const uint8_t hover_report_1[] = {
        0x02, 0x00, 0x10, 0x00, 0x10, 0x00, 0x00
    };

    /* hover at (24576, 24576), pressure = 0 */
    static const uint8_t hover_report_2[] = {
        0x02, 0x00, 0x60, 0x00, 0x60, 0x00, 0x00
    };

    /* touch down at (24576, 24576), pressure = 512 */
    static const uint8_t down_report[] = {
        0x03, 0x00, 0x60, 0x00, 0x60, 0x00, 0x02
    };

    /* drag to (28672, 8192), pressure = 640 */
    static const uint8_t drag_report[] = {
        0x03, 0x00, 0x70, 0x00, 0x20, 0x80, 0x02
    };

    /* leave at (28672, 8192), pressure = 0 */
    static const uint8_t leave_report[] = {
        0x00, 0x00, 0x70, 0x00, 0x20, 0x00, 0x00
    };

    LOGI("stylus test thread started, waiting before sending reports...");
    SDL_Delay(8000);

    for (int i = 0; i < 20; ++i) {
        LOGI("stylus test round %d: hover 1", i);
        sc_stylus_uhid_send_input(stylus, hover_report_1,
                                  sizeof(hover_report_1));
        SDL_Delay(1000);

        LOGI("stylus test round %d: hover 2", i);
        sc_stylus_uhid_send_input(stylus, hover_report_2,
                                  sizeof(hover_report_2));
        SDL_Delay(1000);

        LOGI("stylus test round %d: down", i);
        sc_stylus_uhid_send_input(stylus, down_report,
                                  sizeof(down_report));
        SDL_Delay(1000);

        LOGI("stylus test round %d: drag", i);
        sc_stylus_uhid_send_input(stylus, drag_report,
                                  sizeof(drag_report));
        SDL_Delay(1000);

        LOGI("stylus test round %d: leave", i);
        sc_stylus_uhid_send_input(stylus, leave_report,
                                  sizeof(leave_report));
        SDL_Delay(1000);
    }

    LOGI("stylus test thread finished");
    return 0;
}

bool
sc_stylus_uhid_init(struct sc_stylus_uhid *stylus,
                    struct sc_controller *controller) {
    stylus->controller = controller;

    struct sc_control_msg msg;
    msg.type = SC_CONTROL_MSG_TYPE_UHID_CREATE;

    msg.uhid_create.id = SC_HID_ID_STYLUS;
    msg.uhid_create.vendor_id = 0;
    msg.uhid_create.product_id = 0;
    msg.uhid_create.name = SC_STYLUS_NAME;
    msg.uhid_create.report_desc = SC_HID_STYLUS_REPORT_DESC;
    msg.uhid_create.report_desc_size =
        (uint16_t) sizeof(SC_HID_STYLUS_REPORT_DESC);

    if (!sc_controller_push_msg(controller, &msg)) {
        LOGE("Could not push UHID_CREATE message (stylus)");
        return false;
    }

    if (!SDL_CreateThread(sc_stylus_uhid_test_thread,
                          "stylus-uhid-test", stylus)) {
        LOGE("Could not create stylus test thread: %s", SDL_GetError());
        return false;
    }

    return true;
}