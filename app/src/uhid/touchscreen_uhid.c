#include "touchscreen_uhid.h"

#include <assert.h>
#include <string.h>

#include "control_msg.h"
#include "hid/hid_event.h"
#include "util/log.h"

static const char *SC_TOUCHSCREEN_NAME = "Synaptics_ts";

static int
sc_touchscreen_uhid_find_slot_by_pointer_id(
        struct sc_touchscreen_uhid *touchscreen, uint64_t pointer_id) {
    for (unsigned i = 0; i < SC_HID_TOUCHSCREEN_CONTACTS; ++i) {
        if (touchscreen->slots[i].active
                && touchscreen->slots[i].pointer_id == pointer_id) {
            return (int) i;
        }
    }
    return -1;
}

static int
sc_touchscreen_uhid_find_free_slot(struct sc_touchscreen_uhid *touchscreen) {
    for (unsigned i = 0; i < SC_HID_TOUCHSCREEN_CONTACTS; ++i) {
        if (!touchscreen->slots[i].active) {
            return (int) i;
        }
    }
    return -1;
}

static int
sc_touchscreen_uhid_acquire_slot(struct sc_touchscreen_uhid *touchscreen,
                                 uint64_t pointer_id) {
    int slot = sc_touchscreen_uhid_find_slot_by_pointer_id(touchscreen,
                                                           pointer_id);
    if (slot >= 0) {
        return slot;
    }

    slot = sc_touchscreen_uhid_find_free_slot(touchscreen);
    if (slot < 0) {
        return -1;
    }

    struct sc_touchscreen_slot *s = &touchscreen->slots[slot];
    s->active = true;
    s->pointer_id = pointer_id;
    s->contact_id = touchscreen->next_contact_id++;

    if (!s->contact_id) {
        s->contact_id = touchscreen->next_contact_id++;
    }

    return slot;
}

static void
sc_touchscreen_uhid_release_slot(struct sc_touchscreen_uhid *touchscreen,
                                 unsigned slot) {
    assert(slot < SC_HID_TOUCHSCREEN_CONTACTS);
    memset(&touchscreen->slots[slot], 0, sizeof(touchscreen->slots[slot]));
}

static uint8_t
sc_touchscreen_uhid_normalize_pressure(float pressure) {
    if (pressure <= 0.0f) {
        return 1;
    }
    if (pressure >= 1.0f) {
        return 100;
    }
    return (uint8_t) (pressure * 100.0f);
}

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

    int slot;
    uint16_t x = (uint16_t) event->position.point.x;
    uint16_t y = (uint16_t) event->position.point.y;

    // v1.4 先保持固定默认值，后面再扩展成上层可传
    uint16_t width = 1000;
    uint16_t height = 1400;
    uint8_t pressure = sc_touchscreen_uhid_normalize_pressure(event->pressure);
    uint16_t azimuth = 9000;

    switch (event->action) {
        case SC_TOUCH_ACTION_DOWN:
            slot = sc_touchscreen_uhid_acquire_slot(touchscreen,
                                                    event->pointer_id);
            if (slot < 0) {
                LOGW("Ignoring touch DOWN: no free touchscreen slot");
                return;
            }

            sc_touchscreen_uhid_set_contact(
                touchscreen, (unsigned) slot, true,
                touchscreen->slots[slot].contact_id,
                x, y, width, height, pressure, azimuth);
            sc_touchscreen_uhid_commit(touchscreen);
            break;

        case SC_TOUCH_ACTION_MOVE:
            slot = sc_touchscreen_uhid_find_slot_by_pointer_id(
                touchscreen, event->pointer_id);
            if (slot < 0) {
                LOGW("Ignoring touch MOVE: pointer_id %" PRIu64 " not active",
                     event->pointer_id);
                return;
            }

            sc_touchscreen_uhid_set_contact(
                touchscreen, (unsigned) slot, true,
                touchscreen->slots[slot].contact_id,
                x, y, width, height, pressure, azimuth);
            sc_touchscreen_uhid_commit(touchscreen);
            break;

        case SC_TOUCH_ACTION_UP:
            slot = sc_touchscreen_uhid_find_slot_by_pointer_id(
                touchscreen, event->pointer_id);
            if (slot < 0) {
                LOGW("Ignoring touch UP: pointer_id %" PRIu64 " not active",
                     event->pointer_id);
                return;
            }

            sc_touchscreen_uhid_clear_contact(touchscreen, (unsigned) slot);
            sc_touchscreen_uhid_commit(touchscreen);
            sc_touchscreen_uhid_release_slot(touchscreen, (unsigned) slot);
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
    touchscreen->next_contact_id = 1;                  
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
