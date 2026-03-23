#define SC_TOUCHSCREEN_UHID_MANUAL_TEST 1
#include "touchscreen_uhid_test.h"

#include <inttypes.h>

#include <SDL2/SDL_thread.h>
#include <SDL2/SDL_timer.h>

#include "input_events.h"
#include "util/log.h"

#if defined(SC_TOUCHSCREEN_UHID_MANUAL_TEST)

#define SC_TS_TEST_SCREEN_W 1224
#define SC_TS_TEST_SCREEN_H 2700

#define SC_TS_TEST_P1 ((uint64_t) 1001)
#define SC_TS_TEST_P2 ((uint64_t) 1002)
#define SC_TS_TEST_P3 ((uint64_t) 1003)

#define SC_TS_TEST_START_DELAY_MS          3000
#define SC_TS_TEST_STEP_DELAY_MS            220
#define SC_TS_TEST_HOLD_SHORT_MS            700
#define SC_TS_TEST_HOLD_LONG_MS            1200

struct sc_ts_test_sample {
    enum sc_touch_action action;
    uint64_t pointer_id;
    int32_t x;
    int32_t y;
    float pressure;
    uint16_t touch_major;
    uint16_t touch_minor;
    uint16_t azimuth;
};

static void
sc_touchscreen_test_begin(struct sc_touchscreen_uhid *touchscreen) {
    struct sc_touch_processor *tp = &touchscreen->touch_processor;
    if (tp->ops->begin_touch_update) {
        tp->ops->begin_touch_update(tp);
    }
}

static void
sc_touchscreen_test_end(struct sc_touchscreen_uhid *touchscreen) {
    struct sc_touch_processor *tp = &touchscreen->touch_processor;
    if (tp->ops->end_touch_update) {
        tp->ops->end_touch_update(tp);
    }
}

static void
sc_touchscreen_test_emit_sample(struct sc_touchscreen_uhid *touchscreen,
                                const struct sc_ts_test_sample *s) {
    struct sc_touch_event event = {
        .position = {
            .screen_size = {
                .width = SC_TS_TEST_SCREEN_W,
                .height = SC_TS_TEST_SCREEN_H,
            },
            .point = {
                .x = s->x,
                .y = s->y,
            },
        },
        .action = s->action,
        .pointer_id = s->pointer_id,
        .pressure = s->pressure,
        .touch_major = s->touch_major,
        .touch_minor = s->touch_minor,
        .azimuth = s->azimuth,
    };

    LOGI("[ts-rA2] emit action=%d pid=%" PRIu64
         " pos=(%d,%d) p=%.2f major=%u minor=%u az=%u",
         (int) s->action, s->pointer_id,
         s->x, s->y, s->pressure,
         s->touch_major, s->touch_minor, s->azimuth);

    touchscreen->touch_processor.ops->process_touch(
        &touchscreen->touch_processor, &event);
}

static void
sc_touchscreen_test_emit_batch(struct sc_touchscreen_uhid *touchscreen,
                               const struct sc_ts_test_sample *samples,
                               size_t count) {
    sc_touchscreen_test_begin(touchscreen);
    for (size_t i = 0; i < count; ++i) {
        sc_touchscreen_test_emit_sample(touchscreen, &samples[i]);
    }
    sc_touchscreen_test_end(touchscreen);
}

static void
sc_touchscreen_test_case_a_same_slot_multi_move(struct sc_touchscreen_uhid *touchscreen) {
    static const struct sc_ts_test_sample down[] = {
        {
            .action = SC_TOUCH_ACTION_DOWN,
            .pointer_id = SC_TS_TEST_P1,
            .x = 320, .y = 900,
            .pressure = 0.38f,
            .touch_major = 820,
            .touch_minor = 600,
            .azimuth = 9000,
        },
    };

    static const struct sc_ts_test_sample burst[] = {
        {
            .action = SC_TOUCH_ACTION_MOVE,
            .pointer_id = SC_TS_TEST_P1,
            .x = 350, .y = 980,
            .pressure = 0.40f,
            .touch_major = 840,
            .touch_minor = 610,
            .azimuth = 9100,
        },
        {
            .action = SC_TOUCH_ACTION_MOVE,
            .pointer_id = SC_TS_TEST_P1,
            .x = 390, .y = 1070,
            .pressure = 0.44f,
            .touch_major = 860,
            .touch_minor = 620,
            .azimuth = 9300,
        },
        {
            .action = SC_TOUCH_ACTION_MOVE,
            .pointer_id = SC_TS_TEST_P1,
            .x = 430, .y = 1170,
            .pressure = 0.49f,
            .touch_major = 890,
            .touch_minor = 635,
            .azimuth = 9500,
        },
    };

    static const struct sc_ts_test_sample up[] = {
        {
            .action = SC_TOUCH_ACTION_UP,
            .pointer_id = SC_TS_TEST_P1,
            .x = 430, .y = 1170,
            .pressure = 0.00f,
            .touch_major = 890,
            .touch_minor = 635,
            .azimuth = 9500,
        },
    };

    LOGI("[ts-rA2] caseA: same-slot multi-MOVE in one batch");
    sc_touchscreen_test_emit_batch(touchscreen, down, sizeof(down) / sizeof(down[0]));
    SDL_Delay(SC_TS_TEST_HOLD_SHORT_MS);
    sc_touchscreen_test_emit_batch(touchscreen, burst, sizeof(burst) / sizeof(burst[0]));
    SDL_Delay(SC_TS_TEST_HOLD_SHORT_MS);
    sc_touchscreen_test_emit_batch(touchscreen, up, sizeof(up) / sizeof(up[0]));
    SDL_Delay(SC_TS_TEST_HOLD_SHORT_MS);
}

