#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "ui.h"

int textUpdateCounter = 0; 
lv_timer_t *textTimer;
bool backlightHigh = true;

void textTimerHandler( lv_timer_t * timer );

#ifdef __cplusplus
   }
#endif