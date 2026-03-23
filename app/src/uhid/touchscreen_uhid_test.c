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
#define SC_TS_TEST_START_DELAY_MS 3000

typedef struct sc_ts_test_sample { enum sc_touch_action action; uint64_t pointer_id; int32_t x; int32_t y; float pressure; uint16_t touch_major; uint16_t touch_minor; uint16_t azimuth; } sc_ts_test_sample;
static void begin(struct sc_touchscreen_uhid *ts){ struct sc_touch_processor *tp=&ts->touch_processor; if(tp->ops->begin_touch_update) tp->ops->begin_touch_update(tp);} 
static void endb(struct sc_touchscreen_uhid *ts){ struct sc_touch_processor *tp=&ts->touch_processor; if(tp->ops->end_touch_update) tp->ops->end_touch_update(tp);} 
static void emit(struct sc_touchscreen_uhid *ts,const sc_ts_test_sample *s){ struct sc_touch_event e={.position={.screen_size={.width=SC_TS_TEST_SCREEN_W,.height=SC_TS_TEST_SCREEN_H},.point={.x=s->x,.y=s->y}},.action=s->action,.pointer_id=s->pointer_id,.pressure=s->pressure,.touch_major=s->touch_major,.touch_minor=s->touch_minor,.azimuth=s->azimuth}; LOGI("[ts-r2] emit action=%d pid=%" PRIu64 " pos=(%d,%d) p=%.2f major=%u minor=%u az=%u",(int)s->action,s->pointer_id,s->x,s->y,s->pressure,s->touch_major,s->touch_minor,s->azimuth); ts->touch_processor.ops->process_touch(&ts->touch_processor,&e);} 
static void batch(struct sc_touchscreen_uhid *ts,const sc_ts_test_sample *samples,size_t n){ begin(ts); for(size_t i=0;i<n;++i) emit(ts,&samples[i]); endb(ts);} 

static void case_dual_pinch(struct sc_touchscreen_uhid *ts){
    LOGI("[ts-r2] case1: dual-finger pinch");
    sc_ts_test_sample down[]={
        {SC_TOUCH_ACTION_DOWN,SC_TS_TEST_P1,320,1100,0.42f,900,640,7600},
        {SC_TOUCH_ACTION_DOWN,SC_TS_TEST_P2,900,1100,0.48f,960,700,10400},
    };
    batch(ts,down,2); SDL_Delay(500);
    for(int i=1;i<=6;++i){ sc_ts_test_sample mv[]={
        {SC_TOUCH_ACTION_MOVE,SC_TS_TEST_P1,320+i*35,1100+i*70,0.44f,920,650,(uint16_t)(7600+i*150)},
        {SC_TOUCH_ACTION_MOVE,SC_TS_TEST_P2,900-i*35,1100+i*70,0.50f,980,710,(uint16_t)(10400-i*150)},
    }; batch(ts,mv,2); SDL_Delay(90);} 
    sc_ts_test_sample up[]={
        {SC_TOUCH_ACTION_UP,SC_TS_TEST_P1,530,1520,0.0f,920,650,8500},
        {SC_TOUCH_ACTION_UP,SC_TS_TEST_P2,690,1520,0.0f,980,710,9500},
    }; batch(ts,up,2); SDL_Delay(700);
}

static void case_one_up_other_moves(struct sc_touchscreen_uhid *ts){
    LOGI("[ts-r2] case2: one finger up, the other keeps moving");
    sc_ts_test_sample down[]={
        {SC_TOUCH_ACTION_DOWN,SC_TS_TEST_P1,260,1700,0.40f,880,620,7600},
        {SC_TOUCH_ACTION_DOWN,SC_TS_TEST_P2,900,1700,0.55f,1120,760,10400},
    };
    batch(ts,down,2); SDL_Delay(400);
    for(int i=1;i<=3;++i){ sc_ts_test_sample mv[]={
        {SC_TOUCH_ACTION_MOVE,SC_TS_TEST_P1,260+i*30,1700+i*60,0.42f,900,630,7800},
        {SC_TOUCH_ACTION_MOVE,SC_TS_TEST_P2,900-i*20,1700+i*40,0.56f,1120,760,10400},
    }; batch(ts,mv,2); SDL_Delay(80);} 
    sc_ts_test_sample up1={SC_TOUCH_ACTION_UP,SC_TS_TEST_P1,350,1880,0.0f,900,630,7800}; batch(ts,&up1,1); SDL_Delay(250);
    for(int i=1;i<=4;++i){ sc_ts_test_sample mv={SC_TOUCH_ACTION_MOVE,SC_TS_TEST_P2,840-i*20,1820+i*70,0.58f,1140,770,10400}; batch(ts,&mv,1); SDL_Delay(80);} 
    sc_ts_test_sample up2={SC_TOUCH_ACTION_UP,SC_TS_TEST_P2,760,2100,0.0f,1140,770,10400}; batch(ts,&up2,1); SDL_Delay(700);
}

static void case_three_down_reset(struct sc_touchscreen_uhid *ts){
    LOGI("[ts-r2] case3: three-finger down then reset");
    sc_ts_test_sample down[]={
        {SC_TOUCH_ACTION_DOWN,SC_TS_TEST_P1,260,2200,0.38f,860,620,6600},
        {SC_TOUCH_ACTION_DOWN,SC_TS_TEST_P2,612,2200,0.52f,980,700,9000},
        {SC_TOUCH_ACTION_DOWN,SC_TS_TEST_P3,964,2200,0.66f,1160,820,11400},
    };
    batch(ts,down,3); SDL_Delay(1000);
    LOGI("[ts-r2] case3: reset");
    sc_touchscreen_uhid_reset(ts); SDL_Delay(800);
}

static int SDLCALL thread_main(void *userdata){ struct sc_touchscreen_uhid *ts=userdata; LOGI("[ts-r2] scheduled, waiting %d ms before start",SC_TS_TEST_START_DELAY_MS); SDL_Delay(SC_TS_TEST_START_DELAY_MS); LOGI("[ts-r2] ===== begin round2 multi-finger sync test ====="); case_dual_pinch(ts); case_one_up_other_moves(ts); case_three_down_reset(ts); LOGI("[ts-r2] ===== end round2 multi-finger sync test ====="); return 0; }
void sc_touchscreen_uhid_test_schedule(struct sc_touchscreen_uhid *ts){ SDL_Thread *thread=SDL_CreateThread(thread_main,"ts-r2-test",ts); if(!thread){ LOGE("Could not start touchscreen round2 test thread: %s",SDL_GetError()); return;} SDL_DetachThread(thread);} 
#else
void sc_touchscreen_uhid_test_schedule(struct sc_touchscreen_uhid *ts){ (void)ts; }
#endif