static void
sc_touchscreen_test_case_b_down_then_overwrite(struct sc_touchscreen_uhid *touchscreen) {
    static const struct sc_ts_test_sample batch[] = {
        {
            .action = SC_TOUCH_ACTION_DOWN,
            .pointer_id = SC_TS_TEST_P1,
            .x = 260, .y = 1450,
            .pressure = 0.32f,
            .touch_major = 760,
            .touch_minor = 560,
            .azimuth = 7600,
        },
        {
            .action = SC_TOUCH_ACTION_MOVE,
            .pointer_id = SC_TS_TEST_P1,
            .x = 320, .y = 1520,
            .pressure = 0.42f,
            .touch_major = 820,
            .touch_minor = 590,
            .azimuth = 8200,
        },
        {
            .action = SC_TOUCH_ACTION_MOVE,
            .pointer_id = SC_TS_TEST_P1,
            .x = 390, .y = 1610,
            .pressure = 0.56f,
            .touch_major = 910,
            .touch_minor = 640,
            .azimuth = 9000,
        },
    };

    static const struct sc_ts_test_sample up[] = {
        {
            .action = SC_TOUCH_ACTION_UP,
            .pointer_id = SC_TS_TEST_P1,
            .x = 390, .y = 1610,
            .pressure = 0.00f,
            .touch_major = 910,
            .touch_minor = 640,
            .azimuth = 9000,
        },
    };

    LOGI("[ts-rA2] caseB: DOWN then overwritten by MOVE in same batch");
    sc_touchscreen_test_emit_batch(touchscreen, batch, sizeof(batch) / sizeof(batch[0]));
    SDL_Delay(SC_TS_TEST_HOLD_SHORT_MS);
    sc_touchscreen_test_emit_batch(touchscreen, up, sizeof(up) / sizeof(up[0]));
    SDL_Delay(SC_TS_TEST_HOLD_SHORT_MS);
}

static void
sc_touchscreen_test_case_c_two_slots_one_hot(struct sc_touchscreen_uhid *touchscreen) {
    static const struct sc_ts_test_sample down[] = {
        {
            .action = SC_TOUCH_ACTION_DOWN,
            .pointer_id = SC_TS_TEST_P1,
            .x = 280, .y = 1850,
            .pressure = 0.40f,
            .touch_major = 840,
            .touch_minor = 600,
            .azimuth = 7600,
        },
        {
            .action = SC_TOUCH_ACTION_DOWN,
            .pointer_id = SC_TS_TEST_P2,
            .x = 900, .y = 1850,
            .pressure = 0.58f,
            .touch_major = 1160,
            .touch_minor = 760,
            .azimuth = 10400,
        },
    };

    static const struct sc_ts_test_sample burst[] = {
        {
            .action = SC_TOUCH_ACTION_MOVE,
            .pointer_id = SC_TS_TEST_P1,
            .x = 300, .y = 1920,
            .pressure = 0.42f,
            .touch_major = 850,
            .touch_minor = 606,
            .azimuth = 7800,
        },
        {
            .action = SC_TOUCH_ACTION_MOVE,
            .pointer_id = SC_TS_TEST_P1,
            .x = 330, .y = 1990,
            .pressure = 0.47f,
            .touch_major = 870,
            .touch_minor = 618,
            .azimuth = 8100,
        },
        {
            .action = SC_TOUCH_ACTION_MOVE,
            .pointer_id = SC_TS_TEST_P1,
            .x = 360, .y = 2060,
            .pressure = 0.53f,
            .touch_major = 900,
            .touch_minor = 632,
            .azimuth = 8400,
        },
        {
            .action = SC_TOUCH_ACTION_MOVE,
            .pointer_id = SC_TS_TEST_P2,
            .x = 900, .y = 1910,
            .pressure = 0.58f,
            .touch_major = 1160,
            .touch_minor = 760,
            .azimuth = 10400,
        },
    };

    static const struct sc_ts_test_sample up[] = {
        {
            .action = SC_TOUCH_ACTION_UP,
            .pointer_id = SC_TS_TEST_P1,
            .x = 360, .y = 2060,
            .pressure = 0.00f,
            .touch_major = 900,
            .touch_minor = 632,
            .azimuth = 8400,
        },
        {
            .action = SC_TOUCH_ACTION_UP,
            .pointer_id = SC_TS_TEST_P2,
            .x = 900, .y = 1910,
            .pressure = 0.00f,
            .touch_major = 1160,
            .touch_minor = 760,
            .azimuth = 10400,
        },
    };

    LOGI("[ts-rA2] caseC: two slots, one slot hot-updated inside batch");
    sc_touchscreen_test_emit_batch(touchscreen, down, sizeof(down) / sizeof(down[0]));
    SDL_Delay(SC_TS_TEST_HOLD_SHORT_MS);
    sc_touchscreen_test_emit_batch(touchscreen, burst, sizeof(burst) / sizeof(burst[0]));
    SDL_Delay(SC_TS_TEST_HOLD_SHORT_MS);
    sc_touchscreen_test_emit_batch(touchscreen, up, sizeof(up) / sizeof(up[0]));
    SDL_Delay(SC_TS_TEST_HOLD_SHORT_MS);
}

