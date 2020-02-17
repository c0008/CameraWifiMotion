 /*******************************************************************************************************************
 *
 *             ESP32Camera with motion detection and web server -  using Arduino IDE 
 *             
 *             Serves a web page whilst detecting motion on a camera (uses ESP32Cam module)
 *             
 *             Included files: gmail-esp32.h, standard.h and wifi.h, motion.h, ota.h
 *             Bult using Arduino IDE 1.8.10, esp32 boards v1.0.4
 *             
 *             Note: The flash can not be used if using an SD Card as they both use pin 4
 *             
 *             GPIO16 is used as an input pin for external sensors etc. (not implemented yet)
 *             
 *             IMPORTANT! - If you are getting weird problems (motion detection retriggering all the time, slow wifi
 *                          response times especially when using the LED), chances are there is a problem with the 
 *                          power to the board.  It needs a good 500ma supply and ideally a good sized smoothing 
 *                          capacitor.
 *             
 *      First time the ESP starts it will create an access point "ESPConfig" which you need to connect to in order to enter your wifi details.  
 *             default password = "12345678"   (note-it may not work if anything other than 8 characters long for some reason?)
 *             see: https://randomnerdtutorials.com/wifimanager-with-esp8266-autoconnect-custom-parameter-and-manage-your-ssid-and-password
 *
 *      Motion detection based on: https://eloquentarduino.github.io/2020/01/motion-detection-with-esp32-cam-only-arduino-version/
 *      
 *      camera troubleshooting: https://randomnerdtutorials.com/esp32-cam-troubleshooting-guide/
 *                  
 *                                                                                              www.alanesq.eu5.net
 *      
 ********************************************************************************************************************/



// ---------------------------------------------------------------
//                          -SETTINGS
// ---------------------------------------------------------------

  #define ENABLE_OTA 1                                   // Enable Over The Air updates (OTA)

  const String stitle = "CameraWifiMotion";              // title of this sketch

  const String sversion = "16Feb20";                     // version of this sketch

  const char* MDNStitle = "ESPcam1";                     // Mdns title (use 'http://<MDNStitle>.local' )

  int MaxSpiffsImages = 10;                              // number of images to store in camera (Spiffs)
  
  const uint16_t datarefresh = 5000;                     // Refresh rate of the updating data on web page (1000 = 1 second)

  String JavaRefreshTime = "500";                        // time delay when loading url in web pages (Javascript) to prevent failed requests
    
  const uint16_t LogNumber = 50;                         // number of entries to store in the system log

  const uint16_t ServerPort = 80;                        // ip port to serve web pages on

  const uint16_t Illumination_led = 4;                   // illumination LED pin

  const byte gioPin = 16;                                // I/O pin (for external sensor input) - not yet implemented
  
  const uint16_t MaintCheckRate = 15;                    // how often to do the routine system checks (seconds)
  

// ---------------------------------------------------------------


  uint32_t TRIGGERtimer = 0;                 // used for limiting camera motion trigger rate
  uint32_t EMAILtimer = 0;                   // used for limiting rate emails can be sent
  byte DetectionEnabled = 1;                 // flag if capturing motion is enabled (0=stopped, 1=enabled, 2=paused)
  String TriggerTime = "Not yet triggered";  // Time of last motion trigger
  uint32_t MaintTiming = millis();           // used for timing maintenance tasks
  bool emailWhenTriggered = 0;               // flag if to send emails when motion detection triggers
  bool ReqLEDStatus = 0;                     // desired status of the illuminator led (i.e. should it be on or off when not being used as a flash)
  const bool ledON = HIGH;                   // Status LED control 
  const bool ledOFF = LOW;
  uint16_t TriggerLimitTime = 2;             // min time between motion detection triggers (seconds)
  uint16_t EmailLimitTime = 60;              // min time between email sends (seconds)
  bool UseFlash = 1;                         // use flash when taking a picture

#include "soc/soc.h"                         // Disable brownout problems
#include "soc/rtc_cntl_reg.h"                // Disable brownout problems

// spiffs used to store images and settings
  #include <SPIFFS.h>
  #include <FS.h>                            // gives file access on spiffs
  int SpiffsFileCounter = 0;                 // counter of last image stored

// sd card - see https://randomnerdtutorials.com/esp32-cam-take-photo-save-microsd-card/
  #include "SD_MMC.h"
  // #include <SPI.h>                        // (already loaded)
  // #include <FS.h>                         // gives file access (already loaded)
  #define SD_CS 5                            // sd chip select pin
  bool SD_Present;                           // flag if an sd card was found (0 = no)

#include "wifi.h"                            // Load the Wifi / NTP stuff

#include "standard.h"                        // Standard procedures

#include "gmail_esp32.h"                     // send email via smtp

#include "motion.h"                          // motion detection / camera

#if ENABLE_OTA
  #include "ota.h"                           // Over The Air updates (OTA)
#endif

  
// ---------------------------------------------------------------
//    -SETUP     SETUP     SETUP     SETUP     SETUP     SETUP
// ---------------------------------------------------------------

