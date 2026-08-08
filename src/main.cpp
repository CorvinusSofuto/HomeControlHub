#include "LovyanGFX_Driver.h"
#include "pins_config.h"

#include "WiFi.h"
#include "esp_now.h"
#include <Arduino.h>
#include <AsyncUDP.h>
#include <SPI.h>
#include <Wire.h>
#include <ui.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <stdbool.h>

#include "main.h"

const char *ssid = "MikeG";
const char *password = "rta492000";

// NTP Server and Time Offset settings
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -18000;   // Eastern Standard Time (EST) = -5 hours * 3600
const int daylightOffset_sec = 3600; // 1 hour for Daylight Savings

WiFiUDP udp;
const char *broadcastIP = "255.255.255.255";
const uint16_t broadcastPort = 1234;

// Unified Global Instantiation Wrapper
LGFX gfx;

static lv_disp_draw_buf_t lvDisplayDrawBuffer;
static lv_color_t *lvColorBuffer1 = NULL;
static lv_color_t *lvColorBuffer2 = NULL;

uint16_t touchX, touchY;

SemaphoreHandle_t i2c_mutex = NULL;

struct __attribute__( ( packed ) ) WeatherData
   {
   int sensorID;
   float temp;
   float humidity;
   };

/// @brief Flushes a specific area of the display with the provided color data. This function is called by LVGL when it needs to update a portion of the display. It uses the LovyanGFX library to push the image data to the display using DMA for efficient transfer.
/// @param disp
/// @param area
/// @param color_p
void displayFlush( lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p )
   {
   // Complete any previous refresh operation before starting a new one
   if ( gfx.getStartCount() > 0 )
      {
      gfx.endWrite();
      }

   gfx.pushImageDMA( area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1, (lgfx::rgb565_t *)&color_p->full );

   lv_disp_flush_ready( disp ); // Tell lvgl that the refresh is complete
   }

