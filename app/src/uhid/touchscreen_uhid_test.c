#define SC_TOUCHSCREEN_UHID_MANUAL_TEST 1

#include "touchscreen_uhid_test.h"

#include <inttypes.h>
#include <SDL2/SDL_thread.h>
#include <SDL2/SDL_timer.h>

#include "util/log.h"

#define SC_TOUCH_TEST_ROUND 2
#define SC_TOUCH_TEST_START_DELAY_MS 3000
#define SC_TOUCH_TEST_FRAME_DELAY_MS 40

static void
sc_ts_test_sleep(unsigned ms) {
    SDL_Delay(ms);
}

static void
sc_ts_test_begin(struct sc_touchscreen_uhid *touchscreen) {
    sc_touchscreen_uhid_begin_touch_frame(touchscreen);
}

static void
sc_ts_test_end(struct sc_touchscreen_uhid *touchscreen) {
    sc_touchscreen_uhid_end_touch_frame(touchscreen);
}

static void
sc_ts_test_log_emit(const char *tag, const char *kind, uint64_t pid,
                    uint16_t x, uint16_t y, float pressure,
                    uint16_t major, uint16_t minor, uint16_t azimuth) {
    LOGI("[%s] emit %s pid=%" PRIu64 " pos=(%u,%u) p=%.2f major=%u minor=%u az=%u",
         tag, kind, pid, x, y, pressure, major, minor, azimuth);
}

static void
sc_ts_test_down(struct sc_touchscreen_uhid *touchscreen, const char *tag,
                uint64_t pid, uint16_t x, uint16_t y, float pressure,
                uint16_t major, uint16_t minor, uint16_t azimuth) {
    sc_ts_test_log_emit(tag, "DOWN", pid, x, y, pressure, major, minor, azimuth);
    (void) sc_touchscreen_uhid_pointer_down(touchscreen, pid, x, y, pressure,
                                            major, minor, azimuth);
}

static void
sc_ts_test_move(struct sc_touchscreen_uhid *touchscreen, const char *tag,
                uint64_t pid, uint16_t x, uint16_t y, float pressure,
                uint16_t major, uint16_t minor, uint16_t azimuth) {
    sc_ts_test_log_emit(tag, "MOVE", pid, x, y, pressure, major, minor, azimuth);
    (void) sc_touchscreen_uhid_pointer_move(touchscreen, pid, x, y, pressure,
                                            major, minor, azimuth);
}

static void
sc_ts_test_release(struct sc_touchscreen_uhid *touchscreen, const char *tag,
                   uint64_t pid, uint16_t x, uint16_t y, float pressure,
                   uint16_t major, uint16_t minor, uint16_t azimuth) {
    sc_ts_test_log_emit(tag, "RELEASE", pid, x, y, pressure, major, minor, azimuth);
    (void) sc_touchscreen_uhid_pointer_release(touchscreen, pid, x, y, pressure,
                                               major, minor, azimuth);
}

static void
sc_ts_test_finalize(struct sc_touchscreen_uhid *touchscreen, const char *tag,
                    uint64_t pid) {
    LOGI("[%s] finalize pid=%" PRIu64, tag, pid);
    (void) sc_touchscreen_uhid_finalize_pointer(touchscreen, pid);
}

static void
sc_ts_test_clear_all(struct sc_touchscreen_uhid *touchscreen, const char *tag) {
    LOGI("[%s] clear_all_pointers", tag);
    sc_touchscreen_uhid_clear_all_pointers(touchscreen);
}

static void
sc_ts_test_round1(struct sc_touchscreen_uhid *touchscreen) {
    const char *tag = "ts-r1";
    LOGI("[%s] ===== begin round1 single-finger feel test =====", tag);

    LOGI("[%s] case1: slow straight drag", tag);
    sc_ts_test_begin(touchscreen);
    sc_ts_test_down(touchscreen, tag, 1001, 260, 900, 0.40f, 860, 620, 9000);
    sc_ts_test_end(touchscreen);
    sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);
    for (unsigned i = 0; i < 10; ++i) {
        sc_ts_test_begin(touchscreen);
        sc_ts_test_move(touchscreen, tag, 1001,
                        (uint16_t) (290 + i * 30),
                        (uint16_t) (990 + i * 90),
                        0.42f, (uint16_t) (870 + i * 10),
                        (uint16_t) (626 + i * 6), 9000);
        sc_ts_test_end(touchscreen);
        sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);
    }
    sc_ts_test_begin(touchscreen);
    sc_ts_test_release(touchscreen, tag, 1001, 560, 1800, 0.00f, 960, 680, 9000);
    sc_ts_test_end(touchscreen);
    sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);
    sc_ts_test_begin(touchscreen);
    sc_ts_test_finalize(touchscreen, tag, 1001);
    sc_ts_test_end(touchscreen);
    sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);

    LOGI("[%s] ===== end round1 single-finger feel test =====", tag);
}