void setup(void) {
    
  Serial.begin(115200);
  // Serial.setTimeout(2000);
  // while(!Serial) { }        // Wait for serial to initialize.

  Serial.println(F("\n\n\n---------------------------------------"));
  Serial.println("Starting - " + stitle + " - " + sversion);
  Serial.println(F("---------------------------------------"));
  
  // Serial.setDebugOutput(true);            // enable extra diagnostic info  


  // Spiffs - see: https://circuits4you.com/2018/01/31/example-of-esp8266-flash-file-system-spiffs/
    if (!SPIFFS.begin(true)) {
      Serial.println(F("An Error has occurred while mounting SPIFFS"));
      delay(5000);
      ESP.restart();
      delay(5000);
    } else {
      Serial.print(F("SPIFFS mounted successfully."));
      Serial.print("total bytes: " + String(SPIFFS.totalBytes()));
      Serial.println(", used bytes: " + String(SPIFFS.usedBytes()));
      LoadSettingsSpiffs();     // Load settings from text file in Spiffs
    }

  // start sd card
      SD_Present = 0;
      pinMode(Illumination_led, INPUT);            // disable led pin as sdcard uses it
      if(!SD_MMC.begin()){                         // if loading sd card fails     ("/sdcard", true = 1 wire?)
          log_system_message("SD Card not found");   
          pinMode(Illumination_led, OUTPUT);       // re-enable led pin
      } else {
        uint8_t cardType = SD_MMC.cardType();
        if(cardType == CARD_NONE){                 // if no sd card found
            log_system_message("SD Card type detect failed"); 
            pinMode(Illumination_led, OUTPUT);       // re-enable led pin
        } else {
            log_system_message("SD Card found"); 
            SD_Present = 1;                        // flag working sd card found
        }
      }
      
  // configure the LED
    if (!SD_Present) {
      pinMode(Illumination_led, OUTPUT); 
      digitalWrite(Illumination_led, ledOFF); 
    }
    
  if (!SD_Present) BlinkLed(1);           // flash the led once

  // configure the I/O pin (with pullup resistor)
    pinMode(gioPin,  INPUT_PULLUP);                                

  startWifiManager();                        // Connect to wifi (procedure is in wifi.h)
  
  if (MDNS.begin(MDNStitle)) {
    Serial.println(F("MDNS responder started"));
  }
  
  WiFi.mode(WIFI_STA);     // turn off access point - options are WIFI_AP, WIFI_STA, WIFI_AP_STA or WIFI_OFF

  // set up web page request handling
    server.on("/", handleRoot);              // root page
    server.on("/data", handleData);          // This displays information which updates every few seconds (used by root web page)
    server.on("/ping", handlePing);          // ping requested
    server.on("/log", handleLogpage);        // system log
    server.on("/test", handleTest);          // testing page
    server.on("/reboot", handleReboot);      // reboot the esp
    server.on("/default", handleDefault);    // All settings to defaults
    server.on("/live", handleLive);          // capture and display live image
    server.on("/images", handleImages);      // display images
    server.on("/img", handleImg);            // latest captured image
    server.on("/bootlog", handleBootLog);    // Display boot log from Spiffs
    server.on("/imagedata", handleImagedata);// Show raw image data
    server.onNotFound(handleNotFound);       // invalid page requested

  #if ENABLE_OTA
    otaSetup();    // Over The Air updates (OTA)
  #endif

  // start web server
    Serial.println(F("Starting web server"));
    server.begin();

  // Finished connecting to network
    BlinkLed(2);                             // flash the led twice
    log_system_message(stitle + " Started");   
       
  // set up camera
    Serial.print(F("Initialising camera: "));
    Serial.println(setup_camera() ? "OK" : "ERR INIT");

  // Turn-off the 'brownout detector'
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  if (!SD_Present) BlinkLed(2);                    // flash the led twice

  TRIGGERtimer = millis();                         // reset retrigger timer to stop instant motion trigger

  if (!psramFound()) log_system_message("Warning: No PSRam found");
  
  UpdateBootlogSpiffs("Booted");                   // store time of boot in bootlog

}


// blink the led 
void BlinkLed(byte Bcount) {
  if (!SD_Present) {
    for (int i = 0; i < Bcount; i++) { 
      digitalWrite(Illumination_led, ledON);
      delay(50);
      digitalWrite(Illumination_led, ledOFF);
      delay(300);
    }
  }
}


// ----------------------------------------------------------------
//   -LOOP     LOOP     LOOP     LOOP     LOOP     LOOP     LOOP
// ----------------------------------------------------------------

void loop(void){

  server.handleClient();                           // service any web requests 

  // camera motion detection 
  if (DetectionEnabled == 1) {    
    if (!capture_still()) RebootCamera(PIXFORMAT_GRAYSCALE);                            // capture image, if problem reboot camera and try again
    uint16_t changes = motion_detect();                                                 // find amount of change in current image frame compared to the last one     
    update_frame();                                                                     // Copy current frame to previous
    // which thresholds to use
      uint16_t tLow = dayImage_thresholdL;
      uint16_t tHigh = dayImage_thresholdH;
      if (dayNightBrightness > 0 && AveragePix < dayNightBrightness) {                  // switch to nighttime settings
        tLow = nightImage_thresholdL;
        tHigh = nightImage_thresholdH;        
      }
    if ( (changes >= tLow) && (changes <= tHigh) ) {                                    // if motion detected 
         if ((unsigned long)(millis() - TRIGGERtimer) >= (TriggerLimitTime * 1000) ) {  // limit time between triggers
            TRIGGERtimer = millis();                                                    // update last trigger time
            MotionDetected(changes);                                                    // run motion detected procedure
         } else Serial.println(F("Too soon to re-trigger"));
     }
  } 

  // periodically run some checks
    if ((unsigned long)(millis() - MaintTiming) >= (MaintCheckRate * 1000) ) {   
      WIFIcheck();                                 // check if wifi connection is ok
      MaintTiming = millis();                      // reset timer
      time_t t=now();                              // read current time to ensure NTP auto refresh keeps triggering (otherwise only triggers when time is required causing a delay in response)
      // check status of illumination led
        if (ReqLEDStatus && !SD_Present) digitalWrite(Illumination_led, ledON); 
        else if (!SD_Present) digitalWrite(Illumination_led, ledOFF);
    }
    
  delay(50);
} 
       

// ----------------------------------------------------------------
//              Load settings from text file in Spiffs
// ----------------------------------------------------------------

