#include "pins_config.h"
#include "LovyanGFX_Driver.h"

#include <Arduino.h>
#include <lvgl.h>
#include <Wire.h>
#include <SPI.h>

#include <stdbool.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>

#include "main.h"

// Unified Global Instantiation Wrapper
LGFX gfx;

static lv_disp_draw_buf_t lvDisplayDrawBuffer;
static lv_color_t *lvColorBuffer1 = NULL;
static lv_color_t *lvColorBuffer2 = NULL;

lv_obj_t * uiScreen;

uint16_t touchX, touchY;


/// @brief Flushes a specific area of the display with the provided color data. This function is called by LVGL when it needs to update a portion of the display. It uses the LovyanGFX library to push the image data to the display using DMA for efficient transfer.
/// @param disp 
/// @param area 
/// @param color_p 
void displayFlush( lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p )
   {
    // Complete any previous refresh operation before starting a new one
   if( gfx.getStartCount() > 0 )
      {
      gfx.endWrite();
      }

   gfx.pushImageDMA( area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1, ( lgfx::rgb565_t * )&color_p->full );

   lv_disp_flush_ready( disp ); // Tell lvgl that the refresh is complete
   }

/// @brief Reads the touch input from the display. This function is called by LVGL when it needs to update the touch input state.
void touchscreenRead( lv_indev_drv_t * indev_driver, lv_indev_data_t * data )
   {
   data->state = LV_INDEV_STATE_REL;// The state of data existence when releasing the finger
   bool touched = gfx.getTouch( &touchX, &touchY );

   if( touched )
      {
      data->state = LV_INDEV_STATE_PR;

      //  Set coordinates
      data->point.x = touchX;
      data->point.y = touchY;
      }
   }

// @brief Scans the I2C bus for a device at the specified address.
/// @param address The I2C address to scan for.
/// @return true if a device is found at the address, false otherwise.
bool i2cScanForAddress( uint8_t address )
   {
   Wire.beginTransmission( address );
   return ( Wire.endTransmission() == 0 );
   }

/// @brief Sends a command to the I2C device at address 0x30. This function is used to control the backlight of the display.
/// @param command The command byte to send. For example, 0 turns the backlight
void sendI2CCommand( uint8_t command )
   {
   uint8_t error;
   // Start sending commands to the specified address
   Wire.beginTransmission( 0x30 );
   // Send command
   Wire.write( command );
   //  End transmission and return status
   error = Wire.endTransmission();

   if( error == 0 )
      {
      Serial.print( "command 0x" );
      Serial.print( command, HEX );
      Serial.println( " Sent successfully" );
      }
   else
      {
      Serial.print( "Command sent error, error code:" );
      Serial.println( error );
      }
   }