static void
sc_ts_test_round2(struct sc_touchscreen_uhid *touchscreen) {
    const char *tag = "ts-r2";
    LOGI("[%s] ===== begin round2 multi-finger sync test =====", tag);

    LOGI("[%s] case1: two-finger down, pinch, release, finalize", tag);
    sc_ts_test_begin(touchscreen);
    sc_ts_test_down(touchscreen, tag, 1001, 360, 1200, 0.45f, 900, 650, 7600);
    sc_ts_test_down(touchscreen, tag, 1002, 860, 1200, 0.48f, 940, 680, 10400);
    sc_ts_test_end(touchscreen);
    sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);
    for (unsigned i = 0; i < 7; ++i) {
        sc_ts_test_begin(touchscreen);
        sc_ts_test_move(touchscreen, tag, 1001,
                        (uint16_t) (342 - (int) i * 18),
                        (uint16_t) (1270 + i * 70),
                        0.47f, 920, 660, 7600);
        sc_ts_test_move(touchscreen, tag, 1002,
                        (uint16_t) (878 + i * 18),
                        (uint16_t) (1270 + i * 70),
                        0.50f, 960, 690, 10400);
        sc_ts_test_end(touchscreen);
        sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);
    }
    sc_ts_test_begin(touchscreen);
    sc_ts_test_release(touchscreen, tag, 1001, 252, 1620, 0.00f, 920, 660, 7600);
    sc_ts_test_release(touchscreen, tag, 1002, 968, 1620, 0.00f, 960, 690, 10400);
    sc_ts_test_end(touchscreen);
    sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);
    sc_ts_test_begin(touchscreen);
    sc_ts_test_finalize(touchscreen, tag, 1001);
    sc_ts_test_finalize(touchscreen, tag, 1002);
    sc_ts_test_end(touchscreen);
    sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);

    LOGI("[%s] diag: empty frame after case1 finalize", tag);
    sc_ts_test_begin(touchscreen);
    sc_ts_test_end(touchscreen);
    sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);

    LOGI("[%s] case2: one finger up, other keeps moving", tag);
    sc_ts_test_begin(touchscreen);
    sc_ts_test_down(touchscreen, tag, 1001, 300, 1800, 0.42f, 860, 620, 8200);
    sc_ts_test_down(touchscreen, tag, 1002, 860, 1800, 0.52f, 1040, 740, 9800);
    sc_ts_test_end(touchscreen);
    sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);

    sc_ts_test_begin(touchscreen);
    sc_ts_test_move(touchscreen, tag, 1001, 360, 1880, 0.44f, 880, 630, 8400);
    sc_ts_test_move(touchscreen, tag, 1002, 840, 1880, 0.54f, 1060, 750, 9600);
    sc_ts_test_end(touchscreen);
    sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);

    sc_ts_test_begin(touchscreen);
    sc_ts_test_release(touchscreen, tag, 1001, 360, 1880, 0.00f, 880, 630, 8400);
    sc_ts_test_move(touchscreen, tag, 1002, 810, 1970, 0.55f, 1080, 760, 9500);
    sc_ts_test_end(touchscreen);
    sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);

    sc_ts_test_begin(touchscreen);
    sc_ts_test_finalize(touchscreen, tag, 1001);
    sc_ts_test_move(touchscreen, tag, 1002, 785, 2060, 0.56f, 1080, 760, 9500);
    sc_ts_test_end(touchscreen);
    sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);

    for (unsigned i = 0; i < 3; ++i) {
        sc_ts_test_begin(touchscreen);
        sc_ts_test_move(touchscreen, tag, 1002,
                        (uint16_t) (760 - i * 25),
                        (uint16_t) (2150 + i * 90),
                        0.56f, 1080, 760, 9500);
        sc_ts_test_end(touchscreen);
        sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);
    }
    sc_ts_test_begin(touchscreen);
    sc_ts_test_release(touchscreen, tag, 1002, 710, 2330, 0.00f, 1080, 760, 9500);
    sc_ts_test_end(touchscreen);
    sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);
    sc_ts_test_begin(touchscreen);
    sc_ts_test_finalize(touchscreen, tag, 1002);
    sc_ts_test_end(touchscreen);
    sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);

    LOGI("[%s] diag: empty frame after case2 final finalize", tag);
    sc_ts_test_begin(touchscreen);
    sc_ts_test_end(touchscreen);
    sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);

    LOGI("[%s] diag: second empty frame after case2 final finalize", tag);
    sc_ts_test_begin(touchscreen);
    sc_ts_test_end(touchscreen);
    sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);

    LOGI("[%s] case3: three-finger down, move, clear_all", tag);
    sc_ts_test_begin(touchscreen);
    sc_ts_test_down(touchscreen, tag, 1001, 220, 1500, 0.38f, 820, 600, 7000);
    sc_ts_test_down(touchscreen, tag, 1002, 612, 1500, 0.50f, 980, 680, 9000);
    sc_ts_test_down(touchscreen, tag, 1003, 1004, 1500, 0.62f, 1140, 760, 11000);
    sc_ts_test_end(touchscreen);
    sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);
    for (unsigned i = 0; i < 4; ++i) {
        sc_ts_test_begin(touchscreen);
        sc_ts_test_move(touchscreen, tag, 1001,
                        (uint16_t) (230 + i * 10),
                        (uint16_t) (1555 + i * 55),
                        0.40f, 840, 610, 7200);
        sc_ts_test_move(touchscreen, tag, 1002,
                        612,
                        (uint16_t) (1560 + i * 60),
                        0.52f, 1000, 690, 9000);
        sc_ts_test_move(touchscreen, tag, 1003,
                        (uint16_t) (994 - i * 10),
                        (uint16_t) (1555 + i * 55),
                        0.64f, 1160, 770, 10800);
        sc_ts_test_end(touchscreen);
        sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);
    }
    LOGI("[%s] diag: clear_all wrapped in explicit frame", tag);
    sc_ts_test_begin(touchscreen);
    sc_ts_test_clear_all(touchscreen, tag);
    sc_ts_test_end(touchscreen);
    sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);

    LOGI("[%s] diag: empty frame after clear_all", tag);
    sc_ts_test_begin(touchscreen);
    sc_ts_test_end(touchscreen);
    sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);

    LOGI("[%s] ===== end round2 multi-finger sync test =====", tag);
}