void LoadSettingsSpiffs() {
    String TFileName = "/settings.txt";
    if (!SPIFFS.exists(TFileName)) {
      log_system_message("Settings file not found on Spiffs");
      return;
    }
    File file = SPIFFS.open(TFileName, "r");
    if (!file) {
      log_system_message("Unable to open settings file from Spiffs");
      return;
    } 

    log_system_message("Loading settings from Spiffs");
    
    // read contents of file
      String line;
      uint16_t tnum;
  

    // line 1 - dayBlock_threshold
      ReadLineSpiffs(&file, &line, &tnum);
      if (tnum < 1 || tnum > 255) log_system_message("invalid dayBlock_threshold in settings");
      else dayBlock_threshold = tnum;
      
    // line 2 - min dayImage_thresholdL
      ReadLineSpiffs(&file, &line, &tnum);
      if (tnum < 0 || tnum > 255) log_system_message("invalid min_day_image_threshold in settings");
      else dayImage_thresholdL = tnum;

    // line 3 - min dayImage_thresholdH
      ReadLineSpiffs(&file, &line, &tnum);
      if (tnum < 0 || tnum > 255) log_system_message("invalid max_day_image_threshold in settings");
      else dayImage_thresholdH = tnum;
            
    // line 4 - nightBlock_threshold
      ReadLineSpiffs(&file, &line, &tnum);
      if (tnum < 1 || tnum > 255) log_system_message("invalid nightBlock_threshold in settings");
      else nightBlock_threshold = tnum;
      
    // line 5 - min nightImage_thresholdL
      ReadLineSpiffs(&file, &line, &tnum);
      if (tnum < 0 || tnum > 255) log_system_message("invalid night_day_image_threshold in settings");
      else nightImage_thresholdL = tnum;

    // line 6 - min nightImage_thresholdH
      ReadLineSpiffs(&file, &line, &tnum);
      if (tnum < 0 || tnum > 255) log_system_message("invalid night_day_image_threshold in settings");
      else nightImage_thresholdH = tnum;

    // line 7 - day/night brightness cuttoff point
      ReadLineSpiffs(&file, &line, &tnum);
      if (tnum < 0 || tnum > 255) log_system_message("invalid night/night brightness cuttoff in settings");
      else dayNightBrightness = tnum;

    // line 8 - emailWhenTriggered
      ReadLineSpiffs(&file, &line, &tnum);
      if (tnum == 0) emailWhenTriggered = 0;
      else if (tnum == 1) emailWhenTriggered = 1;
      else log_system_message("Invalid emailWhenTriggered in settings: " + line);
      
    // line 9 - TriggerLimitTime
      ReadLineSpiffs(&file, &line, &tnum);
      if (tnum < 1 || tnum > 3600) log_system_message("invalid TriggerLimitTime in settings");
      else TriggerLimitTime = tnum;

    // line 10 - DetectionEnabled
      ReadLineSpiffs(&file, &line, &tnum);
      if (tnum == 2) tnum = 1;     // if it was paused restart it
      if (tnum == 0) DetectionEnabled = 0;
      else if (tnum == 1) DetectionEnabled = 1;
      else log_system_message("Invalid DetectionEnabled in settings: " + line);

    // line 11 - EmailLimitTime
      ReadLineSpiffs(&file, &line, &tnum);
      if (tnum < 60 || tnum > 10000) log_system_message("invalid EmailLimitTime in settings");
      else EmailLimitTime = tnum;

    // line 12 - UseFlash
      ReadLineSpiffs(&file, &line, &tnum);
      if (tnum == 0) UseFlash = 0;
      else if (tnum == 1) UseFlash = 1;
      else log_system_message("Invalid UseFlash in settings: " + line);
      
    // line 13 - SpiffsFileCounter
      ReadLineSpiffs(&file, &line, &tnum);
      if (tnum > MaxSpiffsImages) log_system_message("invalid SpiffsFileCounter in settings");
      else SpiffsFileCounter = tnum;

    // Detection mask grid
      bool gerr = 0;
      mask_active = 0;
      for (int y = 0; y < mask_rows; y++) {
        for (int x = 0; x < mask_columns; x++) {
          ReadLineSpiffs(&file, &line, &tnum);
          if (tnum == 1) {
            mask_frame[x][y] = 1;
            mask_active ++;  
          }
          else if (tnum == 0) mask_frame[x][y] = 0;
          else gerr = 1;    // flag invalid entry
        }
      }
      if (gerr) log_system_message("invalid mask entry in settings");

    file.close();
}


// read a line of text from Spiffs file and parse an integer from it
//     I realise this is complicating things for no real benefit but I wanted to learn to use pointers ;-)
void ReadLineSpiffs(File* file, String* line, uint16_t* tnum) {
      File tfile = *file;
      String tline = *line;
      tline = tfile.readStringUntil('\n');
      *tnum = tline.toInt();
}


// ----------------------------------------------------------------
//              Save settings to text file in Spiffs
// ----------------------------------------------------------------

void SaveSettingsSpiffs() {
    String TFileName = "/settings.txt";
    SPIFFS.remove(TFileName);   // delete old file if present
    File file = SPIFFS.open(TFileName, "w");
    if (!file) {
      log_system_message("Unable to open settings file in Spiffs");
      return;
    } 

    // save settings in to file
        file.println(String(dayBlock_threshold));
        file.println(String(dayImage_thresholdL));
        file.println(String(dayImage_thresholdH));      
        file.println(String(nightBlock_threshold));
        file.println(String(nightImage_thresholdL));
        file.println(String(nightImage_thresholdH));
        file.println(String(dayNightBrightness));
        file.println(String(emailWhenTriggered));
        file.println(String(TriggerLimitTime));
        file.println(String(DetectionEnabled));
        file.println(String(EmailLimitTime));
        file.println(String(UseFlash));
        file.println(String(SpiffsFileCounter));

       // Detection mask grid
          for (int y = 0; y < mask_rows; y++) {
            for (int x = 0; x < mask_columns; x++) {
              file.println(String(mask_frame[x][y]));
            }
          }

    file.close();
}


// ----------------------------------------------------------------
//                     Update boot log - Spiffs
// ----------------------------------------------------------------

void UpdateBootlogSpiffs(String Info) {
  
    Serial.println("Updating bootlog: " + Info);
    String TFileName = "/bootlog.txt";
    File file = SPIFFS.open(TFileName, FILE_APPEND);
    if (!file) {
      log_system_message("Error: Unable to open boot log in Spiffs");
    } else {
      file.println(currentTime() + " - " + Info);       // add entry to log file   
      file.close();
    }
}


// ----------------------------------------------------------------
//                reset back to default settings
// ----------------------------------------------------------------

void handleDefault() {

    // default settings
      emailWhenTriggered = 0;
      dayNightBrightness = 0;
      dayBlock_threshold = 18;
      dayImage_thresholdL= 10;
      dayImage_thresholdH= 192;
      nightBlock_threshold = 5;
      nightImage_thresholdL= 6;
      nightImage_thresholdH= 192;
      TriggerLimitTime = 10;
      EmailLimitTime = 600;
      DetectionEnabled = 1;
      UseFlash = 1;

      // Detection mask grid
        for (int y = 0; y < mask_rows; y++) 
          for (int x = 0; x < mask_columns; x++) 
            mask_frame[x][y] = 1;
        mask_active = 12;

    SaveSettingsSpiffs();                      // save settings in Spiffs
    TRIGGERtimer = millis();                   // reset last image captured timer (to prevent instant trigger)

    log_system_message("Defauls web page request");      
    String message = "reset to default";

    server.send(404, "text/plain", message);   // send reply as plain text
    message = "";      // clear string
      
}

// ----------------------------------------------------------------
//       -root web page requested    i.e. http://x.x.x.x/
// ----------------------------------------------------------------

