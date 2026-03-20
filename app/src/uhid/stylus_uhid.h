#ifndef SC_STYLUS_UHID_H
#define SC_STYLUS_UHID_H

#include "common.h"
#include "controller.h"

struct sc_stylus_uhid {
    struct sc_controller *controller;
};

bool
sc_stylus_uhid_init(struct sc_stylus_uhid *stylus,
                    struct sc_controller *controller);

#endif