static void
sc_ts_test_round3(struct sc_touchscreen_uhid *touchscreen) {
    const char *tag = "ts-r3";
    LOGI("[%s] ===== begin round3 contact-profile test =====", tag);

    LOGI("[%s] case1: pressure ladder", tag);
    sc_ts_test_begin(touchscreen);
    sc_ts_test_down(touchscreen, tag, 1001, 612, 900, 0.15f, 700, 520, 9000);
    sc_ts_test_end(touchscreen);
    sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);
    for (unsigned i = 0; i < 6; ++i) {
        sc_ts_test_begin(touchscreen);
        sc_ts_test_move(touchscreen, tag, 1001, 612,
                        (uint16_t) (1010 + i * 110),
                        0.27f + 0.12f * i,
                        (uint16_t) (720 + i * 20),
                        (uint16_t) (532 + i * 12),
                        9000);
        sc_ts_test_end(touchscreen);
        sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);
    }
    sc_ts_test_begin(touchscreen);
    sc_ts_test_release(touchscreen, tag, 1001, 612, 1560, 0.00f, 820, 592, 9000);
    sc_ts_test_end(touchscreen);
    sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);
    sc_ts_test_begin(touchscreen);
    sc_ts_test_finalize(touchscreen, tag, 1001);
    sc_ts_test_end(touchscreen);
    sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);

    LOGI("[%s] case2: orientation half-range sweep", tag);
    sc_ts_test_begin(touchscreen);
    sc_ts_test_down(touchscreen, tag, 1001, 260, 2100, 0.42f, 980, 620, 3000);
    sc_ts_test_down(touchscreen, tag, 1002, 612, 2100, 0.52f, 980, 620, 9000);
    sc_ts_test_down(touchscreen, tag, 1003, 964, 2100, 0.62f, 980, 620, 15000);
    sc_ts_test_end(touchscreen);
    sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);
    for (unsigned i = 0; i < 5; ++i) {
        sc_ts_test_begin(touchscreen);
        sc_ts_test_move(touchscreen, tag, 1001, 260,
                        (uint16_t) (2160 + i * 60), 0.44f, 1020, 660,
                        (uint16_t) (3600 + i * 600));
        sc_ts_test_move(touchscreen, tag, 1002, 612,
                        (uint16_t) (2160 + i * 60), 0.54f, 1020, 660, 9000);
        sc_ts_test_move(touchscreen, tag, 1003, 964,
                        (uint16_t) (2160 + i * 60), 0.64f, 1020, 660,
                        (uint16_t) (14400 - i * 600));
        sc_ts_test_end(touchscreen);
        sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);
    }
    sc_ts_test_begin(touchscreen);
    sc_ts_test_release(touchscreen, tag, 1001, 260, 2400, 0.00f, 1020, 660, 6000);
    sc_ts_test_release(touchscreen, tag, 1002, 612, 2400, 0.00f, 1020, 660, 9000);
    sc_ts_test_release(touchscreen, tag, 1003, 964, 2400, 0.00f, 1020, 660, 12000);
    sc_ts_test_end(touchscreen);
    sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);
    sc_ts_test_begin(touchscreen);
    sc_ts_test_finalize(touchscreen, tag, 1001);
    sc_ts_test_finalize(touchscreen, tag, 1002);
    sc_ts_test_finalize(touchscreen, tag, 1003);
    sc_ts_test_end(touchscreen);
    sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);

    LOGI("[%s] ===== end round3 contact-profile test =====", tag);
}

