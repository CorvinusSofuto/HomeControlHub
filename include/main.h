#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

extern lv_obj_t * uiInitialActions;
extern lv_obj_t * uiScreen;

void initializeUI( void );
void ui_Screen1_screen_init( void );
void createHelloWorld( void );
  
#ifdef __cplusplus
   }
#endif