/// @brief Reads the touch input from the display. This function is called by LVGL when it needs to update the touch input state.
void touchscreenRead( lv_indev_drv_t *indev_driver, lv_indev_data_t *data )
   {
   if ( xSemaphoreTake( i2c_mutex, pdMS_TO_TICKS( 10 ) ) == pdTRUE )
      {
      data->state = LV_INDEV_STATE_REL; // The state of data existence when releasing the finger
      bool touched = gfx.getTouch( &touchX, &touchY );

      if ( touched )
         {
         data->state = LV_INDEV_STATE_PR;

         //  Set coordinates
         data->point.x = touchX;
         data->point.y = touchY;
         }

      xSemaphoreGive( i2c_mutex );
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

/// @brief Sends a command to the I2C device at address 0x30. This function is used to control the backlight of the display, etc.
/// @param command The command byte to send. For example, 0 turns the backlight
void sendI2CCommand( uint8_t command )
   {
   uint8_t error;

   // 1. Attempt standard transmission
   Wire.beginTransmission( 0x30 );
   Wire.write( command );
   error = Wire.endTransmission();

   // 2. If it fails with an invalid state/busy error (error != 0)
   if ( error != 0 )
      {
      Serial.print( "I2C Bus Stuck! Error Code: " );
      Serial.println( error );

      // --- AUTOMATED BUS RECOVERY ROUTINE ---
      Serial.println( "Executing manual SCL clock recovery..." );

      // Completely isolate the Wire hardware module temporarily
      Wire.end();

      // Set SCL (Pin 16) as an output, SDA (Pin 15) as an input
      pinMode( 16, OUTPUT );
      pinMode( 15, INPUT );

      // Pulse SCL 9 times to force the co-microcontroller to release SDA
      for ( int i = 0; i < 9; i++ )
         {
         digitalWrite( 16, HIGH );
         delayMicroseconds( 5 );
         digitalWrite( 16, LOW );
         delayMicroseconds( 5 );
         }

      // Re-initialize the clean I2C hardware peripheral registers
      Wire.begin( 15, 16 );
      delay( 10 );

      // Retry the command immediately so the user doesn't experience lag
      Wire.beginTransmission( 0x30 );
      Wire.write( command );

      if ( Wire.endTransmission() == 0 )
         {
         Serial.println( "Bus recovered! Backlight adjusted on retry." );
         }
      else
         {
         Serial.println( "Critical: Hardware recovery loop failed to clear slave chip." );
         }
      }
   else
      {
      Serial.print( "Command 0x" );
      Serial.print( command, HEX );
      Serial.println( " Sent successfully" );
      }
   }

/// @brief
/// @param esp_now_info
/// @param data
/// @param data_len
void onReceive( const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len )
   {
   char macStr[18];
   snprintf( macStr, sizeof( macStr ), "%02x:%02x:%02x:%02x:%02x:%02x", esp_now_info->src_addr[0], esp_now_info->src_addr[1],
             esp_now_info->src_addr[2], esp_now_info->src_addr[3], esp_now_info->src_addr[4], esp_now_info->src_addr[5] );

   WeatherData *incomingPacket = (WeatherData *)data;

   int sensorID = incomingPacket->sensorID;

   char *temperatureString = (char *)malloc( 10 );
   snprintf( temperatureString, 10, "%.1fF", incomingPacket->temp );

   char *humidityString = (char *)malloc( 10 );
   snprintf( humidityString, 10, "%.1f%%", incomingPacket->humidity );

   if ( sensorID == 101 )
      {
      lv_label_set_text( ui_LabelTemperature, temperatureString );
      lv_label_set_text( ui_LabelHumidity, humidityString );
      }
   else if ( sensorID == 102 )
      {
      lv_label_set_text( ui_LabelTemperature2, temperatureString );
      lv_label_set_text( ui_LabelHumidity2, humidityString );
      }

   free( temperatureString );
   free( humidityString );

   udp.beginPacket( broadcastIP, broadcastPort );
   udp.printf( "{\"SensorID\":%d, \"Temperature\":%f,\"Humidity\":%f}", sensorID, incomingPacket->temp, incomingPacket->humidity );
   udp.endPacket();
   }

// Function to read internal clock and update the SquareLine label
void updateClockLabel()
   {
   struct tm timeinfo;

   if ( !getLocalTime( &timeinfo ) )
      {
      Serial.println( "Failed to obtain time" );
      return;
      }

   // Format the time into a clean string (HH:MM:SS)
   char dateStringBuff[40];
   strftime( dateStringBuff, sizeof( dateStringBuff ), "%A %b %d, %Y", &timeinfo );

   char timeStringBuff[40];
   strftime( timeStringBuff, sizeof( timeStringBuff ), "%I:%M %p", &timeinfo );

   // Make sure you do this only if the screen containing the label is loaded!
   if ( lv_obj_is_valid( ui_LabelDate ) )
      {
      lv_label_set_text( ui_LabelDate, dateStringBuff );
      }

   if ( lv_obj_is_valid( ui_LabelTime ) )
      {
      lv_label_set_text( ui_LabelTime, timeStringBuff );
      }
   }

/// @brief Handles the event for Button1
/// @param e
void EventBLButtonClicked( lv_event_t *e )
   {
   lv_event_code_t event_code = lv_event_get_code( e );
   lv_obj_t *target = lv_event_get_target( e );

   if ( backlightHigh )
      {
      sendI2CCommand( 230 ); // 245 backlight off
      backlightHigh = false;
      lv_label_set_text( ui_LabelBLButtonText, "Dim" );
      }
   else
      {
      sendI2CCommand( 20 ); // 20 backlight on
      backlightHigh = true;
      lv_label_set_text( ui_LabelBLButtonText, "Bright" );
      }
   }

/// @brief Initializes the system, including serial communication, I2C communication, display, and LVGL. It also checks for the presence of specific I2C devices and configures the backlight of the display.
void setup()
   {
   Serial.begin( 115200 );

   WiFi.mode( WIFI_STA );
   WiFi.disconnect();
   WiFi.begin( ssid, password ); // Required in newer core versions to initialize interface

   if ( WiFi.waitForConnectResult() != WL_CONNECTED )
      {
      Serial.println( "WiFi Failed" );

      while ( 1 )
         {
         delay( 1000 );
         }
      }

   // 2. Init and set the internal ESP32 RTC using NTP
   configTime( gmtOffset_sec, daylightOffset_sec, ntpServer );
   Serial.println( "Time synced with NTP server!" );

   WiFi.setSleep( false );

   udp.begin( broadcastPort );
   //udp.beginPacket( broadcastIP, broadcastPort );
   //udp.endPacket();

   i2c_mutex = xSemaphoreCreateMutex();

   // Initialize physical I2C pins for bus controller expansion tasks
   pinMode( 19, OUTPUT );
   Wire.begin( 15, 16 );
   delay( 150 );

   // Coprocessor Wake Routine
   Serial.println( "Scanning for co-processors..." );

   while ( 1 )
      {
      if ( i2cScanForAddress( 0x30 ) && i2cScanForAddress( 0x5D ) )
         {
         Serial.println( "Co-processors detected successfully at 0x30 and 0x5D!" );
         break;
         }
      else
         {
         Serial.println( "Waiting for microcontrollers to respond... retrying pings." );

         // Force an active chip wake-up signal safely over I2C without touching GPIO 1
         sendI2CCommand( 250 ); // 250 : Activate touch screen

         delay( 250 ); // Give the command time to register before scanning again
         }
      }

   sendI2CCommand( 20 ); // 0 is the brightest backlight.    / 245 backlight off   (0-245)

   // Initialize display framework through the custom Lovyan class definition
   gfx.init();
   gfx.initDMA();
   gfx.startWrite();

   // Initialize Core LVGL Structures
   lv_init();
   size_t bufferSize = sizeof( lv_color_t ) * LCD_H_RES * LCD_V_RES;
   lvColorBuffer1 = (lv_color_t *)heap_caps_malloc( bufferSize, MALLOC_CAP_SPIRAM );
   lvColorBuffer2 = (lv_color_t *)heap_caps_malloc( bufferSize, MALLOC_CAP_SPIRAM );

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
   //createHelloWorld();
   //textTimer = lv_timer_create( textTimerHandler, 1000, NULL );
   ui_init();

   if ( esp_now_init() != ESP_OK )
      {
      Serial.println( "Error initializing ESP-NOW" );
      return;
      }

   esp_now_register_recv_cb( onReceive );

   Serial.println( "Setup done" );
   }

/// @brief
void loop()
   {
   lv_tick_inc( 1 );
   lv_timer_handler();

   static uint32_t lastTick = 0;

   if ( millis() - lastTick > 1000 )
      {
      lastTick = millis();
      updateClockLabel();
      }

   delay( 1 );
   }
