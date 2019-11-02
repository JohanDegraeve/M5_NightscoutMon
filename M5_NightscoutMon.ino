#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

/*  M5Stack Nightscout monitor
    Copyright (C) 2019 Johan Degraeve

    Connects to xdripswift via Bluetooth. Receives readings and slope. Shows both on screen.
    Settings can be configured via xdripswift.
    
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>. 

    Original code by Martin Lukasek <martin@lukasek.cz>
    
    This software uses some 3rd party libraries:
    IniFile by Steve Marple <stevemarple@googlemail.com> (GNU LGPL v2.1)
    ArduinoJson by Benoit BLANCHON (MIT License) 
    IoT Icon Set by Artur Funk (GPL v3)
    Additions to the code:
    Peter Leimbach (Nightscout token)
*/

#include <M5Stack.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include "time.h"
// #include <util/eu_dst.h>
#define ARDUINOJSON_USE_LONG_LONG 1
#include <ArduinoJson.h>
#include "Free_Fonts.h"
#include "IniFile.h"
#include "M5NSconfig.h"
#include <cstring>
#include <string>


// needed to act as BLE server
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>

// if debugLogging then there's more logging, haha
const bool debugLogging = false;

extern const unsigned char wifi2_icon16x16[];

Preferences preferences;
tConfig cfg;

const char * ntpServer = "pool.ntp.org"; // "time.nist.gov", "time.google.com"

struct err_log_item {
  struct tm err_time;
  int err_code;
} err_log[10];
int err_log_ptr = 0;
int err_log_count = 0;

int dispPage = 1;
#define MAX_PAGE 2
int icon_xpos[3] = {144, 144+18, 144+2*18};
int icon_ypos[3] = {0, 0, 0};

#ifndef min
  #define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

WiFiMulti WiFiMulti;
unsigned long msCount;
unsigned long msStart;
static uint8_t lcdBrightness = 10;
static char *iniFilename = "/M5NS.INI";
static uint16_t textColor = TFT_WHITE;

// milliseconds since start of last call to connectToWiFiIfNightScoutUrlExists from within nightscout check
unsigned long milliSecondsSinceLastCallToWifiConnectFromWithinNightScoutcheck = 0;

const unsigned long minimumTimeBetweenTwoCallsToWifiConnectFromWithinNightScoutcheck = 60000;

//timestamp of latest nightscout reading, initially 0
unsigned long timeStampLatestBgReadingInSecondsUTC = 0;

// to temporary store the latest shown string, each refresh, it will be checked if it has changed and if not then no redraw, otherwise the screen flickers annoyingly
const int senssgvStringLength = 30;
char previousSensSgvStr[senssgvStringLength];

DynamicJsonDocument JSONdoc(16384);

struct NSinfo {
  uint64_t rawtime = 0;
  time_t sensTime = 0; /// time in seconds
  struct tm sensTm;
  char sensDir[32];
  float sensSgvMgDl = 0;
  float sensSgv = 0;
  int arrowAngle = 180;
} ns;

int previousArrowAngle = 180;

// if it's not an M5StackC, then it's a normal M5Stack
bool isM5StickC = false;

//////// BLE PROPERTIES  ///////

// used for creating random password
char *letters = "abcdefghijklmnopqrstuvwxyz0123456789";

BLEServer *pServer = NULL;
BLECharacteristic * pRxTxCharacteristic;
BLEService *pService;
bool bleDeviceConnected = false;
uint8_t txValue = 0;

// device name that BLE client will see when scanning
const String BLE_DeviceName = "M5Stack";

// service and characteristic uuid for ble
// characteristic will be used for read and write
const String BLE_SERVICE_UUID = "AF6E5F78-706A-43FB-B1F4-C27D7D5C762F";
const String BLE_CHARACTERISTIC_UUID = "6D810E9F-0983-4030-BDA7-C7C9A6A19C1C";

// is BLE communication needed or not ? always true. Could be used in future, for instance by setting to false in the  config file, ble would never start up and so save energy
const bool useBLE = true;

// maximum number of bytes to send in one BLE packet
const int maxBytesInOneBLEPacket = 20;

// will be set to true if we find a blepassword in the ini file
bool useConfiguredBlePassword = false;

// is authentication done or not, in case not authenticated, we won't accept any reading or anything else
bool bleAuthenticated = false;

// will be set to false as soon as BLE setup is fully finished
bool initialBLEStartUpOnGoing = true;

///// as we may work without wifi, we need to be able to set the time, we can use BLE client for that. Following variable tell us if we've already retrieved the time from the server
///// and also the moment when retrieved is noted as the number of milliseconds since start 
///// we retrieve the local time, so we don't need to care about timezone, daylight savings etc.

// time difference in seconds, between local time and utc time - to get UTC time, do localTimeStampInSecondsRetrievedFromBLEClient - diffBetweenLocalTimeAndUTCTime
unsigned long diffBetweenLocalTimeAndUTCTime = 0;

// local time, in seconds since 1.1.1970 retrieved from client - this value remains fixed once retrieved. Value 0 means not yet retrieved.
unsigned long localTimeStampInSecondsRetrievedFromBLEClient = 0;

// time in milliseconds since start of the sketch, when timeStampInSecondsRetrievedFromBLEClient was received
unsigned long milliSecondsSinceRetrievalTimeStampInSecondsRetrievedFromBLEClient = 0;

// will hold value received from BLE client, defined here once to avoid heap fragmentation
byte rxValueAsByteArray[maxBytesInOneBLEPacket];

// when receiving wifi names and passwords, xdrip will split longer strings in multiple packets. The first packet will have as fourth byte (ie index 3), 
// the number of the wifi and/or password being changed, this needs to be stored as a global variable, because only the first packet has that information
int indexForWifiNamesAndPasswords = 0;

////// NightScout properties

char NSurl[128];

/////// FUNCTIONS

