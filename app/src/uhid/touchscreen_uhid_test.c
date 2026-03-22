#include "touchscreen_uhid_test.h"
#define SC_TOUCHSCREEN_UHID_MANUAL_TEST 1
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

/* 整体节奏放慢 */
#define SC_TS_TEST_START_DELAY_MS       3000
#define SC_TS_TEST_HOLD_AFTER_DOWN_MS   1400
#define SC_TS_TEST_MOVE_STEP_DELAY_MS    180
#define SC_TS_TEST_HOLD_BEFORE_UP_MS    1000
#define SC_TS_TEST_HOLD_AFTER_UP_MS     1200
#define SC_TS_TEST_HOLD_BEFORE_RESET_MS 1200
#define SC_TS_TEST_HOLD_AFTER_RESET_MS  1200

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

    LOGI("[ts-test] emit action=%d pid=%" PRIu64
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
sc_touchscreen_test_case_down_2(struct sc_touchscreen_uhid *touchscreen) {
    static const struct sc_ts_test_sample batch[] = {
        {
            .action = SC_TOUCH_ACTION_DOWN,
            .pointer_id = SC_TS_TEST_P1,
            .x = 320, .y = 900,
            .pressure = 0.42f,
            .touch_major = 920,
            .touch_minor = 640,
            .azimuth = 7200,
        },
        {
            .action = SC_TOUCH_ACTION_DOWN,
            .pointer_id = SC_TS_TEST_P2,
            .x = 900, .y = 900,
            .pressure = 0.63f,
            .touch_major = 1280,
            .touch_minor = 760,
            .azimuth = 10800,
        },
    };

    LOGI("[ts-test] case1: two-finger DOWN");
    sc_touchscreen_test_emit_batch(touchscreen, batch,
                                   sizeof(batch) / sizeof(batch[0]));
    SDL_Delay(SC_TS_TEST_HOLD_AFTER_DOWN_MS);
}

static void
sc_touchscreen_test_case_move_2(struct sc_touchscreen_uhid *touchscreen) {
    LOGI("[ts-test] case2: two-finger MOVE");

    for (int i = 1; i <= 8; ++i) {
        struct sc_ts_test_sample batch[] = {
            {
                .action = SC_TOUCH_ACTION_MOVE,
                .pointer_id = SC_TS_TEST_P1,
                .x = 320 - i * 14,
                .y = 900 + i * 95,
                .pressure = 0.42f + 0.04f * i,
                .touch_major = (uint16_t) (920 + 35 * i),
                .touch_minor = (uint16_t) (640 + 16 * i),
                .azimuth = (uint16_t) (7200 + 220 * i),
            },
            {
                .action = SC_TOUCH_ACTION_MOVE,
                .pointer_id = SC_TS_TEST_P2,
                .x = 900 + i * 14,
                .y = 900 + i * 95,
                .pressure = 0.63f - 0.025f * i,
                .touch_major = (uint16_t) (1280 - 22 * i),
                .touch_minor = (uint16_t) (760 + 10 * i),
                .azimuth = (uint16_t) (10800 - 180 * i),
            },
        };

        sc_touchscreen_test_emit_batch(touchscreen, batch,
                                       sizeof(batch) / sizeof(batch[0]));
        SDL_Delay(SC_TS_TEST_MOVE_STEP_DELAY_MS);
    }

    SDL_Delay(SC_TS_TEST_HOLD_BEFORE_UP_MS);
}

static void
sc_touchscreen_test_case_up_2(struct sc_touchscreen_uhid *touchscreen) {
    static const struct sc_ts_test_sample batch[] = {
        {
            .action = SC_TOUCH_ACTION_UP,
            .pointer_id = SC_TS_TEST_P1,
            .x = 208, .y = 1660,
            .pressure = 0.00f,
            .touch_major = 980,
            .touch_minor = 700,
            .azimuth = 8600,
        },
        {
            .action = SC_TOUCH_ACTION_UP,
            .pointer_id = SC_TS_TEST_P2,
            .x = 1012, .y = 1660,
            .pressure = 0.00f,
            .touch_major = 1120,
            .touch_minor = 780,
            .azimuth = 9400,
        },
    };

    LOGI("[ts-test] case3: two-finger UP");
    sc_touchscreen_test_emit_batch(touchscreen, batch,
                                   sizeof(batch) / sizeof(batch[0]));
    SDL_Delay(SC_TS_TEST_HOLD_AFTER_UP_MS);
}

static void
sc_touchscreen_test_case_down_move_reset_3(struct sc_touchscreen_uhid *touchscreen) {
    static const struct sc_ts_test_sample down3[] = {
        {
            .action = SC_TOUCH_ACTION_DOWN,
            .pointer_id = SC_TS_TEST_P1,
            .x = 260, .y = 1500,
            .pressure = 0.38f,
            .touch_major = 860,
            .touch_minor = 620,
            .azimuth = 6600,
        },
        {
            .action = SC_TOUCH_ACTION_DOWN,
            .pointer_id = SC_TS_TEST_P2,
            .x = 612, .y = 1500,
            .pressure = 0.55f,
            .touch_major = 1100,
            .touch_minor = 760,
            .azimuth = 9000,
        },
        {
            .action = SC_TOUCH_ACTION_DOWN,
            .pointer_id = SC_TS_TEST_P3,
            .x = 964, .y = 1500,
            .pressure = 0.72f,
            .touch_major = 1360,
            .touch_minor = 880,
            .azimuth = 11400,
        },
    };

    LOGI("[ts-test] case4: three-finger DOWN");
    sc_touchscreen_test_emit_batch(touchscreen, down3,
                                   sizeof(down3) / sizeof(down3[0]));
    SDL_Delay(SC_TS_TEST_HOLD_BEFORE_RESET_MS);

    LOGI("[ts-test] case4: three-finger MOVE");
    for (int i = 1; i <= 4; ++i) {
        struct sc_ts_test_sample move3[] = {
            {
                .action = SC_TOUCH_ACTION_MOVE,
                .pointer_id = SC_TS_TEST_P1,
                .x = 260 - i * 10,
                .y = 1500 + i * 55,
                .pressure = 0.38f + 0.03f * i,
                .touch_major = (uint16_t) (860 + 22 * i),
                .touch_minor = (uint16_t) (620 + 10 * i),
                .azimuth = (uint16_t) (6600 + 180 * i),
            },
            {
                .action = SC_TOUCH_ACTION_MOVE,
                .pointer_id = SC_TS_TEST_P2,
                .x = 612,
                .y = 1500 + i * 60,
                .pressure = 0.55f + 0.02f * i,
                .touch_major = (uint16_t) (1100 + 18 * i),
                .touch_minor = (uint16_t) (760 + 10 * i),
                .azimuth = 9000,
            },
            {
                .action = SC_TOUCH_ACTION_MOVE,
                .pointer_id = SC_TS_TEST_P3,
                .x = 964 + i * 10,
                .y = 1500 + i * 55,
                .pressure = 0.72f + 0.02f * i,
                .touch_major = (uint16_t) (1360 + 15 * i),
                .touch_minor = (uint16_t) (880 + 8 * i),
                .azimuth = (uint16_t) (11400 - 160 * i),
            },
        };

        sc_touchscreen_test_emit_batch(touchscreen, move3,
                                       sizeof(move3) / sizeof(move3[0]));
        SDL_Delay(SC_TS_TEST_MOVE_STEP_DELAY_MS);
    }

    SDL_Delay(SC_TS_TEST_HOLD_BEFORE_RESET_MS);

    LOGI("[ts-test] case4: reset while three fingers are active");
    sc_touchscreen_uhid_reset(touchscreen);
    SDL_Delay(SC_TS_TEST_HOLD_AFTER_RESET_MS);
}

static int SDLCALL
sc_touchscreen_uhid_test_thread(void *userdata) {
    struct sc_touchscreen_uhid *touchscreen = userdata;

    LOGI("[ts-test] scheduled, waiting %d ms before start",
         SC_TS_TEST_START_DELAY_MS);
    SDL_Delay(SC_TS_TEST_START_DELAY_MS);

    LOGI("[ts-test] ===== begin full-field multi-touch test =====");
    sc_touchscreen_test_case_down_2(touchscreen);
    sc_touchscreen_test_case_move_2(touchscreen);
    sc_touchscreen_test_case_up_2(touchscreen);
    sc_touchscreen_test_case_down_move_reset_3(touchscreen);
    LOGI("[ts-test] ===== end full-field multi-touch test =====");

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