void handleRoot() {

  log_system_message("root webpage requested");     

  
  // Action any buttons presses etc.
     
    // email was clicked -  if an email is sent when triggered 
      if (server.hasArg("email")) {
        if (!emailWhenTriggered) {
              log_system_message("Email when motion detected enabled");
              EMAILtimer = 0;
              emailWhenTriggered = 1;
        } else {
          log_system_message("Email when motion detected disabled"); 
          emailWhenTriggered = 0;
        }
        SaveSettingsSpiffs();     // save settings in Spiffs
      }
      
   // if wipeS was entered  - clear Spiffs
      if (server.hasArg("wipeS")) WipeSpiffs();        // format Spiffs 

//    // if wipeSD was entered  - clear all stored images on SD Card
//      if (server.hasArg("wipeSD")) {
//        log_system_message("Clearing all stored images (SD Card)"); 
//        fs::FS &fs = SD_MMC;
//        fs.format();     // not a valid command
//      }

    // if daynight was entered - dayNightBrightness
      if (server.hasArg("daynight")) {
        String Tvalue = server.arg("daynight");   // read value
        int val = Tvalue.toInt();
        if (val >= 0 && val < 256 && val != dayNightBrightness) { 
          log_system_message("dayNight cuttoff changed to " + Tvalue ); 
          dayNightBrightness = val;
          SaveSettingsSpiffs();     // save settings in Spiffs
        }
      }
      
      // if dblockt was entered - dayBlock_threshold
      if (server.hasArg("dblockt")) {
        String Tvalue = server.arg("dblockt");   // read value
        int val = Tvalue.toInt();
        if (val > 0 && val < 256 && val != dayBlock_threshold) { 
          log_system_message("dayBlock_threshold changed to " + Tvalue ); 
          dayBlock_threshold = val;
          SaveSettingsSpiffs();     // save settings in Spiffs
        }
      }
      
    // if dimagetl was entered - min-image_threshold
      if (server.hasArg("dimagetl")) {
        String Tvalue = server.arg("dimagetl");   // read value
        int val = Tvalue.toInt();
        if (val >= 0 && val < 192 && val != dayImage_thresholdL) { 
          log_system_message("Min_day_image_threshold changed to " + Tvalue ); 
          dayImage_thresholdL = val;
          SaveSettingsSpiffs();     // save settings in Spiffs
        }
      }

    // if dimageth was entered - min-image_threshold
      if (server.hasArg("dimageth")) {
        String Tvalue = server.arg("dimageth");   // read value
        int val = Tvalue.toInt();
        if (val > 0 && val <= 192 && val != dayImage_thresholdH) { 
          log_system_message("Max_day_image_threshold changed to " + Tvalue ); 
          dayImage_thresholdH = val;
          SaveSettingsSpiffs();     // save settings in Spiffs
        }
      }
      
    // if nlockt was entered - dayBlock_threshold
      if (server.hasArg("nblockt")) {
        String Tvalue = server.arg("nblockt");   // read value
        int val = Tvalue.toInt();
        if (val > 0 && val < 256 && val != nightBlock_threshold) { 
          log_system_message("nightBlock_threshold changed to " + Tvalue ); 
          nightBlock_threshold = val;
          SaveSettingsSpiffs();     // save settings in Spiffs
        }
      }
      
    // if nimagetl was entered - min-image_threshold
      if (server.hasArg("nimagetl")) {
        String Tvalue = server.arg("nimagetl");   // read value
        int val = Tvalue.toInt();
        if (val >= 0 && val < 192 && val != nightImage_thresholdL) { 
          log_system_message("Min_night_image_threshold changed to " + Tvalue ); 
          nightImage_thresholdL = val;
          SaveSettingsSpiffs();     // save settings in Spiffs
        }
      }

    // if nimageth was entered - min-image_threshold
      if (server.hasArg("nimageth")) {
        String Tvalue = server.arg("nimageth");   // read value
        int val = Tvalue.toInt();
        if (val > 0 && val <= 192 && val != nightImage_thresholdH) { 
          log_system_message("Max_night_image_threshold changed to " + Tvalue ); 
          nightImage_thresholdH = val;
          SaveSettingsSpiffs();     // save settings in Spiffs
        }
      }

    // Mask grid check array
      if (server.hasArg("submit")) {                           // if submit button was pressed
        mask_active = 0;  
        bool maskChanged = 0;                                  // flag if the mask has changed
        for (int y = 0; y < mask_rows; y++) {
          for (int x = 0; x < mask_columns; x++) {
            if (server.hasArg(String(x) + String(y))) { 
              // set to active
              if (mask_frame[x][y] == 0) maskChanged = 1;      
              mask_frame[x][y] = 1;
              mask_active ++;
            } else {
              // set to disabled
              if (mask_frame[x][y] == 1) maskChanged = 1;    
              mask_frame[x][y] = 0;
            }
          }
        }
        if (maskChanged) {
          dayImage_thresholdH = mask_active * blocksPerMaskUnit;    // reset max trigger setting to max possible
          SaveSettingsSpiffs();                                  // save settings in Spiffs
          log_system_message("Detection mask updated"); 
        }
      }
      
    // if emailtime was entered - min time between email sends
      if (server.hasArg("emailtime")) {
        String Tvalue = server.arg("emailtime");   // read value
        int val = Tvalue.toInt();
        if (val > 59 && val < 10000 && val != EmailLimitTime) { 
          log_system_message("EmailLimitTime changed to " + Tvalue + " seconds"); 
          EmailLimitTime = val;
          SaveSettingsSpiffs();     // save settings in Spiffs
        }
      }
      
      // if triggertime was entered - min time between triggers
      if (server.hasArg("triggertime")) {
        String Tvalue = server.arg("triggertime");   // read value
        int val = Tvalue.toInt();
        if (val > 0 && val < 3600 && val != TriggerLimitTime) { 
          log_system_message("Triggertime changed to " + Tvalue + " seconds"); 
          TriggerLimitTime = val;
          SaveSettingsSpiffs();     // save settings in Spiffs
        }
      }
  
    // if button "toggle illuminator LED" was pressed  
      if (server.hasArg("illuminator")) {
        // button was pressed 
          if (DetectionEnabled == 1) DetectionEnabled = 2;    // pause motion detecting (to stop light triggering it)
          if (!ReqLEDStatus) {
            ReqLEDStatus = 1;
            digitalWrite(Illumination_led, ledON);  
            log_system_message("Illuminator LED turned on");    
          } else {
            ReqLEDStatus = 0;
            digitalWrite(Illumination_led, ledOFF);  
            log_system_message("Illuminator LED turned off"); 
          }
          TRIGGERtimer = millis();                                // reset last image captured timer (to prevent instant trigger)
          if (DetectionEnabled == 2) DetectionEnabled = 1;        // re enable detection if it was paused
      }
      
    // if button "flash" was pressed  - toggle flash enabled
      if (server.hasArg("flash")) {
        // button was pressed 
        if (UseFlash == 0) {
            UseFlash = 1;
            log_system_message("Flash enabled");    
          } else {
            UseFlash = 0;
            log_system_message("Flash disabled");    
          }
          SaveSettingsSpiffs();     // save settings in Spiffs
      }
      
    // if button "toggle motion detection" was pressed  
      if (server.hasArg("detection")) {
        // button was pressed 
          if (DetectionEnabled == 0) {
            TRIGGERtimer = millis();                                // reset last image captured timer (to prevent instant 
            DetectionEnabled = 1;
            log_system_message("Motion detection enabled"); 
            TriggerTime = "Not since detection enabled";
          } else {
            DetectionEnabled = 0;
            log_system_message("Motion detection disabled");  
            AveragePix = 0;                                         // clear average brightness reading as frames no longer being captured
          }
          SaveSettingsSpiffs();                                     // save settings in Spiffs
      }

  latestChanges = 0;                                                // reset stored motion values as could be out of date 

  // build the HTML code 
  
    String message = webheader();                                   // add the standard html header
    message += "<FORM action='/' method='post'>\n";                 // used by the buttons (action = the page send it to)
    message += "<P>";                                               // start of section
    

    // insert an iframe containing the changing data (updates every few seconds using java script)
       message += "<BR><iframe id='dataframe' height=160; width=600; frameborder='0';></iframe>\n"
      "<script type='text/javascript'>\n"
         "setTimeout(function() {document.getElementById('dataframe').src='/data';}, " + JavaRefreshTime +");\n"
         "window.setInterval(function() {document.getElementById('dataframe').src='/data';}, " + String(datarefresh) + ");\n"
      "</script>\n"; 

    // detection mask check grid (right of screen)
      message += "<div style='float: right;'>Detection Mask<br>";
      for (int y = 0; y < mask_rows; y++) {
        for (int x = 0; x < mask_columns; x++) {
          message += "<input type='checkbox' name='" + String(x) + String(y) + "' ";
          if (mask_frame[x][y]) message += "checked ";
          message += ">\n";
        }
        message += "<BR>";
      }
      message += "<BR>" + String(mask_active) + " active";
      message += "<BR>(" + String(mask_active * blocksPerMaskUnit) + " blocks)";
      message += "</div>\n";
      
    // minimum seconds between triggers
      message += "<BR>Minimum time between triggers:";
      message += "<input type='number' style='width: 60px' name='triggertime' min='1' max='3600' value='" + String(TriggerLimitTime) + "'>seconds \n";

    // minimum seconds between email sends
      message += "<BR>Minimum time between E-mails:";
      message += "<input type='number' style='width: 60px' name='emailtime' min='60' max='10000' value='" + String(EmailLimitTime) + "'>seconds \n";

    // Day / night brightness cuttoff point
      message += "<BR><BR>Day/Night brightness switch point: "; 
      message += "<input type='number' style='width: 40px' name='daynight' title='Brightness level below which night mode is enabled' min='0' max='255' value='" + String(dayNightBrightness) + "'>";
      message += " (0 = day/night have same settings)<BR>\n";

    // detection parameters (day)
      if (dayImage_thresholdH > (mask_active * blocksPerMaskUnit)) dayImage_thresholdH = (mask_active * blocksPerMaskUnit);    // make sure high threshold is not greater than max possible
      if (dayNightBrightness != 0) message += "<BR>Day: ";
      message += "Detection threshold: <input type='number' style='width: 40px' name='dblockt' title='Brightness variation in block required to count as changed (0-255)' min='1' max='255' value='" + String(dayBlock_threshold) + "'>, \n";
      message += "Trigger when between<input type='number' style='width: 40px' name='dimagetl' title='Minimum changed blocks in image required to count as motion detected' min='0' max='" + String(mask_active * blocksPerMaskUnit) + "' value='" + String(dayImage_thresholdL) + "'> \n"; 
      message += " and <input type='number' style='width: 40px' name='dimageth' title='Maximum changed blocks in image required to count as motion detected' min='1' max='" + String(mask_active * blocksPerMaskUnit) + "' value='" + String(dayImage_thresholdH) + "'> blocks changed";
      message += " out of " + String(mask_active * blocksPerMaskUnit); 
               
    // detection parameters (night)
      if (dayNightBrightness != 0) {
        if (nightImage_thresholdH > (mask_active * blocksPerMaskUnit)) nightImage_thresholdH = (mask_active * blocksPerMaskUnit);    // make sure high threshold is not greater than max possible
        message += "<BR>Night: Detection threshold: <input type='number' style='width: 40px' name='nblockt' title='Brightness variation in block required to count as changed (0-255)' min='1' max='255' value='" + String(nightBlock_threshold) + "'>, \n";
        message += "Trigger when between<input type='number' style='width: 40px' name='nimagetl' title='Minimum changed blocks in image required to count as motion detected' min='0' max='" + String(mask_active * blocksPerMaskUnit) + "' value='" + String(nightImage_thresholdL) + "'> \n"; 
        message += " and <input type='number' style='width: 40px' name='nimageth' title='Maximum changed blocks in image required to count as motion detected' min='1' max='" + String(mask_active * blocksPerMaskUnit) + "' value='" + String(nightImage_thresholdH) + "'> blocks changed";
        message += " out of " + String(mask_active * blocksPerMaskUnit); 
      }
      
    // input submit button  
      message += "<BR><BR><input type='submit' name='submit'><BR><BR>\n";

    // Toggle illuminator LED button
      if (!SD_Present) message += "<input style='height: 30px;' name='illuminator' title='Toggle the Illumination LED On/Off' value='Light' type='submit'> \n";

    // Toggle 'use flash' button
      if (!SD_Present) message += "<input style='height: 30px;' name='flash' title='Toggle use of flash when capturing image On/Off' value='Flash' type='submit'> \n";

    // Toggle motion detection
      message += "<input style='height: 30px;' name='detection' title='Motion detection enable/disable' value='Detection' type='submit'> \n";

    // Toggle email when motion detection
      message += "<input style='height: 30px;' name='email' value='Email' title='Send email when motion detected enable/disable' type='submit'> \n";

    // Clear images in spiffs
      message += "<input style='height: 30px;' name='wipeS' value='Wipe Store' title='Delete all images stored in Spiffs' type='submit'> \n";
    
//    // Clear images on SD Card
//      if (SD_Present) message += "<input style='height: 30px;' name='wipeSD' value='Wipe SDCard' title='Delete all images on SD Card' type='submit'> \n";

    message += "</span></P>\n";    // end of section    
    message += webfooter();        // add the standard footer

    server.send(200, "text/html", message);      // send the web page
    message = "";      // clear string

}

  
// ----------------------------------------------------------------
//     -data web page requested     i.e. http://x.x.x.x/data
// ----------------------------------------------------------------
//
//   This shows information on the root web page which refreshes every few seconds