// local time in seconds since 1.1.1970 , if return value is 0, then failed
unsigned long getLocalTimeInSeconds() {

  // Start by trying timestamp received from BLE client, if so calculate it and return if not get it from ble server
  // it is only after having passed here two times, that this can succeed, if localTimeStampInSecondsRetrievedFromBLEClient = 0, and ble is connected then we'll get the time
  if (localTimeStampInSecondsRetrievedFromBLEClient > 0L) {
    return localTimeStampInSecondsRetrievedFromBLEClient + (millis() - milliSecondsSinceRetrievalTimeStampInSecondsRetrievedFromBLEClient)/1000;
  }

  Serial.println("localTimeStampInSecondsRetrievedFromBLEClient <= 0");

  /// using ble, only if connected and authenticated, ask client to send time
  if (bleDeviceConnected && bleAuthenticated && pRxTxCharacteristic != NULL) {
    Serial.println(F("Sending opcode readTimeStampRx to client"));
    sendTextToBLEClient("", 0x11, 0);
  }


  /// next try using standard Arduino methods, ie ntp server
  tm  dateandtime;

  if (getLocalTime(&dateandtime)) {

    if (debugLogging) {
      Serial.println(F("getLocalTime is true"));
    }
    
    time_t utcTimeInSeconds = mktime(&dateandtime);

    if (debugLogging) {Serial.print("in getLocalTimeInSeconds, utcTimeInSeconds = ");Serial.println(utcTimeInSeconds);}

    // if diffBetweenLocalTimeAndUTCTime still 0, then set it now, based on time_zone and dst in config
    if (diffBetweenLocalTimeAndUTCTime == 0) {
      // we assume here time_zone and dst are correctly set, which should be the case as getLocalTime was working ok
      diffBetweenLocalTimeAndUTCTime = (unsigned long)((long)cfg.timeZone + (long)cfg.dst);
    }
    
    return (unsigned long) utcTimeInSeconds + diffBetweenLocalTimeAndUTCTime;
    
  } else {
    if (debugLogging) {Serial.println("getLocalTime is false");}
  }

  /// try setting up ntp , this will only work if wifi is on, after that, no matter if succeeded or not we will continue trying to get time from ble client
  /// using ntp
  if((WiFiMulti.run() == WL_CONNECTED)) {
    if (debugLogging) {Serial.println("calling configtime");}
    configTime(cfg.timeZone, cfg.dst, ntpServer, "time.nist.gov", "time.google.com");
  }

  return 0;  
}

// utc time in seconds since 1.1.1970 , if return value is 0, then failed
unsigned long getUTCTimeInSeconds() {
  unsigned long localTimeInSeconds = getLocalTimeInSeconds();
  if (localTimeInSeconds > 0) {
    return localTimeInSeconds - diffBetweenLocalTimeAndUTCTime;
  } else {
    return 0;
  }
}

void setPageIconPos(int page) {
  switch(page) {
    case 0:
      icon_xpos[0] = 144;
      icon_xpos[1] = 144+18;
      icon_xpos[2] = 144+2*18;
      icon_ypos[0] = 0;
      icon_ypos[1] = 0;
      icon_ypos[2] = 0;
      break;
    case 1:
      icon_xpos[0] = 126;
      icon_xpos[1] = 126+18;
      icon_xpos[2] = 126+18; // 320-16;
      icon_ypos[0] = 0;
      icon_ypos[1] = 0;
      icon_ypos[2] = 18; // 220;
      break;
    default:
      icon_xpos[0] = 144;
      icon_xpos[1] = 144+18;
      icon_xpos[2] = 144+2*18;
      icon_ypos[0] = 0;
      icon_ypos[1] = 0;
      icon_ypos[2] = 0;
      break;
  }
}

void drawIcon(int16_t x, int16_t y, const uint8_t *bitmap, uint16_t color) {
  int16_t w = 16;
  int16_t h = 16; 
  int32_t i, j, byteWidth = (w + 7) / 8;
  for (j = 0; j < h; j++) {
    for (i = 0; i < w; i++) {
      if (pgm_read_byte(bitmap + j * byteWidth + i / 8) & (128 >> (i & 7))) {
        M5.Lcd.drawPixel(x + i, y + j, color);
      }
    }
  }
}

void connectToWiFiIfNightScoutUrlExists() {

  // check if nightscouturl exists, otherwise don't even try to connect
  if ( sizeOfStringInCharArray(cfg.url, 64) == 0) {
    return;
  }

  if((WiFiMulti.run() == WL_CONNECTED)) {
    return;
  }
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  Serial.println(F("WiFi connect start"));

  // We start by connecting to a WiFi network
  for(int i=0; i<=9; i++) {
    if((cfg.wlanssid[i][0]!=0) && (cfg.wlanpass[i][0]!=0)) {
      if (debugLogging) {
        Serial.print(F("Adding access point "));Serial.print(cfg.wlanssid[i]);Serial.print(F(" with password "));Serial.println(cfg.wlanpass[i]);
      }
      WiFiMulti.addAP(cfg.wlanssid[i], cfg.wlanpass[i]);
    }
  }

  
  Serial.println(F("Wait for WiFi... "));

  if (WiFiMulti.run() != WL_CONNECTED) {
      Serial.println(F("Wifi not connected"));
      yield();
      return;
  }

  Serial.println(F(""));
  Serial.println(F("WiFi connected to SSID ")); Serial.println(WiFi.SSID());

  Serial.println(F(""));

  Serial.println(F("Connection done"));

}