static void
sc_ts_test_round4(struct sc_touchscreen_uhid *touchscreen) {
    const char *tag = "ts-r4";
    LOGI("[%s] ===== begin round4 direct-api regression test =====", tag);

    LOGI("[%s] caseA: same-slot multi-move in one frame", tag);
    sc_ts_test_begin(touchscreen);
    sc_ts_test_down(touchscreen, tag, 1001, 320, 900, 0.38f, 820, 600, 9000);
    sc_ts_test_end(touchscreen);
    sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);
    sc_ts_test_begin(touchscreen);
    sc_ts_test_move(touchscreen, tag, 1001, 350, 980, 0.40f, 840, 610, 9100);
    sc_ts_test_move(touchscreen, tag, 1001, 390, 1070, 0.44f, 860, 620, 9300);
    sc_ts_test_move(touchscreen, tag, 1001, 430, 1170, 0.49f, 890, 635, 9500);
    sc_ts_test_end(touchscreen);
    sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);
    sc_ts_test_begin(touchscreen);
    sc_ts_test_release(touchscreen, tag, 1001, 430, 1170, 0.00f, 890, 635, 9500);
    sc_ts_test_end(touchscreen);
    sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);
    sc_ts_test_begin(touchscreen);
    sc_ts_test_finalize(touchscreen, tag, 1001);
    sc_ts_test_end(touchscreen);
    sc_ts_test_sleep(SC_TOUCH_TEST_FRAME_DELAY_MS);

    LOGI("[%s] ===== end round4 direct-api regression test =====", tag);
}

static int
sc_ts_test_thread(void *userdata) {
    struct sc_touchscreen_uhid *touchscreen = userdata;

    LOGI("[ts-r%d] scheduled, waiting %d ms before start",
         SC_TOUCH_TEST_ROUND, SC_TOUCH_TEST_START_DELAY_MS);
    sc_ts_test_sleep(SC_TOUCH_TEST_START_DELAY_MS);

    switch (SC_TOUCH_TEST_ROUND) {
        case 1:
            sc_ts_test_round1(touchscreen);
            break;
        case 2:
            sc_ts_test_round2(touchscreen);
            break;
        case 3:
            sc_ts_test_round3(touchscreen);
            break;
        case 4:
        default:
            sc_ts_test_round4(touchscreen);
            break;
    }

    return 0;
}

void
sc_touchscreen_uhid_test_schedule(struct sc_touchscreen_uhid *touchscreen) {
    LOGI("[ts-r%d] schedule requested", SC_TOUCH_TEST_ROUND);
    SDL_Thread *thread = SDL_CreateThread(sc_ts_test_thread,
                                          "ts-uhid-test",
                                          touchscreen);
    if (!thread) {
        LOGW("[ts-r%d] could not create test thread", SC_TOUCH_TEST_ROUND);
        return;
    }
    SDL_DetachThread(thread);
}