void handleData(){

  String message = 
      "<!DOCTYPE HTML>\n"
      "<html><body>\n";
   
          
  // Motion detection
    message += "<BR>Motion detection is ";
    if (DetectionEnabled == 1) message +=  "enabled, last triggered: " + TriggerTime + "\n";
    else if (DetectionEnabled == 2) message += red + "disabled" + endcolour + "\n";
    else message += red + "disabled" + endcolour + "\n";

  // display adnl info if detection is enabled
    if (DetectionEnabled == 1) {
        message += "<BR>Readings: Brightness:" + String(AveragePix);
        // day/night mode
          if (dayNightBrightness > 0) {
            if (AveragePix < dayNightBrightness) message += " (Night)";
            else message += " (Day)";
          }
        message += ", " + String(latestChanges) + " changed blocks out of " + String(mask_active * blocksPerMaskUnit);
        latestChanges = 0;                                                       // reset stored values once displayed
    }
    
  // email when motion detected
    message += "<BR>Send an Email when motion detected: "; 
    message += emailWhenTriggered ? red + "enabled" + endcolour : "disabled";
    
  // Illumination
    message += "<BR>Illumination LED is ";    
    if (digitalRead(Illumination_led) == ledON) message += red + "On" + endcolour;
    else message += "Off";
    if (!SD_Present) {    // if no sd card in use show status of use flash 
      message += UseFlash ? " - Flash enabled\n" :  " - Flash disabled\n";
    }

  // show current time
    message += "<BR>Time: " + currentTime() + "\n";      // show current time

  // show if a sd card is present
    if (SD_Present) message += "<BR>SD-Card present (Flash disabled)\n";

//  // show io pin status
//    message += "<BR>External sensor pin is: ";
//    if (digitalRead(gioPin)) message += "High\n";
//    else message += "Low\n";

  message += "</body></htlm>\n";
  
  server.send(200, "text/html", message);   // send reply as plain text
  message = "";      // clear string

  
}