// the setup routine runs once when M5Stack starts up
void setup() {

    // initialize previousSensSgvStr
    strcpy(previousSensSgvStr, "");
    
    // initialize the M5Stack object
    M5.begin();

    // set text color foreground, black background, always
    M5.Lcd.setTextColor(textColor, TFT_BLACK);
    
    // prevent button A "ghost" random presses
    Wire.begin();
    SD.begin();
    
    // Lcd display
    M5.Lcd.setBrightness(100);
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(0, 0);//(0, 0, 1) for mini
    M5.Lcd.setTextSize(2);// 1 for mini
    yield();

    Serial.print(F("Free Heap: ")); Serial.println(ESP.getFreeHeap());

    uint8_t cardType = SD.cardType();

    if(cardType == CARD_NONE){
        Serial.println(F("No SD card attached"));
        M5.Lcd.println(F("No SD card attached"));
    } else {
      
          readConfiguration(iniFilename, &cfg);
          lcdBrightness = cfg.brightness1;
          M5.Lcd.setBrightness(lcdBrightness);

          if (sizeOfStringInCharArray(cfg.blepassword, 64) >0) {
            useConfiguredBlePassword = true;
          }
          
          yield();
      
          preferences.begin("M5StackNS", false);
          if(preferences.getBool("SoftReset", false)) {
            // no startup sound after soft reset and remove the SoftReset key
            preferences.remove("SoftReset");
          }
          
          preferences.end();
      
          delay(1000);
          M5.Lcd.fillScreen(BLACK);
      
          M5.Lcd.setBrightness(lcdBrightness);
          connectToWiFiIfNightScoutUrlExists();
          yield();// seems to be to let the board to things in the background, probably related to calling connectToWiFiIfNightScoutUrlExists
      
          M5.Lcd.setBrightness(lcdBrightness);
          M5.Lcd.fillScreen(BLACK);
      
          dispPage = 1;// default page is page with value large number
          setPageIconPos(dispPage);
          
          // stat startup time
          msStart = millis();
          
          // update glycemia now
          msCount = msStart-16000L;

          configureTargetServerAndUrl(cfg.url, cfg.token);

    }

    if (useBLE) {
      Serial.println(F("Bluetooth is on, open the xdrip app on iOS device and scan for M5Stack"));
      M5.Lcd.println(F("Bluetooth is on, open the xdrip app on iOS device and scan for M5Stack"));
      setupBLE();
    }
}

int readNightscout() {

  int err=0;
    
  if((WiFiMulti.run() == WL_CONNECTED)) {

    Serial.println(F("In readNightscout"));

    HTTPClient http;
    char tmpstr[32];/// DELETE THIS OR CHECK FIRST IF DETAILED DEBUGGING IS ENABLED WITH EXTRA FLAG, TO AVOID THAT THIS GETS ALLOCATED EACH TIME AGAIN AND AGAIN

    M5.Lcd.fillRect(icon_xpos[0], icon_ypos[0], 16, 16, BLACK);
    drawIcon(icon_xpos[0], icon_ypos[0], (uint8_t*)wifi2_icon16x16, TFT_BLUE);
    
    Serial.print(F("JSON query NSurl = \'"));Serial.print(NSurl);Serial.print(F("\'\n"));
    http.begin(NSurl); //HTTP
    
    Serial.print(F("[HTTP] GET...\n"));
    // start connection and send HTTP header
    int httpCode = http.GET();
  
    // httpCode will be negative on error
    if(httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      Serial.printf("[HTTP] GET... code: %d\n", httpCode);

      // file found at server
      if(httpCode == HTTP_CODE_OK) {
        String json = http.getString();

        Serial.print(F("Free Heap = ")); Serial.println(ESP.getFreeHeap());
        
        auto JSONerr = deserializeJson(JSONdoc, json);
        Serial.println(F("JSON deserialized OK"));
        JsonArray arr=JSONdoc.as<JsonArray>();
        Serial.print(F("JSON array size = ")); Serial.println(arr.size());
        if (JSONerr || arr.size()==0) {   //Check for errors in parsing
          if(JSONerr) {
            err=1001; // "JSON parsing failed"
          } else {
            err=1002; // "No data from Nightscout"
          }

        } else {
          JsonObject obj; 
          int sgvindex = 0;
          do {
            obj=JSONdoc[sgvindex].as<JsonObject>();
            sgvindex++;
          } while ((!obj.containsKey(F("sgv"))) && (sgvindex<(arr.size()-1)));
          sgvindex--;
          if(sgvindex<0 || sgvindex>(arr.size()-1))
            sgvindex=0;

          ns.rawtime = JSONdoc[sgvindex][F("date")].as<long long>(); // sensTime is time in milliseconds since 1970, something like 1555229938118
          strlcpy(ns.sensDir, JSONdoc[sgvindex][F("direction")] | "N/A", 32);
          
          ns.sensSgv = JSONdoc[sgvindex][F("sgv")]; // get value of sensor measurement
          ns.sensTime = ns.rawtime / 1000; // no milliseconds, since 2000 would be - 946684800, but ok
          timeStampLatestBgReadingInSecondsUTC = ns.sensTime;   

          ns.sensSgvMgDl = ns.sensSgv;
          // internally we work in mmol/L
          ns.sensSgv/=18.0;
          
          localtime_r(&ns.sensTime, &ns.sensTm);
          
          setNsArrowAngle();                                          

          // screen should be updated
          updateGlycemia();
          
          Serial.print(F("sensTime = "));
          Serial.print(ns.sensTime);
          sprintf(tmpstr, " (JSON %lld)", (long long) ns.rawtime);
          Serial.print(tmpstr);
          sprintf(tmpstr, " = %s", ctime(&ns.sensTime));
          Serial.print(tmpstr);
          Serial.print(F("sensSgv = "));
          Serial.println(ns.sensSgv);
          Serial.print(F("sensDir = "));
          Serial.println(ns.sensDir);
          // Serial.print(ns.sensTm.tm_year+1900); Serial.print(F(" / ")); Serial.print(ns.sensTm.tm_mon+1); Serial.print(F(" / ")); Serial.println(ns.sensTm.tm_mday);
          Serial.print(F("Sensor time: ")); Serial.print(ns.sensTm.tm_hour); Serial.print(F(":")); Serial.print(ns.sensTm.tm_min); Serial.print(F(":")); Serial.print(ns.sensTm.tm_sec); Serial.print(F(" DST ")); Serial.println(ns.sensTm.tm_isdst);
        } 
      } else {
        err=httpCode;
      }
    } else {
      Serial.print(F("httpCode = "));Serial.print(httpCode);Serial.print(F(", errorToString = "));Serial.println(http.errorToString(httpCode));
      err=httpCode;
    }
    http.end();

    if(err!=0)
      return err;
      
  } else {
    if (millis() - milliSecondsSinceLastCallToWifiConnectFromWithinNightScoutcheck > minimumTimeBetweenTwoCallsToWifiConnectFromWithinNightScoutcheck) {
      connectToWiFiIfNightScoutUrlExists();
      yield();
      milliSecondsSinceLastCallToWifiConnectFromWithinNightScoutcheck = millis();
    }
    
  }

  M5.Lcd.fillRect(icon_xpos[0], icon_ypos[0], 16, 16, BLACK);

  return err;
}

