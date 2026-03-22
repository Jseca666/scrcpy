#include "touchscreen_uhid.h"

#include <assert.h>
#include <string.h>

#include "control_msg.h"
#include "hid/hid_event.h"
#include "util/log.h"

static const char *SC_TOUCHSCREEN_NAME = "Synaptics_ts";

static bool
sc_touchscreen_uhid_send_input(struct sc_touchscreen_uhid *touchscreen,
                               const struct sc_hid_input *hid_input) {
    assert(hid_input->size <= SC_HID_MAX_SIZE);

    struct sc_control_msg msg;
    msg.type = SC_CONTROL_MSG_TYPE_UHID_INPUT;
    msg.uhid_input.id = hid_input->hid_id;
    memcpy(msg.uhid_input.data, hid_input->data, hid_input->size);
    msg.uhid_input.size = hid_input->size;

    if (!sc_controller_push_msg(touchscreen->controller, &msg)) {
        LOGE("Could not push UHID_INPUT message (touchscreen)");
        return false;
    }

    return true;
}




static void
sc_touchscreen_uhid_set_contact(struct sc_touchscreen_uhid *touchscreen,
                                unsigned index, bool active,
                                uint16_t contact_id,
                                uint16_t x, uint16_t y,
                                uint16_t width, uint16_t height,
                                uint8_t pressure, uint16_t azimuth) {
    sc_hid_touchscreen_set_contact(&touchscreen->hid, index, active,
                                   contact_id, x, y, width, height,
                                   pressure, azimuth);
}

static void
sc_touchscreen_uhid_clear_contact(struct sc_touchscreen_uhid *touchscreen,
                                  unsigned index) {
    sc_hid_touchscreen_clear_contact(&touchscreen->hid, index);
}

static void
sc_touchscreen_uhid_clear_all(struct sc_touchscreen_uhid *touchscreen) {
    sc_hid_touchscreen_clear_all(&touchscreen->hid);
}

static bool
sc_touchscreen_uhid_commit(struct sc_touchscreen_uhid *touchscreen) {
    struct sc_hid_input hid_input;
    sc_hid_touchscreen_generate_input(&touchscreen->hid, &hid_input);
    return sc_touchscreen_uhid_send_input(touchscreen, &hid_input);
}



static void
sc_touchscreen_uhid_process_touch(struct sc_touch_processor *tp,
                                  const struct sc_touch_event *event) {
    struct sc_touchscreen_uhid *touchscreen =
        container_of(tp, struct sc_touchscreen_uhid, touch_processor);

    unsigned index = (unsigned) event->pointer_id;

    if (index >= SC_HID_TOUCHSCREEN_CONTACTS) {
        LOGW("Ignoring touch event: pointer_id %" PRIu64 " out of range",
             event->pointer_id);
        return;
    }

    uint16_t x = (uint16_t) event->position.point.x;
    uint16_t y = (uint16_t) event->position.point.y;

    // v1.3 先给出稳定默认值，后面上层需要时再扩展
    uint16_t width = 1000;
    uint16_t height = 1400;
    uint8_t pressure = event->pressure > 1.0f
            ? 100
            : (uint8_t) (event->pressure * 100.0f);
    uint16_t azimuth = 9000; // 中值，后续可扩展

    switch (event->action) {
        case SC_TOUCH_ACTION_DOWN:
        case SC_TOUCH_ACTION_MOVE:
            sc_touchscreen_uhid_set_contact(touchscreen, index, true,
                                            (uint16_t) event->pointer_id,
                                            x, y,
                                            width, height,
                                            pressure, azimuth);
            sc_touchscreen_uhid_commit(touchscreen);
            break;
        case SC_TOUCH_ACTION_UP:
            sc_touchscreen_uhid_clear_contact(touchscreen, index);
            sc_touchscreen_uhid_commit(touchscreen);
            break;
        default:
            LOGW("Ignoring unknown touch action");
            break;
    }
}

static const struct sc_touch_processor_ops
sc_touchscreen_uhid_touch_processor_ops = {
    .process_touch = sc_touchscreen_uhid_process_touch,
};

bool
sc_touchscreen_uhid_init(struct sc_touchscreen_uhid *touchscreen,
                         struct sc_controller *controller) {
    memset(touchscreen, 0, sizeof(*touchscreen));
    touchscreen->controller = controller;
    touchscreen->touch_processor.ops =
        &sc_touchscreen_uhid_touch_processor_ops;

    sc_hid_touchscreen_init(&touchscreen->hid);

    struct sc_hid_open hid_open;
    sc_hid_touchscreen_generate_open(&hid_open);
    assert(hid_open.hid_id == SC_HID_ID_TOUCHSCREEN);

    struct sc_control_msg msg;
    msg.type = SC_CONTROL_MSG_TYPE_UHID_CREATE;
    msg.uhid_create.id = hid_open.hid_id;
    msg.uhid_create.vendor_id = 0x06cb;
    msg.uhid_create.product_id = 0x0006;
    msg.uhid_create.name = SC_TOUCHSCREEN_NAME;
    msg.uhid_create.report_desc = hid_open.report_desc;
    msg.uhid_create.report_desc_size = (uint16_t) hid_open.report_desc_size;

    if (!sc_controller_push_msg(controller, &msg)) {
        LOGE("Could not push UHID_CREATE message (touchscreen)");
        return false;
    }

    return true;
}