// ----------------------------------------------------------------
//           -Display live image     i.e. http://x.x.x.x/live
// ----------------------------------------------------------------

void handleLive(){

  log_system_message("Live image requested");      


  String message = webheader();                                      // add the standard html header

  message += "<BR><H1>Live Image - " + currentTime() +"</H1><BR>\n";

  capturePhotoSaveSpiffs(UseFlash);     // capture an image from camera

  // insert image in to html
    message += "<img  id='img' alt='Live Image' width='90%'>\n";       // content is set in javascript

  // javascript to refresh the image after short delay (bug fix as it often rejects the first request)
    message +=  "<script type='text/javascript'>\n"
                "  setTimeout(function(){ document.getElementById('img').src='/img'; }, " + JavaRefreshTime + ");\n"
                "</script>\n";
    
  message += webfooter();                                             // add the standard footer
  
  server.send(200, "text/html", message);      // send the web page
  message = "";      // clear string  
}


// ----------------------------------------------------------------
//    -Display captured images     i.e. http://x.x.x.x/images
// ----------------------------------------------------------------
// Image width in percent can be specified in URL with http://x.x.x.x/images?width=90

void handleImages(){

  log_system_message("Stored images page requested");   
  uint16_t ImageToShow = SpiffsFileCounter;     // set current image to display when /img called
  String ImageWidthSetting = "90";              // percentage of screen width to use for displaying the image

  // action any buttons presses or url parameters

    // if a image select button was pressed
      if (server.hasArg("button")) {
        String Bvalue = server.arg("button");   // read value
        int val = Bvalue.toInt();
        Serial.println("Button " + Bvalue + " was pressed");
        ImageToShow = val;     // select which image to display when /img called
      }
      
    // if a image width is specified in the URL    (i.e.  '...?width=')
      if (server.hasArg("width")) {
        String Bvalue = server.arg("width");   // read value
        uint16_t val = Bvalue.toInt();
        if (val >= 10 && val <= 100) ImageWidthSetting = String(val);
        else log_system_message("Error: Invalid image width specified in URL: " + Bvalue);
      }
      
  String message = webheader();                                      // add the standard html header
  message += "<FORM action='/images' method='post'>\n";               // used by the buttons (action = the page send it to)

  message += "<H1>Stored Images</H1>\n";
  
  // create image selection buttons
    for(int i=1; i <= MaxSpiffsImages; i++) {
        message += "<input style='height: 30px; ";
        if (i == ImageToShow) message += "background-color: #0f8;";
        message += "' name='button' value='" + String(i) + "' type='submit'>\n";
    }

  // Insert time info. from text file
    String TFileName = "/" + String(ImageToShow) + ".txt";
    File file = SPIFFS.open(TFileName, "r");
    if (!file) message += red + "<BR>File not found" + endcolour + "\n";
    else {
      String line = file.readStringUntil('\n');      // read first line of text file
      message += "<BR>" + line +"\n";
    }
    file.close();
    
  // insert image in to html
    message += "<BR><img id='img' alt='Camera Image' width='" + ImageWidthSetting + "%'>\n";      // content is set in javascript

  // javascript to refresh the image after short delay (bug fix as it often rejects the request otherwise)
    message +=  "<script type='text/javascript'>\n"
                "  setTimeout(function(){ document.getElementById('img').src='/img?pic=" + String(ImageToShow) + "' ; }, " + JavaRefreshTime + ");\n"
                "</script>\n";

  message += webfooter();                      // add the standard footer

    
  server.send(200, "text/html", message);      // send the web page
  message = "";      // clear string
  
}


// ----------------------------------------------------------------
//      -ping web page requested     i.e. http://x.x.x.x/ping
// ----------------------------------------------------------------
// responds with either 'enabled' or 'disabled'

void handlePing(){

  log_system_message("ping web page requested");      
  String message = DetectionEnabled ? "enabled" : "disabled";

  server.send(404, "text/plain", message);   // send reply as plain text
  message = "";      // clear string
  
}


// ----------------------------------------------------------------
//   -Imagedata web page requested    i.e. http://x.x.x.x/imagedata
// ----------------------------------------------------------------
// display raw greyscale image block data

void handleImagedata() {

    log_system_message("Imagedata webpage requested");     

    capture_still();         // capture current image

    // build the html 

    // add the standard header with some adnl style 
    String message = webheader("td {border: 1px solid grey; width: 30px; color: red;}");       

      message += "<P>\n";                // start of section
  
      message += "<br>RAW IMAGE DATA (Blocks) - Detection is ";
      message += DetectionEnabled ? "enabled" : "disabled";
     
      // show raw image data in html tables

      // difference between images table
      message += "<BR><center>Difference<BR><table>\n";
      for (int y = 0; y < H; y++) {
        message += "<tr>";
        for (int x = 0; x < W; x++) {
          uint16_t timg = abs(current_frame[y][x] - prev_frame[y][x]);
          message += generateTD(timg);
        }         
        message += "</tr>\n";
      }
      message += "</table>";
      
      // current image table
      message += "<BR><BR>Current Frame<BR><table>\n";
      for (int y = 0; y < H; y++) {
        message += "<tr>";
        for (int x = 0; x < W; x++) {
          message += generateTD(current_frame[y][x]);       
        }
        message += "</tr>\n";
      }
      message += "</table>";

      // previous image table
      message += "<BR><BR>Previous Frame<BR><table>\n";
      for (int y = 0; y < H; y++) {
        message += "<tr>";
        for (int x = 0; x < W; x++) {
          message += generateTD(prev_frame[y][x]);       
        }
        message += "</tr>\n";
      }
      message += "</table></center>\n";

      message += "<BR>If detection is disabled the previous frame only updates when this page is refreshed, ";
      message += "otherwise it automatically refreshes around twice a second";
      message += "<BR>Each block shown here is the average reading from 16x12 pixels on the camera image, ";
      message += "The detection mask selection works on 4x4 groups of blocks";
      
      message += "<BR>\n" + webfooter();     // add standard footer html
    

    server.send(200, "text/html", message);    // send the web page

    if (!DetectionEnabled) update_frame();     // if detection disabled copy this frame to previous 
}


