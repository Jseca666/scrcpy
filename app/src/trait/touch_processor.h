#ifndef SC_TOUCH_PROCESSOR_H
#define SC_TOUCH_PROCESSOR_H

#include "common.h"
#include "input_events.h"

struct sc_touch_processor {
    const struct sc_touch_processor_ops *ops;
};

struct sc_touch_processor_ops {
    void (*process_touch)(struct sc_touch_processor *tp,
                          const struct sc_touch_event *event);

    void (*begin_touch_update)(struct sc_touch_processor *tp);

    void (*end_touch_update)(struct sc_touch_processor *tp);
};

#endif