void updateGlycemia() {
  char tmpstr[255];// MAKE GLOBAL AND AVOID RECREATION each TIME ???
  
  M5.Lcd.setTextDatum(TL_DATUM);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(0, 0);

  switch(dispPage) {
    case 1: {
      
      sprintf(tmpstr, "Glyk: %4.1f %s", ns.sensSgv, ns.sensDir);
      Serial.println(tmpstr);
      
      char sensSgvStr[senssgvStringLength];
      strcpy(sensSgvStr, "---");
      M5.Lcd.setFreeFont(FSSB24);

      struct tm timeinfo;
      // if we can't get timeinfo then skip it all

      unsigned long utcTimeInSeconds = getUTCTimeInSeconds();

      if (utcTimeInSeconds >  0L) {

        Serial.print(F("timeStampLatestBgReadingInSecondsUTC = ")); Serial.println(timeStampLatestBgReadingInSecondsUTC);
        Serial.print(F("in updateGlycemia utcTimeInSeconds = ")); Serial.println(utcTimeInSeconds);

        if (utcTimeInSeconds > timeStampLatestBgReadingInSecondsUTC + (5 * 60 + 10) || ns.sensSgvMgDl == 0) {
          Serial.println(F("utcTimeInSeconds > timeStampLatestBgReadingInSecondsUTC + (5 * 60 + 10) or ns.sensSgvMgDl == 0, not showing value"));
          // latest nightscout reading is more than 5 minutes old, don't show the value - value is "---"
          M5.Lcd.setFreeFont(FSSB24);
        } else {
                if( cfg.show_mgdl == 0 ) {
                  M5.Lcd.setFreeFont(FSSB24);
                  if(ns.sensSgvMgDl<100) {
                    sprintf(sensSgvStr, "%2.0f", ns.sensSgvMgDl);
                  } else {
                    sprintf(sensSgvStr, "%3.0f", ns.sensSgvMgDl);
                  }
                } else {
                  Serial.println("before checking ns.sensSgv");
                  if(ns.sensSgv<10) {
                    Serial.println("ns.sensSgv < 10");
                  sprintf(sensSgvStr, "%3.1f", ns.sensSgv);
                  M5.Lcd.setFreeFont(FSSB24);
                } else {
                  Serial.println("ns.sensSgv >= 10");
                  sprintf(sensSgvStr, "%4.1f", ns.sensSgv);
                  M5.Lcd.setFreeFont(FSSB18);
                }
              }

        }
      } else {
        Serial.println(F("could not get local time info"));
      }

      boolean previousEqualToNew = true;
      // check if the string to show is new
      if (strlen(sensSgvStr) != strlen(previousSensSgvStr)) {
        previousEqualToNew = false;
      } else {
        if (strcmp(previousSensSgvStr, sensSgvStr) != 0) {
          previousEqualToNew = false;
        }
      }
      if (previousArrowAngle != ns.arrowAngle) {
         previousEqualToNew = false;
         previousArrowAngle = ns.arrowAngle;
      }
      
      // if strings is new, then display the new string and copy to previousSensSgvStr
      if (!previousEqualToNew) {
         M5.Lcd.fillRect(0, 0, 320, 240, TFT_BLACK);// mini = screen size 80×160
         if (isM5StickC) {
           M5.Lcd.setTextSize(1);
           M5.Lcd.setTextDatum(MC_DATUM);
           M5.Lcd.drawString(sensSgvStr, 0, 19, 4);
         } else {
           M5.Lcd.setTextSize(4);
           M5.Lcd.setTextDatum(MC_DATUM);
           M5.Lcd.drawString(sensSgvStr, 160, 120, GFXFF);
         }

         /// draw arrow
         int ay=0;
         if(ns.arrowAngle>=45)
            ay=4;
         else if(ns.arrowAngle>-45)
            ay=18;
          else
            ay=30;
      
        if(ns.arrowAngle!=180) {
           if (isM5StickC) {
              drawArrow(112, 40, 10, ns.arrowAngle+85, 30, 30, textColor);
           } else {
              drawArrow(280, ay, 10, ns.arrowAngle+85, 28, 28, textColor);
           }
        }

        // copy sensSgvStr to previousSensSgvStr
        for (int i = 0; i < senssgvStringLength; i++) {
            previousSensSgvStr[i] = sensSgvStr[i];
        }
      }
    
      //drawLogWarningIcon();
    }
    break;
   
    case MAX_PAGE: {
      // display error log
      char tmpStr[32];
      HTTPClient http;
      M5.Lcd.fillScreen(BLACK);
      M5.Lcd.setCursor(0, 18);
      M5.Lcd.setTextDatum(TL_DATUM);
      M5.Lcd.setFreeFont(FMB9);
      M5.Lcd.setTextSize(1); 
      M5.Lcd.drawString(F("Date  Time  Error Log"), 0, 0, GFXFF);
      // M5.Lcd.drawString("Error", 143, 0, GFXFF);
      M5.Lcd.setFreeFont(FM9);
      if(err_log_ptr==0) {
        M5.Lcd.drawString(F("no errors in log"), 0, 20, GFXFF);
      } else {
        for(int i=0; i<err_log_ptr; i++) {
          sprintf(tmpStr, "%02d.%02d.%02d:%02d", err_log[i].err_time.tm_mday, err_log[i].err_time.tm_mon+1, err_log[i].err_time.tm_hour, err_log[i].err_time.tm_min);
          M5.Lcd.drawString(tmpStr, 0, 20+i*18, GFXFF);
          if(err_log[i].err_code<0) {
            strlcpy(tmpStr, http.errorToString(err_log[i].err_code).c_str(), 32);
          } else {
            switch(err_log[i].err_code) {
              case 1001:
                strcpy(tmpStr, "JSON parsing failed");
                break;
              case 1002:
                strcpy(tmpStr, "No data from Nightscout");
                break;
              case 1003:
                strcpy(tmpStr, "JSON2 parsing failed");
                break;
              default:              
                sprintf(tmpStr, "HTTP error %d", err_log[i].err_code);
            }
          }
          M5.Lcd.drawString(tmpStr, 132, 20+i*18, GFXFF);
        }
        M5.Lcd.setFreeFont(FMB9);
        sprintf(tmpStr, "Total errors %d", err_log_count);
        M5.Lcd.drawString(tmpStr, 0, 20+err_log_ptr*18, GFXFF);
      }

    }
    break;
  }
}