// generate the html for a table cell 
String generateTD(uint16_t idat) {
          String bcol = String(idat, HEX);     // block color in hex  (for style command 'background-color: #fff;')
          if (bcol.length() == 1) bcol = "0" + bcol;
          return "<td style='background-color: #" + bcol + bcol + bcol + ";'>" + String(idat) + "</td>";
}


// ----------------------------------------------------------------
//   -bootlog web page requested    i.e. http://x.x.x.x/bootlog
// ----------------------------------------------------------------
// display boot log from Spiffs

void handleBootLog() {

   log_system_message("bootlog webpage requested");     

    // build the html for /bootlog page

    String message = webheader();     // add the standard header

      message += "<P>\n";                // start of section
  
      message += "<br>SYSTEM BOOT LOG<br><br>\n";
  
      // show contents of bootlog.txt in Spiffs
        File file = SPIFFS.open("/bootlog.txt", "r");
        if (!file) message += red + "No Boot Log Available" + endcolour + "<BR>\n";
        else {
          String line;
          while(file.available()){
            line = file.readStringUntil('\n');      // read first line of text file
            message += line +"<BR>\n";
          }
        }
        file.close();

      message += "<BR><BR>" + webfooter();     // add standard footer html
    

    server.send(200, "text/html", message);    // send the web page

}


// ----------------------------------------------------------------
// -last stored image page requested     i.e. http://x.x.x.x/img
// ----------------------------------------------------------------
// pic parameter on url selects which file to display

void handleImg(){
    
    uint16_t ImageToShow = SpiffsFileCounter;     // set image to display as current image
        
    // if a image to show is specified in url
      if (server.hasArg("pic")) {
        String Bvalue = server.arg("pic");
        ImageToShow = Bvalue.toInt();
        if (ImageToShow < 1 || ImageToShow > MaxSpiffsImages) ImageToShow=SpiffsFileCounter;
      }

    log_system_message("display stored image requested: " + String(ImageToShow));

    // send image file
        String TFileName = "/" + String(ImageToShow) + ".jpg";
        File f = SPIFFS.open(TFileName, "r");                         // read file from spiffs
            if (!f) Serial.println("Error reading " + TFileName);
            else {
                size_t sent = server.streamFile(f, "image/jpeg");     // send file to web page
                if (!sent) Serial.println("Error sending " + TFileName);
                f.close();               
            }
}


// ----------------------------------------------------------------
//                       -spiffs procedures
// ----------------------------------------------------------------
// capture live image and save in spiffs (also on sd-card if present)

void capturePhotoSaveSpiffs(bool UseFlash) {

  if (DetectionEnabled == 1) DetectionEnabled = 2;               // pause motion detecting while photo is captured (don't think this is required as only one core?)

  bool ok;

  // increment image count
    SpiffsFileCounter++;
    if (SpiffsFileCounter > MaxSpiffsImages) SpiffsFileCounter = 1;
    SaveSettingsSpiffs();     // save settings in Spiffs
    

  // ------------------- capture an image -------------------
    
  RestartCamera(FRAME_SIZE_PHOTO, PIXFORMAT_JPEG);      // restart camera in jpg mode to take a photo (uses greyscale mode for motion detection)

  camera_fb_t * fb = NULL; // pointer
  ok = 0; // Boolean to indicate if the picture has been taken correctly
  byte TryCount = 0;    // attempt counter to limit retries

  do {

    TryCount ++;
      
    // use flash if required
      if (!SD_Present && UseFlash)  digitalWrite(Illumination_led, ledON);   // turn Illuminator LED on if no sd card and it is required
   
    Serial.println("Taking a photo... attempt #" + String(TryCount));
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed - rebooting camera");
      RebootCamera(PIXFORMAT_JPEG);
      fb = esp_camera_fb_get();       // try again to capture frame
      if (!fb) {
        Serial.println("Capture image failed");
        return;
      }
    }

    // restore flash status after using it as a flash
      if (ReqLEDStatus && !SD_Present) digitalWrite(Illumination_led, ledON);   
      else if (!SD_Present) digitalWrite(Illumination_led, ledOFF);
       

    // ------------------- save image to Spiffs -------------------
    
    String IFileName = "/" + String(SpiffsFileCounter) + ".jpg";      // file names to store in Spiffs
    String TFileName = "/" + String(SpiffsFileCounter) + ".txt";
    // Serial.println("Picture file name: " + IFileName);

    SPIFFS.remove(IFileName);                          // delete old image file if it exists
    File file = SPIFFS.open(IFileName, FILE_WRITE);
    if (!file) {
      Serial.println("Failed to open file in writing mode");
      return;
    }
    else {
      if (file.write(fb->buf, fb->len)) {              // payload (image), payload length
        Serial.print("The picture has been saved in ");
        Serial.print(IFileName);
        Serial.print(" - Size: ");
        Serial.print(file.size());
        Serial.println(" bytes");
      } else {
        log_system_message("Error: writing image to Spiffs...will format and try again");
        WipeSpiffs();     // format spiffs 
        file = SPIFFS.open(IFileName, FILE_WRITE);
        if (!file.write(fb->buf, fb->len)) log_system_message("Error: Still unable to write image to Spiffs");
        return;
      }
    }
    file.close();
    
    // save text file to spiffs with time info. 
      SPIFFS.remove(TFileName);   // delete old file with same name if present
      file = SPIFFS.open(TFileName, "w");
      if (!file) {
        log_system_message("Error: Failed to create date file in spiffs");
        return;
      }
      else file.println(currentTime());
      file.close();


    // ------------------- save image to SD Card -------------------
    
    if (SD_Present) {

      fs::FS &fs = SD_MMC; 
      
      // read image number from counter text file
        uint16_t Inum = 0;  
        String CFileName = "/counter.txt";   
        file = fs.open(CFileName, FILE_READ);
        if (!file) Serial.println("Unable to read counter.txt from sd card"); 
        else {
          // read contents
          String line = file.readStringUntil('\n');    
          Inum = line.toInt();
          if (Inum > 0 && Inum < 2000) Serial.println("Last stored image on SD Card was #" + line);
          else Inum = 0;
        }
        file.close();
        Inum ++;
        
      // store new image number to counter text file
        if (fs.exists(CFileName)) fs.remove(CFileName);
        file = fs.open(CFileName, FILE_WRITE);
        if (!file) Serial.println("Unable to create counter file on sd card");
        else file.println(String(Inum));
        file.close();
        
      IFileName = "/" + String(Inum) + ".jpg";     // file names to store on sd card
      TFileName = "/" + String(Inum) + ".txt";
      
      // save image
        file = fs.open(IFileName, FILE_WRITE);
        if (!file) Serial.println("Failed to create image file on sd-card: " + IFileName);
        else {
            file.write(fb->buf, fb->len);  
            file.close();
        }
        
      // save text (time and date info)
        file = fs.open(TFileName, FILE_WRITE);
        if (!file) Serial.println("Failed to create date file on sd-card: " + TFileName);
        else {
            file.println(currentTime());
            file.close();
        }
    }    
    
    // ------------------------------------------------------------

    
    esp_camera_fb_return(fb);    // return frame so memory can be released

    ok = checkPhoto(SPIFFS, IFileName);       // check if file has been correctly saved in SPIFFS
    
  } while ( !ok && TryCount < 3);             // if there was a problem taking photo try again 

  if (TryCount == 3) log_system_message("Error: Unable to capture/store image");
  
  RestartCamera(FRAME_SIZE_MOTION, PIXFORMAT_GRAYSCALE);    // restart camera back to greyscale mode for motion detection

  TRIGGERtimer = millis();                                  // reset retrigger timer to stop instant motion trigger
  if (DetectionEnabled == 2) DetectionEnabled = 1;          // restart paused motion detecting 
}