static void
sc_touchscreen_test_case_d_param_rewrite(struct sc_touchscreen_uhid *touchscreen) {
    static const struct sc_ts_test_sample down[] = {
        {
            .action = SC_TOUCH_ACTION_DOWN,
            .pointer_id = SC_TS_TEST_P3,
            .x = 612, .y = 2250,
            .pressure = 0.36f,
            .touch_major = 760,
            .touch_minor = 520,
            .azimuth = 5200,
        },
    };

    static const struct sc_ts_test_sample rewrite[] = {
        {
            .action = SC_TOUCH_ACTION_MOVE,
            .pointer_id = SC_TS_TEST_P3,
            .x = 612, .y = 2250,
            .pressure = 0.42f,
            .touch_major = 860,
            .touch_minor = 540,
            .azimuth = 7000,
        },
        {
            .action = SC_TOUCH_ACTION_MOVE,
            .pointer_id = SC_TS_TEST_P3,
            .x = 612, .y = 2250,
            .pressure = 0.55f,
            .touch_major = 980,
            .touch_minor = 620,
            .azimuth = 9000,
        },
        {
            .action = SC_TOUCH_ACTION_MOVE,
            .pointer_id = SC_TS_TEST_P3,
            .x = 612, .y = 2250,
            .pressure = 0.70f,
            .touch_major = 1120,
            .touch_minor = 760,
            .azimuth = 12000,
        },
    };

    static const struct sc_ts_test_sample up[] = {
        {
            .action = SC_TOUCH_ACTION_UP,
            .pointer_id = SC_TS_TEST_P3,
            .x = 612, .y = 2250,
            .pressure = 0.00f,
            .touch_major = 1120,
            .touch_minor = 760,
            .azimuth = 12000,
        },
    };

    LOGI("[ts-rA2] caseD: same-slot parameter rewrite in one batch");
    sc_touchscreen_test_emit_batch(touchscreen, down, sizeof(down) / sizeof(down[0]));
    SDL_Delay(SC_TS_TEST_HOLD_SHORT_MS);
    sc_touchscreen_test_emit_batch(touchscreen, rewrite, sizeof(rewrite) / sizeof(rewrite[0]));
    SDL_Delay(SC_TS_TEST_HOLD_SHORT_MS);
    sc_touchscreen_test_emit_batch(touchscreen, up, sizeof(up) / sizeof(up[0]));
    SDL_Delay(SC_TS_TEST_HOLD_LONG_MS);
}

static int SDLCALL
sc_touchscreen_uhid_test_thread(void *userdata) {
    struct sc_touchscreen_uhid *touchscreen = userdata;

    LOGI("[ts-rA2] scheduled, waiting %d ms before start",
         SC_TS_TEST_START_DELAY_MS);
    SDL_Delay(SC_TS_TEST_START_DELAY_MS);

    LOGI("[ts-rA2] ===== begin PhaseA-2 redundancy stress test =====");
    sc_touchscreen_test_case_a_same_slot_multi_move(touchscreen);
    sc_touchscreen_test_case_b_down_then_overwrite(touchscreen);
    sc_touchscreen_test_case_c_two_slots_one_hot(touchscreen);
    sc_touchscreen_test_case_d_param_rewrite(touchscreen);
    LOGI("[ts-rA2] ===== end PhaseA-2 redundancy stress test =====");

    return 0;
}

void
sc_touchscreen_uhid_test_schedule(struct sc_touchscreen_uhid *touchscreen) {
    SDL_Thread *thread = SDL_CreateThread(sc_touchscreen_uhid_test_thread,
                                          "ts-uhid-test", touchscreen);
    if (!thread) {
        LOGE("Could not start touchscreen test thread: %s", SDL_GetError());
        return;
    }

    SDL_DetachThread(thread);
}

#else

void
sc_touchscreen_uhid_test_schedule(struct sc_touchscreen_uhid *touchscreen) {
    (void) touchscreen;
}

#endif