// the loop routine runs over and over again forever
void loop(){

  //Serial.println(F("in loop"));

  delay(20);

  unsigned long utcTimeInSeconds = getUTCTimeInSeconds();

  // updateglycemia and call readNightScout every 120 seconds, or if latest reading is more than 2 minutes old, then check every 15 seconds, or if utcTimeInSeconds is still 0
  // call to updateGlycemia here is only needed to make sure that if there's no recent reading, younger than 5 minutes, to make sure --- is shown
  // if readNightScout results in a new reading, then this will also call updateGlyecemia
  if((millis()-msCount>120000L) || ((millis()-msCount>5000) && utcTimeInSeconds == 0L)  || ((millis()-msCount>15000L) && utcTimeInSeconds > 0L && (utcTimeInSeconds-timeStampLatestBgReadingInSecondsUTC>120L))) {
    updateGlycemia();
    readNightscout();
    msCount = millis();  
  } else {
    if((cfg.restart_at_logged_errors>0) && (err_log_count>=cfg.restart_at_logged_errors)) {
      preferences.begin("M5StackNS", false);
      preferences.putBool("SoftReset", true);
      preferences.end();
      ESP.restart();
    }

  }

  M5.update();
}

//////// helper functions

void configureTargetServerAndUrl(char *url, char *token) {

    if(strncmp(url, "http", 4))
      strcpy(NSurl,"https://");
    else
      strcpy(NSurl,"");
    strcat(NSurl,url);
    strcat(NSurl,"/api/v1/entries.json");
    if ((token!=NULL) && (strlen(token)>0)){
      strcat(NSurl,"?token=");
      strcat(NSurl,token);
    }
  
}

void setNsArrowAngle() {
          previousArrowAngle = ns.arrowAngle;
            ns.arrowAngle = 180;
          if(strcmp(ns.sensDir,"DoubleDown")==0)
            ns.arrowAngle = 90;
          else 
            if(strcmp(ns.sensDir,"SingleDown")==0)
              ns.arrowAngle = 75;
            else 
                if(strcmp(ns.sensDir,"FortyFiveDown")==0)
                  ns.arrowAngle = 45;
                else 
                    if(strcmp(ns.sensDir,"Flat")==0)
                      ns.arrowAngle = 0;
                    else 
                        if(strcmp(ns.sensDir,"FortyFiveUp")==0)
                          ns.arrowAngle = -45;
                        else 
                            if(strcmp(ns.sensDir,"SingleUp")==0)
                              ns.arrowAngle = -75;
                            else 
                                if(strcmp(ns.sensDir,"DoubleUp")==0)
                                  ns.arrowAngle = -90;
                                else 
                                    if(strcmp(ns.sensDir,"NONE")==0)
                                      ns.arrowAngle = 180;
                                    else 
                                        if(strcmp(ns.sensDir,"NOT COMPUTABLE")==0)
                                          ns.arrowAngle = 180;

}

void drawArrow(int x, int y, int asize, int aangle, int pwidth, int plength, uint16_t color){
  float dx = (asize-10)*cos(aangle-90)*PI/180+x; // calculate X position  
  float dy = (asize-10)*sin(aangle-90)*PI/180+y; // calculate Y position  
  float x1 = 0;         float y1 = plength;
  float x2 = pwidth/2;  float y2 = pwidth/2;
  float x3 = -pwidth/2; float y3 = pwidth/2;
  float angle = aangle*PI/180-135;
  float xx1 = x1*cos(angle)-y1*sin(angle)+dx;
  float yy1 = y1*cos(angle)+x1*sin(angle)+dy;
  float xx2 = x2*cos(angle)-y2*sin(angle)+dx;
  float yy2 = y2*cos(angle)+x2*sin(angle)+dy;
  float xx3 = x3*cos(angle)-y3*sin(angle)+dx;
  float yy3 = y3*cos(angle)+x3*sin(angle)+dy;
  M5.Lcd.fillTriangle(xx1,yy1,xx3,yy3,xx2,yy2, color);
  M5.Lcd.drawLine(x, y, xx1, yy1, color);
  M5.Lcd.drawLine(x+1, y, xx1+1, yy1, color);
  M5.Lcd.drawLine(x, y+1, xx1, yy1+1, color);
  M5.Lcd.drawLine(x-1, y, xx1-1, yy1, color);
  M5.Lcd.drawLine(x, y-1, xx1, yy1-1, color);
  M5.Lcd.drawLine(x+2, y, xx1+2, yy1, color);
  M5.Lcd.drawLine(x, y+2, xx1, yy1+2, color);
  M5.Lcd.drawLine(x-2, y, xx1-2, yy1, color);
  M5.Lcd.drawLine(x, y-2, xx1, yy1-2, color);
}

void byteArrayToHexString(byte array[], unsigned int len, char buffer[])
{
    for (unsigned int i = 0; i < len; i++)
    {
        byte nib1 = (array[i] >> 4) & 0x0F;
        byte nib2 = (array[i] >> 0) & 0x0F;
        buffer[i*2+0] = nib1  < 0xA ? '0' + nib1  : 'A' + nib1  - 0xA;
        buffer[i*2+1] = nib2  < 0xA ? '0' + nib2  : 'A' + nib2  - 0xA;
    }
    buffer[len*2] = '\0';
}