// check file saved to Spiffs ok
bool checkPhoto( fs::FS &fs, String IFileName ) {
  File f_pic = fs.open( IFileName );
  uint16_t pic_sz = f_pic.size();
  bool tres = ( pic_sz > 100 );
  if (!tres) log_system_message("Error: Problem detected taking/storing image");
  f_pic.close();
  return ( tres );
}


// ----------------------------------------------------------------
//                 -restart / reboot the camera
// ----------------------------------------------------------------
//  pixformats = PIXFORMAT_ + YUV422,GRAYSCALE,RGB565,JPEG
//  framesizes = FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA
// switches camera mode

void RestartCamera(framesize_t fsize, pixformat_t format) {
    bool ok;
    esp_camera_deinit();
      config.frame_size = fsize;
      config.pixel_format = format;
    ok = esp_camera_init(&config);
    if (ok == ESP_OK) {
      Serial.println("Camera mode switched");
      TRIGGERtimer = millis();                                // reset last image captured timer (to prevent instant trigger)
    }
    else {
      // failed so try again
        delay(50);
        ok = esp_camera_init(&config);
        if (ok == ESP_OK) Serial.println("Camera restarted");
        else Serial.println("Camera failed to restart");
    }
}


// reboot camera (used if camera is failing to respond)
void RebootCamera(pixformat_t format) {  
    log_system_message("ERROR: Problem with camera detected so rebooting it"); 
    // turn camera off then back on      
        digitalWrite(PWDN_GPIO_NUM, HIGH);
        delay(500);
        digitalWrite(PWDN_GPIO_NUM, LOW); 
        delay(500);
    RestartCamera(FRAME_SIZE_MOTION, format); 
    delay(1000);
    // try camera again, if still problem reboot esp32
        if (!capture_still()) {
            Serial.println("unable to reboot camera so rebooting esp...");
            UpdateBootlogSpiffs("Camera fault - rebooting");                     // store in bootlog
            delay(500);
            ESP.restart();   
            delay(5000);      // restart will fail without this delay
         }
}


// ----------------------------------------------------------------
//                       -wipe/format Spiffs
// ----------------------------------------------------------------

bool WipeSpiffs() {
        log_system_message("Formatting/Wiping Spiffs"); 
        bool wres = SPIFFS.format();
        if (!wres) {
          log_system_message("Error: Unable to format Spiffs");
          return 0;
        }
        SpiffsFileCounter = 0;
        TriggerTime = "Not since Spiffs wiped";
        UpdateBootlogSpiffs("Spiffs Wiped");                           // store event in bootlog file
        SaveSettingsSpiffs();                                          // save settings in Spiffs
        return 1;
}

      
// ----------------------------------------------------------------
//                       -motion has been detected
// ----------------------------------------------------------------

void MotionDetected(uint16_t changes) {

  if (DetectionEnabled == 1) DetectionEnabled = 2;                        // pause motion detecting (prob. not required?)
  
    log_system_message("Camera detected motion: " + String(changes)); 
    TriggerTime = currentTime() + " - " + String(changes) + " out of " + String(mask_active * blocksPerMaskUnit);    // store time of trigger and motion detected
    capturePhotoSaveSpiffs(UseFlash);                                     // capture an image

    // send email if long enough since last motion detection (or if this is the first one)
    if (emailWhenTriggered) {
        unsigned long currentMillis = millis();        // get current time  
        if ( ((unsigned long)(currentMillis - EMAILtimer) >= (EmailLimitTime * 1000)) || (EMAILtimer == 0) ) {

          EMAILtimer = currentMillis;    // reset timer 
      
          // send an email
              String emessage = "Camera triggered at " + currentTime();
              byte q = sendEmail(emailReceiver,"Message from CameraWifiMotion", emessage);    
              if (q==0) log_system_message("email sent ok" );
              else log_system_message("Error: sending email, error code=" + String(q) );
  
         }
         else log_system_message("Too soon to send another email");
    }

  TRIGGERtimer = millis();                                       // reset retrigger timer to stop instant motion trigger
  if (DetectionEnabled == 2) DetectionEnabled = 1;               // restart paused motion detecting

}



// ----------------------------------------------------------------
//           -testing page     i.e. http://x.x.x.x/test
// ----------------------------------------------------------------

void handleTest(){

  log_system_message("Testing page requested");      

  String message = webheader();                                      // add the standard html header

  message += "<BR>Testing page<BR><BR>\n";

  // ---------------------------- test section here ------------------------------



//        capturePhotoSaveSpiffs(1);
//        
//        // send email
//          String emessage = "Test email";
//          byte q = sendEmail(emailReceiver,"Message from CameraWifiMotion sketch", emessage);    
//          if (q==0) log_system_message("email sent ok" );
//          else log_system_message("Error: sending email code=" + String(q) );



       
  // -----------------------------------------------------------------------------

  message += webfooter();                      // add the standard footer

    
  server.send(200, "text/html", message);      // send the web page
  message = "";      // clear string
  
}


// --------------------------- E N D -----------------------------