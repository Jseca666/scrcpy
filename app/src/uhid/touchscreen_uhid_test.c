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

#define SC_TS_TEST_START_DELAY_MS        3000
#define SC_TS_TEST_STEP_DELAY_MS          220
#define SC_TS_TEST_HOLD_SHORT_MS          900
#define SC_TS_TEST_HOLD_LONG_MS          1800
#define SC_TS_TEST_REPEAT_COUNT             5

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
sc_touchscreen_test_end(struct sc_touch_processor *tp) {
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

    LOGI("[ts-r1] emit action=%d pid=%" PRIu64
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
    struct sc_touch_processor *tp = &touchscreen->touch_processor;

    sc_touchscreen_test_begin(touchscreen);
    for (size_t i = 0; i < count; ++i) {
        sc_touchscreen_test_emit_sample(touchscreen, &samples[i]);
    }
    sc_touchscreen_test_end(tp);
}

static void
sc_touchscreen_round1_case_split_up(struct sc_touchscreen_uhid *touchscreen) {
    static const struct sc_ts_test_sample down2[] = {
        {SC_TOUCH_ACTION_DOWN, SC_TS_TEST_P1, 320, 920, 0.48f, 960, 680, 7600},
        {SC_TOUCH_ACTION_DOWN, SC_TS_TEST_P2, 900, 920, 0.56f, 1180, 760, 10400},
    };

    LOGI("[ts-r1] case1: two-finger down -> split up");
    sc_touchscreen_test_emit_batch(touchscreen, down2, 2);
    SDL_Delay(SC_TS_TEST_HOLD_SHORT_MS);

    for (int i = 1; i <= 4; ++i) {
        struct sc_ts_test_sample move2[] = {
            {SC_TOUCH_ACTION_MOVE, SC_TS_TEST_P1, 320 - i * 15, 920 + i * 95,
             0.48f + 0.03f * i, (uint16_t) (960 + 28 * i),
             (uint16_t) (680 + 14 * i), (uint16_t) (7600 + 200 * i)},
            {SC_TOUCH_ACTION_MOVE, SC_TS_TEST_P2, 900 + i * 15, 920 + i * 95,
             0.56f + 0.02f * i, (uint16_t) (1180 + 16 * i),
             (uint16_t) (760 + 8 * i), (uint16_t) (10400 - 170 * i)},
        };
        sc_touchscreen_test_emit_batch(touchscreen, move2, 2);
        SDL_Delay(SC_TS_TEST_STEP_DELAY_MS);
    }

    {
        static const struct sc_ts_test_sample up1[] = {
            {SC_TOUCH_ACTION_UP, SC_TS_TEST_P1, 260, 1300, 0.0f, 1040, 740, 8400},
        };
        LOGI("[ts-r1] case1: up first finger only");
        sc_touchscreen_test_emit_batch(touchscreen, up1, 1);
        SDL_Delay(SC_TS_TEST_HOLD_SHORT_MS);
    }

    for (int i = 1; i <= 4; ++i) {
        struct sc_ts_test_sample move1[] = {
            {SC_TOUCH_ACTION_MOVE, SC_TS_TEST_P2, 960 + i * 10, 1300 + i * 85,
             0.60f, (uint16_t) (1220 + 8 * i), (uint16_t) (792 + 4 * i), 9300},
        };
        sc_touchscreen_test_emit_batch(touchscreen, move1, 1);
        SDL_Delay(SC_TS_TEST_STEP_DELAY_MS);
    }

    {
        static const struct sc_ts_test_sample up2[] = {
            {SC_TOUCH_ACTION_UP, SC_TS_TEST_P2, 1000, 1640, 0.0f, 1220, 800, 9300},
        };
        LOGI("[ts-r1] case1: up second finger");
        sc_touchscreen_test_emit_batch(touchscreen, up2, 1);
        SDL_Delay(SC_TS_TEST_HOLD_SHORT_MS);
    }
}

static void
sc_touchscreen_round1_case_middle_up(struct sc_touchscreen_uhid *touchscreen) {
    static const struct sc_ts_test_sample down3[] = {
        {SC_TOUCH_ACTION_DOWN, SC_TS_TEST_P1, 250, 1480, 0.40f, 900, 620, 7000},
        {SC_TOUCH_ACTION_DOWN, SC_TS_TEST_P2, 612, 1480, 0.55f, 1120, 760, 9000},
        {SC_TOUCH_ACTION_DOWN, SC_TS_TEST_P3, 974, 1480, 0.68f, 1320, 860, 11000},
    };

    LOGI("[ts-r1] case2: three-finger down -> middle up");
    sc_touchscreen_test_emit_batch(touchscreen, down3, 3);
    SDL_Delay(SC_TS_TEST_HOLD_SHORT_MS);

    {
        static const struct sc_ts_test_sample up_mid[] = {
            {SC_TOUCH_ACTION_UP, SC_TS_TEST_P2, 612, 1480, 0.0f, 1120, 760, 9000},
        };
        LOGI("[ts-r1] case2: up middle finger only");
        sc_touchscreen_test_emit_batch(touchscreen, up_mid, 1);
        SDL_Delay(SC_TS_TEST_HOLD_SHORT_MS);
    }

    for (int i = 1; i <= 4; ++i) {
        struct sc_ts_test_sample move2[] = {
            {SC_TOUCH_ACTION_MOVE, SC_TS_TEST_P1, 250 - i * 10, 1480 + i * 70,
             0.46f, (uint16_t) (920 + 16 * i), (uint16_t) (632 + 8 * i),
             (uint16_t) (7000 + 120 * i)},
            {SC_TOUCH_ACTION_MOVE, SC_TS_TEST_P3, 974 + i * 10, 1480 + i * 70,
             0.72f, (uint16_t) (1340 + 16 * i), (uint16_t) (872 + 8 * i),
             (uint16_t) (11000 - 120 * i)},
        };
        sc_touchscreen_test_emit_batch(touchscreen, move2, 2);
        SDL_Delay(SC_TS_TEST_STEP_DELAY_MS);
    }

    {
        static const struct sc_ts_test_sample up_rest[] = {
            {SC_TOUCH_ACTION_UP, SC_TS_TEST_P1, 210, 1760, 0.0f, 980, 680, 7600},
            {SC_TOUCH_ACTION_UP, SC_TS_TEST_P3, 1014, 1760, 0.0f, 1380, 900, 10200},
        };
        LOGI("[ts-r1] case2: up remaining fingers");
        sc_touchscreen_test_emit_batch(touchscreen, up_rest, 2);
        SDL_Delay(SC_TS_TEST_HOLD_SHORT_MS);
    }
}

static void
sc_touchscreen_round1_case_long_press_reset(struct sc_touchscreen_uhid *touchscreen) {
    static const struct sc_ts_test_sample down2[] = {
        {SC_TOUCH_ACTION_DOWN, SC_TS_TEST_P1, 360, 1780, 0.50f, 980, 700, 8200},
        {SC_TOUCH_ACTION_DOWN, SC_TS_TEST_P2, 860, 1780, 0.62f, 1180, 790, 9800},
    };

    LOGI("[ts-r1] case3: long press two fingers then reset");
    sc_touchscreen_test_emit_batch(touchscreen, down2, 2);
    SDL_Delay(SC_TS_TEST_HOLD_LONG_MS);
    sc_touchscreen_uhid_reset(touchscreen);
    SDL_Delay(SC_TS_TEST_HOLD_SHORT_MS);

    {
        static const struct sc_ts_test_sample down3[] = {
            {SC_TOUCH_ACTION_DOWN, SC_TS_TEST_P1, 260, 2080, 0.44f, 920, 640, 7400},
            {SC_TOUCH_ACTION_DOWN, SC_TS_TEST_P2, 612, 2080, 0.58f, 1120, 760, 9000},
            {SC_TOUCH_ACTION_DOWN, SC_TS_TEST_P3, 964, 2080, 0.72f, 1320, 880, 10600},
        };
        LOGI("[ts-r1] case3: long press three fingers then reset");
        sc_touchscreen_test_emit_batch(touchscreen, down3, 3);
        SDL_Delay(SC_TS_TEST_HOLD_LONG_MS);
        sc_touchscreen_uhid_reset(touchscreen);
        SDL_Delay(SC_TS_TEST_HOLD_SHORT_MS);
    }
}

static void
sc_touchscreen_round1_case_repeat(struct sc_touchscreen_uhid *touchscreen) {
    LOGI("[ts-r1] case4: repeat two-finger down/up %d rounds", SC_TS_TEST_REPEAT_COUNT);

    for (int round = 1; round <= SC_TS_TEST_REPEAT_COUNT; ++round) {
        struct sc_ts_test_sample down2[] = {
            {SC_TOUCH_ACTION_DOWN, SC_TS_TEST_P1, 360, 1180, 0.45f, 900, 640, 7600},
            {SC_TOUCH_ACTION_DOWN, SC_TS_TEST_P2, 860, 1180, 0.57f, 1140, 760, 10200},
        };
        struct sc_ts_test_sample up2[] = {
            {SC_TOUCH_ACTION_UP, SC_TS_TEST_P1, 360, 1180, 0.0f, 900, 640, 7600},
            {SC_TOUCH_ACTION_UP, SC_TS_TEST_P2, 860, 1180, 0.0f, 1140, 760, 10200},
        };

        LOGI("[ts-r1] case4: round %d", round);
        sc_touchscreen_test_emit_batch(touchscreen, down2, 2);
        SDL_Delay(350);
        sc_touchscreen_test_emit_batch(touchscreen, up2, 2);
        SDL_Delay(350);
    }

    SDL_Delay(SC_TS_TEST_HOLD_SHORT_MS);
}

static int SDLCALL
sc_touchscreen_uhid_test_thread(void *userdata) {
    struct sc_touchscreen_uhid *touchscreen = userdata;

    LOGI("[ts-r1] scheduled, waiting %d ms before start", SC_TS_TEST_START_DELAY_MS);
    SDL_Delay(SC_TS_TEST_START_DELAY_MS);

    LOGI("[ts-r1] ===== begin round1 lifecycle stability test =====");
    sc_touchscreen_round1_case_split_up(touchscreen);
    sc_touchscreen_round1_case_middle_up(touchscreen);
    sc_touchscreen_round1_case_long_press_reset(touchscreen);
    sc_touchscreen_round1_case_repeat(touchscreen);
    LOGI("[ts-r1] ===== end round1 lifecycle stability test =====");
    return 0;
}

void
sc_touchscreen_uhid_test_schedule(struct sc_touchscreen_uhid *touchscreen) {
    SDL_Thread *thread = SDL_CreateThread(sc_touchscreen_uhid_test_thread,
                                          "ts-uhid-test-r1", touchscreen);
    if (!thread) {
        LOGE("Could not start touchscreen round1 test thread: %s", SDL_GetError());
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