// returns index of first occurrence of value '0', which can be considered as end of string 
// maxsize must be set to size of the array, for some reason getting the size of stringtext by call .lenght() or sizeof doesn't work. To avoid run-time errors, the maxsize must be passed as parameter
int sizeOfStringInCharArray(char stringtext[], int arraysize) {
 int returnValue = 0;
  while (uint8_t(stringtext[returnValue]) != 0 && returnValue < arraysize) {
    returnValue = returnValue + 1;
  }
  return returnValue;
}

// sends opCode and textToSend to characteristic. Possibly split in multiple packets
// first byte = opcode, second byte = packet number, third byte = number of packets in total
void sendTextToBLEClient(char * textToSend, uint8_t opCode, int maximumSizeOfTextToSend) {

             int sizeOfTextToSend = sizeOfStringInCharArray(textToSend, maximumSizeOfTextToSend);

             // if sizeOfTextToSend = 0, then it means there's just an opcode being sent
             if (sizeOfTextToSend == 0) {
                uint8_t dataToSend[3];
                dataToSend[0] = opCode;
                dataToSend[1] = 0x01;
                dataToSend[2] = 0x01;
                pRxTxCharacteristic->setValue(dataToSend, 3);
                pRxTxCharacteristic->notify();
                return;
             }

             // 20 (maxBytesInOneBLEPacket) bytes per packet. First byte = opcode, second byte is packet number, third byte = total number of packets
             
             // text may be longer than maximum ble packet size, need to split up, this variable tells us how many characters are already sent
             int charactersSent = 0;

             // number of packets is total size / (maxBytesInOneBLEPacket - 3) , possibly + 1
             int totalNumberOfPacketsToSent = sizeOfTextToSend/(maxBytesInOneBLEPacket - 3);
             if (sizeOfTextToSend > totalNumberOfPacketsToSent * (maxBytesInOneBLEPacket - 3)) {
                totalNumberOfPacketsToSent++;
             }

             // number of the next packet to send
             int numberOfNextPacketToSend = 1;

             // send them one by one
             while (charactersSent < sizeOfTextToSend) {

                // calculate size of packet to send
                int sizeOfNextPacketToSend = maxBytesInOneBLEPacket;
                if (numberOfNextPacketToSend == totalNumberOfPacketsToSent) {
                  sizeOfNextPacketToSend = 3 + (sizeOfTextToSend - charactersSent);//First byte = opcode, second byte is packet number, third byte = total number of packets, rest is content
                }

                // craete and populate dataToSend
                uint8_t dataToSend[sizeOfNextPacketToSend];
                dataToSend[0] = opCode;
                dataToSend[1] = numberOfNextPacketToSend;
                dataToSend[2] = totalNumberOfPacketsToSent;
                for (int i = 0; i < sizeOfNextPacketToSend - 3; i++) {
                   dataToSend[i + 3] = uint8_t(textToSend[charactersSent + i]);
                }

                // send the data 
                if (debugLogging) {
                  Serial.print(F("sending packet "));Serial.print(numberOfNextPacketToSend);Serial.print(F(" for text "));Serial.print(textToSend);Serial.print(F(" with opcode "));Serial.print(opCode);Serial.println(F(" to client"));
                }
                pRxTxCharacteristic->setValue(dataToSend, sizeOfNextPacketToSend);
                pRxTxCharacteristic->notify();

                // increase charactersSent
                charactersSent = charactersSent + (sizeOfNextPacketToSend - 3);

                // increase numberOfNextPacketToSend
                numberOfNextPacketToSend++;
                
             }

}

///////// BLE declarations

class BLECharacteristicCallBack: public BLECharacteristicCallbacks {
  
