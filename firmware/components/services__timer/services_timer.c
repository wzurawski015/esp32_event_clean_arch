#include "services_timer.h"
#include "core_ev.h"
#include "esp_timer.h"

static esp_timer_handle_t s_t100 = NULL;
static esp_timer_handle_t s_t1s  = NULL;

static void t100_cb(void* arg){ (void)arg; ev_post(EV_SRC_TIMER, EV_TICK_100MS, 0, 0); }
static void t1s_cb(void* arg){ (void)arg; ev_post(EV_SRC_TIMER, EV_TICK_1S, 0, 0); }

bool services_timer_start(void){
    esp_timer_create_args_t a100 = {.callback=t100_cb, .name="t100ms"};
    esp_timer_create_args_t a1s  = {.callback=t1s_cb,  .name="t1s"};
    if (esp_timer_create(&a100,&s_t100)!=ESP_OK) return false;
    if (esp_timer_create(&a1s, &s_t1s )!=ESP_OK) return false;
    if (esp_timer_start_periodic(s_t100, 100000) != ESP_OK) return false; // 100ms
    if (esp_timer_start_periodic(s_t1s, 1000000) != ESP_OK) return false; // 1s
    return true;
}

void services_timer_stop(void){
    if (s_t100){ esp_timer_stop(s_t100); esp_timer_delete(s_t100); s_t100=NULL; }
    if (s_t1s){  esp_timer_stop(s_t1s ); esp_timer_delete(s_t1s ); s_t1s =NULL; }
}