/// @brief Initializes the system, including serial communication, I2C communication, display, and LVGL. It also checks for the presence of specific I2C devices and configures the backlight of the display.
void setup()
   {
   Serial.begin( 115200 );

   // Initialize physical I2C pins for bus controller expansion tasks
   pinMode( 19, OUTPUT );
   Wire.begin( 15, 16 );
   delay( 50 );

   // Coprocessor Wake Routine
   int retry = 0;

   while( 1 )
      {
      if( i2cScanForAddress( 0x30 ) && i2cScanForAddress( 0x5D ) )
         {
         Serial.print( "The microcontroller is detected: address 0x" );
         Serial.println( 0x30, HEX );
         Serial.print( "The microcontroller is detected: address 0x" );
         Serial.println( 0x5D, HEX );
         break;
         }
      else
         {
         Serial.print( "No microcontroller was detected: address 0x" );
         Serial.println( 0x30, HEX );
         Serial.print( "No microcontroller was detected: address 0x" );
         Serial.println( 0x5D, HEX );

         //Prevent the microcontroller did not start to adjust the bright screen
         sendI2CCommand( 250 );  // 250 : Activate touch screen
         pinMode( 1, OUTPUT );
         digitalWrite( 1, LOW );

         delay( 120 );
         pinMode( 1, INPUT );

         delay( 100 );
         }
      }

      // Start sending command 0 to address 0x30
   sendI2CCommand( 20 ); // 0 is the brightest backlight.    / 245 backlight off   (0-245)

   // Initialize display framework through the custom Lovyan class definition
   gfx.init();
   gfx.initDMA();
   gfx.startWrite();
   
   // Initialize Core LVGL Structures
   lv_init();
   size_t bufferSize = sizeof( lv_color_t ) * LCD_H_RES * LCD_V_RES;
   lvColorBuffer1 = ( lv_color_t * )heap_caps_malloc( bufferSize, MALLOC_CAP_SPIRAM );
   lvColorBuffer2 = ( lv_color_t * )heap_caps_malloc( bufferSize, MALLOC_CAP_SPIRAM );

   lv_disp_draw_buf_init( &lvDisplayDrawBuffer, lvColorBuffer1, lvColorBuffer2, LCD_H_RES * LCD_V_RES );

   // Initialize display driver and register it with LVGL
   static lv_disp_drv_t lvDisplayDriver;
   lv_disp_drv_init( &lvDisplayDriver );
   lvDisplayDriver.hor_res = LCD_H_RES;
   lvDisplayDriver.ver_res = LCD_V_RES;
   lvDisplayDriver.flush_cb = displayFlush;
   lvDisplayDriver.draw_buf = &lvDisplayDrawBuffer;
   lv_disp_drv_register( &lvDisplayDriver );

   // Initialize input device driver program
   static lv_indev_drv_t indev_drv;
   lv_indev_drv_init( &indev_drv );
   indev_drv.type = LV_INDEV_TYPE_POINTER;
   indev_drv.read_cb = touchscreenRead;
   lv_indev_drv_register( &indev_drv );

   delay( 100 );
   gfx.fillScreen( TFT_BLACK );

   // Call UI layout logic
   createHelloWorld();

   Serial.println( "Setup done" );
   }

/// @brief 
void loop()
   {
   lv_timer_handler();
   delay( 1 );
   }

/// @brief Initializes the UI
/// @param  
void initializeUI( void )
   {
   lv_disp_t *display = lv_disp_get_default();
   lv_theme_t *theme = lv_theme_default_init( display, lv_palette_main( LV_PALETTE_BLUE ), lv_palette_main( LV_PALETTE_RED ), false, LV_FONT_DEFAULT );

   lv_disp_set_theme( display, theme );
   ui_Screen1_screen_init();

   uiInitialActions = lv_obj_create( NULL );
   lv_disp_load_scr( uiScreen );
   }

extern "C" {
/// @brief Creates the hello world UI elements
/// @param  
void createHelloWorld( void )
   {
   /* Define a clean layout canvas background color */
   lv_obj_set_style_bg_color( lv_scr_act(), lv_color_hex( 0x660066 ), LV_PART_MAIN );

   /* Generate text blocks objects directly inside core layout */
   lv_obj_t * label = lv_label_create( lv_scr_act() ); // Create a label object on the active screen
   lv_label_set_long_mode( label, LV_LABEL_LONG_WRAP ); // Set the label to wrap text if it exceeds the width of the label
   lv_obj_set_width( label, 300 ); // Set the width of the label to 300 pixels
   lv_obj_set_style_text_align( label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN ); // Set the text alignment of the label to center

   lv_label_set_text( label, "Hello World! (Advance v1.4) Stable Display Base" );

   lv_obj_set_style_text_color( label, lv_color_hex( 0xFFFFFF ), LV_PART_MAIN );
   lv_obj_set_style_text_font( label, &lv_font_montserrat_32, LV_PART_MAIN );

   lv_obj_align( label, LV_ALIGN_CENTER, 0, 0 ); // Align the label to the center of the screen
   }
}