  void onWrite(BLECharacteristic *pCharacteristic) {
    
    std::string rxValue = pCharacteristic->getValue();

    if (rxValue.length() > 0) {
        
        for (int i = 0; i < maxBytesInOneBLEPacket;i++) {
          rxValueAsByteArray[i] = 0x00;
        }
        memcpy(rxValueAsByteArray, rxValue.c_str(), rxValue.length());

        // only for logging
        if (debugLogging) {
          char rxValueAsHexString[rxValue.length() * 2] = "";
          byteArrayToHexString(rxValueAsByteArray, rxValue.length(), rxValueAsHexString);
          Serial.print(F("Received Value from BLE client : "));Serial.println(rxValueAsHexString);
        }

        // see what server has been sending
        byte opCode = rxValueAsByteArray[0];
        switch (opCode) {

          /// codes for writing from client to server         
          
          case 0x01:{
             Serial.println(F("received opcode for writeNightScoutUrlTx"));
             // if it's a long url, we will receive multiple packets, the packet number is the second byte, starting at 1, the total number of 
             // packets is byte 2, then the contents start as of the 4th byte
             // there might still be packets coming 
             std::strcpy (cfg.url + (rxValueAsByteArray[1] - 1) * (maxBytesInOneBLEPacket - 3), rxValue.c_str() + 3);
             if (rxValueAsByteArray[1] == rxValueAsByteArray[2]) {
              if (debugLogging) {Serial.print("received all packets, url = ");Serial.println(cfg.url);}
              configureTargetServerAndUrl(cfg.url, cfg.token);
              connectToWiFiIfNightScoutUrlExists();
             }
          }
          break;
          
          case 0x02:{
             Serial.println(F("received opcode for writeNightScoutAPIKeyTx"));
             std::strcpy (cfg.token + (rxValueAsByteArray[1] - 1) * (maxBytesInOneBLEPacket - 3), rxValue.c_str() + 3);
             if (rxValueAsByteArray[1] == rxValueAsByteArray[2]) {
              if (debugLogging) {Serial.print("received all packets, token = ");Serial.println(cfg.token);}
              configureTargetServerAndUrl(cfg.url, cfg.token);
             }

          }
          break;
          
          case 0x03:{
            char * unitismgdl = new char[6];// value is literally "true" or "false"
             Serial.println(F("received opcode for writemgdlTx"));
             std::strcpy (unitismgdl, rxValue.c_str() + 3);// starts at postion 4, because split in packets is used
             if (strcmp(unitismgdl, "true") == 0) {
              cfg.show_mgdl = 0;
             } else {
              cfg.show_mgdl = 1;
             }
             updateGlycemia();
             delete[] unitismgdl;
          }
          break;
          
          case 0x04:
             Serial.println(F("received opcode for writebrightness1Tx"));
          break;
          
          case 0x05:
             Serial.println(F("received opcode for writebrightness2Tx"));
          break;
          
          case 0x06:
             Serial.println(F("received opcode for writebrightness3Tx"));
          break;
          
          case 0x07: {
             Serial.println(F("received opcode for writeWlanSSIDTx"));
             if (rxValueAsByteArray[1] == 1) {// it's the first packet, the first byte is the number of the wifi name
                
                //0 is ascii code 48, that means for instance if rxValueAsByteArray[3] = 49, then the actual number is 1
                // but from that we need to substract further one, because indexing starts at 0
                indexForWifiNamesAndPasswords = rxValueAsByteArray[3] - 48 - 1;
                
                std::strcpy (cfg.wlanssid[indexForWifiNamesAndPasswords], rxValue.c_str() + 4);
                
             } else if (rxValueAsByteArray[1] == 2) {// second packet
                std::strcpy (cfg.wlanssid[indexForWifiNamesAndPasswords] + maxBytesInOneBLEPacket - 4, rxValue.c_str() + 3);
             } else {// third or fourth, ...  packet
                std::strcpy (cfg.wlanssid[indexForWifiNamesAndPasswords] + (maxBytesInOneBLEPacket - 4) + (rxValueAsByteArray[1] - 2) * (maxBytesInOneBLEPacket - 3), rxValue.c_str() + 3);
             }
             
             if (rxValueAsByteArray[1] == rxValueAsByteArray[2]) {
                // all packets received
                if (debugLogging) {Serial.print("received all packets, cfg.wlanssid = ");Serial.println(cfg.wlanssid[indexForWifiNamesAndPasswords]);}
             }

          }
          break;
          
          case 0x08:{
             Serial.println(F("received opcode for writeWlanPassTx"));

             if (rxValueAsByteArray[1] == 1) {// it's the first packet, the first byte is the number of the password
                
                //0 is ascii code 48, that means for instance if rxValueAsByteArray[3] = 49, then the actual number is 1
                // but from that we need to substract further one, because indexing starts at 0
                indexForWifiNamesAndPasswords = rxValueAsByteArray[3] - 48 - 1;
                
                std::strcpy (cfg.wlanpass[indexForWifiNamesAndPasswords], rxValue.c_str() + 4);
                
             } else if (rxValueAsByteArray[1] == 2) {// second packet
                std::strcpy (cfg.wlanpass[indexForWifiNamesAndPasswords] + maxBytesInOneBLEPacket - 4, rxValue.c_str() + 3);
             } else {// third or fourth, ...  packet
                std::strcpy (cfg.wlanpass[indexForWifiNamesAndPasswords] + (maxBytesInOneBLEPacket - 4) + (rxValueAsByteArray[1] - 2) * (maxBytesInOneBLEPacket - 3), rxValue.c_str() + 3);
             }
             
             if (rxValueAsByteArray[1] == rxValueAsByteArray[2]) {
                // all packets received
                if (debugLogging) {Serial.print("received all packets, cfg.wlanssid = ");Serial.println(cfg.wlanpass[indexForWifiNamesAndPasswords]);}
             }


          }
          break;

          case 0x09: {
             Serial.println(F("received opcode for readBlePassWordTx"));

             // client is requesting the blepassword - if cfg.blepassword has length > 0, then it means it should already be known by the app, we won't send it. 
             // this is to prevent that some other app requests it
             if (sizeOfStringInCharArray(cfg.blepassword, 64) == 0) {// here in this case useConfiguredBlePassword must be false
               Serial.println(F("current blepassword has length 0, creating a new one and will send this to client"));
               for(int i = 0; i<10  ; i++) {
                 cfg.blepassword[i] = letters[random(0, 36)];
               }
               
               sendTextToBLEClient(cfg.blepassword, 0x0E, 64);

               // considering this case as authenticated
               bleAuthenticated = true;
               
             } else if (useConfiguredBlePassword) {
                Serial.println(F("'Error - 1 -  User should set password in settings', sending 0x0D to client"));
                sendTextToBLEClient("", 0x0D, 0);
                
             } else { // useConfiguredBlePassword is false and cfg.blepassword already exists, means it's already randomly generated
                Serial.println(F("blepassword has length > 0, 'Error - 2 - Password already known, user should reset M5Stack', sending 0x0F to client"));
                sendTextToBLEClient("", 0x0F, 0);
             }
          }
          break;
          
          case 0x0A: {//authenticateTx
             Serial.println(F("received opcode for authenticateTx"));
             // client is trying authentication, in case cfg.blepassword is currently 0, then we're in a situation where there's no password in the config, a new random password hasn't been
             // generated yet, the app still has an old stored temporary password, but which needs to be renewed
             if (sizeOfStringInCharArray(cfg.blepassword, 64) == 0) {
                 Serial.println(F("blepassword has length 0, creating a new one and will send this to client"));
                 for(int i = 0; i<10; i++) {
                    cfg.blepassword[i] = letters[random(0, 36)];
                 }
                 sendTextToBLEClient(cfg.blepassword, 0x0E, 64);
                 
                 // considering this case as authenticated
                 bleAuthenticated = true;
               
             } else {
               // verify the password
               bool passwordMatch = false;
               if ((sizeOfStringInCharArray(cfg.blepassword, 64) + 1) == rxValue.length()) {// rxValue is opCode + password, meaning should be 1 longer than actual password, if that's not the case then password doesn't match
                Serial.println(F("password length correct"));
                passwordMatch = true;
                 for (int i = 0; i < sizeOfStringInCharArray(cfg.blepassword, 64); i++) {
                    if (cfg.blepassword[i] != rxValueAsByteArray[i+1]) {
                      passwordMatch = false;
                      Serial.println(F("password doesn't match"));
                      break;
                    }
                 }
               } else {
                  Serial.println(F("password length not correct"));
               }

                if (passwordMatch) {
                  Serial.println(F("sending opcode authenticateSuccessRx to client"));
                  sendTextToBLEClient("", 0x0B, 0);
                  bleAuthenticated = true;
                } else {
                  Serial.println(F("sending opcode authenticateFailureRx to client, user should switch off-on the M5stack or set the right password in the app"));
                  sendTextToBLEClient("", 0x0C, 0);
                  
                  // disconnect because it may be an untrusted device that is trying to connect
                 pService->stop();
                 // resetup ble so that the M5stack restarts advertising
                 setupBLE();
                 
                  // set bleAuthenticated to false
                  bleAuthenticated = false;
                }

               
             }
            }
            break;

             case 0x10: {
                Serial.println(F("received opcode for bgReadingTx"));
                // should be one packet, will not start to compose packets, string starts at byte 3 (ie index 2), here it's multiplied with 2

                if (bleAuthenticated) {

                    // first field is bgReading in mgdl as Int, which starts in byte 3 of rxValue.c_str(),
                    // first copy to c string because functions used require this
                    char * cstr = new char [rxValue.length() + 1 - 3];
                    std::strcpy (cstr, rxValue.c_str() + 3);

                    // splitByBlanc is assigned to first field before the blanc
                    char * splitByBlanc = strtok (cstr," ");
                    ns.sensSgv = float(atoi(splitByBlanc));

                    // strange but this assigned splitByBlanc to the next field
                    splitByBlanc = strtok (NULL," ");
                    ns.rawtime = uint64_t(strtoul(splitByBlanc, NULL, 10)) * 1000;
                    
                    ns.sensTime = ns.rawtime / 1000; 
                    timeStampLatestBgReadingInSecondsUTC = ns.sensTime; 
                    ns.sensSgvMgDl = ns.sensSgv;
                    ns.sensSgv/=18.0;
                    localtime_r(&ns.sensTime, &ns.sensTm);

                    
                    updateGlycemia();
                    msCount = millis();  
                    
                    delete[] cstr;
                    
                } else {
                    Serial.println(F("BLE Not authenticated"));
                }
             }
             break;

             case 0x12: {
                Serial.println(F("received opcode for writeTimeStampTx"));
                // should be one packet, will not start to compose packets, string starts at byte 3 (ie index 2), here it's multiplied with 2

                if (bleAuthenticated) {
                    localTimeStampInSecondsRetrievedFromBLEClient = strtoul(rxValue.c_str() + 3, NULL, 0);
                    milliSecondsSinceRetrievalTimeStampInSecondsRetrievedFromBLEClient = millis();

                    if (initialBLEStartUpOnGoing) {
                      // ask upate all parameters
                      Serial.println(F("sending opcode readAllParametersRx to client"));
                      sendTextToBLEClient("", 0x16, 0);

                      initialBLEStartUpOnGoing = false;
                    }
                }
                
             }
             break;

             case 0x13: {
                Serial.println(F("received opcode for writeSlopeNameTx"));
                if (bleAuthenticated) {
                  strlcpy(ns.sensDir, rxValue.c_str() + 3, 32); 
                  setNsArrowAngle();  
                }
                
             }
             break;

             case 0x14: {
                Serial.println(F("received opcode for writeTimeOffsetTx"));
                if (bleAuthenticated) {
                  diffBetweenLocalTimeAndUTCTime = strtoul(rxValue.c_str() + 3, NULL, 0);
                }
             }
             break;

             case 0x15: {
                Serial.println(F("received opcode for writeTextColorTx"));
                if (bleAuthenticated) {
                  
                    textColor = rxValueAsByteArray[1] * 256 + rxValueAsByteArray[2];
                    Serial.print(F("textcolor value = "));Serial.println(textColor);
                    
                    // reinitialize previousSensSgvStr, to force a redisplay of the screen when calling updateGlycemia
                    strcpy(previousSensSgvStr, "");

                    // set new textcolor
                    M5.Lcd.setTextColor(textColor, TFT_BLACK);
                    updateGlycemia();
                }
             }
             break;
             
          
        }
      }
    }
};

