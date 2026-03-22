#ifndef SC_TOUCH_PROCESSOR_H
#define SC_TOUCH_PROCESSOR_H

#include "common.h"
#include "input_events.h"

/**
 * Touch processor trait.
 *
 * Component able to process and inject touch events should implement this
 * trait.
 */
struct sc_touch_processor {
    const struct sc_touch_processor_ops *ops;
};

struct sc_touch_processor_ops {
    /**
     * Process a touch event.
     *
     * This function is mandatory.
     */
    void (*process_touch)(struct sc_touch_processor *tp,
                          const struct sc_touch_event *event);
};

#endif