class BLEServerCallBack: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      Serial.println(F("BLE connect"));
      bleDeviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      Serial.println(F("BLE disconnect"));
      bleDeviceConnected = false;

      // need to set authenticated to false, because the M5Stack will immediately start advertising again, so another client might connect
      bleAuthenticated = false;
    }
};

void setupBLE() {
  
  Serial.begin(115200);
  Serial.println(F("Starting BLE"));

  BLEDevice::init(BLE_DeviceName.c_str());
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new BLEServerCallBack());
  pService = pServer->createService(BLE_SERVICE_UUID.c_str());
  
  // PROPERTY_WRITE_NR : client can write a value to the characteristic, client does not expect response
  // PROPERTY_NOTIFY : server can inform client that there's a new value, confirmation from client back to server is not needed
  // in fact pRxTxCharacteristic will be used to exchange data. Write is from client to server, for example if client needs to know information, it will write a specific command
  //   to the server, the server will respond with a notify  
  pRxTxCharacteristic = pService->createCharacteristic(BLE_CHARACTERISTIC_UUID.c_str(), BLECharacteristic::PROPERTY_WRITE_NR | BLECharacteristic::PROPERTY_NOTIFY);

  // needed to make it work as notifier
  pRxTxCharacteristic->addDescriptor(new BLE2902());
  
  pRxTxCharacteristic->setCallbacks(new BLECharacteristicCallBack());
  
  // Start the service
  pService->start();

  // Start advertising
  pServer->getAdvertising()->start();
  
}
