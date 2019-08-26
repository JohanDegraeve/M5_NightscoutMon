/*  M5Stack Nightscout monitor
    Copyright (C) 2019 Johan Degraeve
    
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



extern const unsigned char sun_icon16x16[];
extern const unsigned char clock_icon16x16[];
extern const unsigned char timer_icon16x16[];
extern const unsigned char door_icon16x16[];
extern const unsigned char warning_icon16x16[];
extern const unsigned char wifi1_icon16x16[];
extern const unsigned char wifi2_icon16x16[];

extern const unsigned char bat0_icon16x16[];
extern const unsigned char bat1_icon16x16[];
extern const unsigned char bat2_icon16x16[];
extern const unsigned char bat3_icon16x16[];
extern const unsigned char plug_icon16x16[];

Preferences preferences;
tConfig cfg;

const char* ntpServer = "pool.ntp.org"; // "time.nist.gov", "time.google.com"
struct tm localTimeInfo;
unsigned long localTimeInSeconds = 0;
int MAX_TIME_RETRY = 30;
int lastSec = 61;
int lastMin = 61;
char localTimeStr[30];

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

//timestamp of latest nightscout reading, initially 0
unsigned long timeStampLatestNightScoutReadingInSeconds = 0;

// to temporary store the latest shown string, each refresh, it will be checked if it has changed and if not then no redraw, otherwise the screen flickers annoyingly
char previousSensSgvStr[30];

DynamicJsonDocument JSONdoc(16384);

struct NSinfo {
  char sensDev[64];
  uint64_t rawtime = 0;
  time_t sensTime = 0; /// time in seconds
  struct tm sensTm;
  char sensDir[32];
  float sensSgvMgDl = 0;
  float sensSgv = 0;
  float last10sgv[10];
  bool is_xDrip = 0;  
  int arrowAngle = 180;
} ns;

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

void addErrorLog(int code){
  if(err_log_ptr>9) {
    for(int i=0; i<9; i++) {
      err_log[i].err_time=err_log[i+1].err_time;
      err_log[i].err_code=err_log[i+1].err_code;
    }
    err_log_ptr=9;
  }
  getLocalTime(&err_log[err_log_ptr].err_time);
  err_log[err_log_ptr].err_code=code;
  err_log_ptr++;
  err_log_count++;
}

void startupLogo() {
    static uint8_t brightness, pre_brightness;
    M5.Lcd.setBrightness(0);
    if(cfg.bootPic[0]==0) {
    } else {
      M5.Lcd.drawJpgFile(SD, cfg.bootPic);
    }
    M5.Lcd.setBrightness(100);
    M5.update();
}

void printLocalTime() {
  if(!getLocalTime(&localTimeInfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&localTimeInfo, "%A, %B %d %Y %H:%M:%S");
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

void buttons_test() {
  if(M5.BtnA.wasPressed()) {

    Serial.printf("A");

    if(lcdBrightness==cfg.brightness1) 
      lcdBrightness = cfg.brightness2;
    else
      if(lcdBrightness==cfg.brightness2) 
        lcdBrightness = cfg.brightness3;
      else
        lcdBrightness = cfg.brightness1;
    M5.Lcd.setBrightness(lcdBrightness);

  }
  
  if(M5.BtnC.wasPressed()) {

    Serial.printf("C");

    // initialize previousSensSgvStr
    strcpy(previousSensSgvStr, "");

    unsigned long btnCPressTime = millis();
    long pwrOffTimeout = 4000;
    int lastDispTime = pwrOffTimeout/1000;
    int longPress = 0;
    char tmpstr[32];
    while(M5.BtnC.read()) {
      M5.Lcd.setTextSize(1);
      M5.Lcd.setFreeFont(FSSB12);
      
      int timeToPwrOff = (pwrOffTimeout - (millis()-btnCPressTime))/1000;
      if((lastDispTime!=timeToPwrOff) && (millis()-btnCPressTime>800)) {
        longPress = 1;
        sprintf(tmpstr, "OFF in %1d   ", timeToPwrOff);
        M5.Lcd.drawString(tmpstr, 210, 220, GFXFF);
        lastDispTime=timeToPwrOff;
      }
      if(timeToPwrOff<=0) {
        // play_tone(3000, 100, 1);
        M5.setWakeupButton(BUTTON_C_PIN);
        M5.powerOFF();
      }
      M5.update();
    }
    if(longPress) {
      M5.Lcd.fillRect(210, 220, 110, 20, TFT_BLACK);
      drawIcon(246, 220, (uint8_t*)door_icon16x16, TFT_LIGHTGREY);
    } else {
      dispPage++;
      if(dispPage>MAX_PAGE)
        dispPage = 1;

      setPageIconPos(dispPage);
      M5.Lcd.clear(BLACK);
      msCount = millis()-16000;

    }
  }
}

void wifi_connect() {

  if((WiFiMulti.run() == WL_CONNECTED)) {
    Serial.println("In wifi_connect but already connected");
    return;
  }
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  Serial.println("WiFi connect start");

  // We start by connecting to a WiFi network
  for(int i=0; i<=9; i++) {
    if((cfg.wlanssid[i][0]!=0) && (cfg.wlanpass[i][0]!=0))
      WiFiMulti.addAP(cfg.wlanssid[i], cfg.wlanpass[i]);
  }

  
  Serial.println("Wait for WiFi... ");

  if (WiFiMulti.run() != WL_CONNECTED) {
      Serial.println("not connected");
      yield();
      return;
  }

  Serial.println("");
  Serial.println("WiFi connected to SSID "); Serial.println(WiFi.SSID());
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP().toString());

  setLocalTimeInfo();
  
  Serial.println();
  printLocalTime();

  Serial.println("Connection done");

}

// the setup routine runs once when M5Stack starts up
void setup() {

    // initialize previousSensSgvStr
    strcpy(previousSensSgvStr, "");
    
    // initialize the M5Stack object
    M5.begin();

    // set text color to white foreground, black background, always
    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    
    // prevent button A "ghost" random presses
    Wire.begin();
    SD.begin();
    
    // Lcd display
    M5.Lcd.setBrightness(100);
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.setTextSize(2);
    yield();

    Serial.print("Free Heap: "); Serial.println(ESP.getFreeHeap());

    uint8_t cardType = SD.cardType();

    if(cardType == CARD_NONE){
        Serial.println("No SD card attached");
        M5.Lcd.println("No SD card attached");
    } else {
      
          readConfiguration(iniFilename, &cfg);
          lcdBrightness = cfg.brightness1;
          M5.Lcd.setBrightness(lcdBrightness);
          
          startupLogo();
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
          wifi_connect();
          yield();// seems to be to let the board to things in the background, probably related to calling wifi_connect
      
          M5.Lcd.setBrightness(lcdBrightness);
          M5.Lcd.fillScreen(BLACK);
      
          dispPage = 1;// default page is page with value large number
          setPageIconPos(dispPage);
          
          // stat startup time
          msStart = millis();
          
          // update glycemia now
          msCount = msStart-16000;

    }

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

int readNightscout(char *url, char *token, struct NSinfo *ns) {

  Serial.println("In readNightscout");
  
  HTTPClient http;
  char NSurl[128];
  int err=0;
  char tmpstr[32];
  
  if((WiFiMulti.run() == WL_CONNECTED)) {
    // configure target server and url
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

    M5.Lcd.fillRect(icon_xpos[0], icon_ypos[0], 16, 16, BLACK);
    drawIcon(icon_xpos[0], icon_ypos[0], (uint8_t*)wifi2_icon16x16, TFT_BLUE);
    
    Serial.print("JSON query NSurl = \'");Serial.print(NSurl);Serial.print("\'\n");
    http.begin(NSurl); //HTTP
    
    Serial.print("[HTTP] GET...\n");
    // start connection and send HTTP header
    int httpCode = http.GET();
  
    // httpCode will be negative on error
    if(httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      Serial.printf("[HTTP] GET... code: %d\n", httpCode);

      // file found at server
      if(httpCode == HTTP_CODE_OK) {
        String json = http.getString();
        // Serial.println(json);
        // const size_t capacity = JSON_ARRAY_SIZE(10) + 10*JSON_OBJECT_SIZE(19) + 3840;
        // Serial.print("JSON size needed= "); Serial.print(capacity); 
        Serial.print("Free Heap = "); Serial.println(ESP.getFreeHeap());
        auto JSONerr = deserializeJson(JSONdoc, json);
        Serial.println("JSON deserialized OK");
        JsonArray arr=JSONdoc.as<JsonArray>();
        Serial.print("JSON array size = "); Serial.println(arr.size());
        if (JSONerr || arr.size()==0) {   //Check for errors in parsing
          if(JSONerr) {
            err=1001; // "JSON parsing failed"
          } else {
            err=1002; // "No data from Nightscout"
          }
          addErrorLog(err);
        } else {
          JsonObject obj; 
          int sgvindex = 0;
          do {
            obj=JSONdoc[sgvindex].as<JsonObject>();
            sgvindex++;
          } while ((!obj.containsKey("sgv")) && (sgvindex<(arr.size()-1)));
          sgvindex--;
          if(sgvindex<0 || sgvindex>(arr.size()-1))
            sgvindex=0;
          strlcpy(ns->sensDev, JSONdoc[sgvindex]["device"] | "N/A", 64);
          ns->is_xDrip = obj.containsKey("xDrip_raw");
          ns->rawtime = JSONdoc[sgvindex]["date"].as<long long>(); // sensTime is time in milliseconds since 1970, something like 1555229938118
          strlcpy(ns->sensDir, JSONdoc[sgvindex]["direction"] | "N/A", 32);
          ns->sensSgv = JSONdoc[sgvindex]["sgv"]; // get value of sensor measurement
          ns->sensTime = ns->rawtime / 1000; // no milliseconds, since 2000 would be - 946684800, but ok
          timeStampLatestNightScoutReadingInSeconds = ns->sensTime;
          for(int i=0; i<=9; i++) {
            ns->last10sgv[i]=JSONdoc[i]["sgv"];
            ns->last10sgv[i]/=18.0;
          }
          ns->sensSgvMgDl = ns->sensSgv;
          // internally we work in mmol/L
          ns->sensSgv/=18.0;
          
          localtime_r(&ns->sensTime, &ns->sensTm);
          
          ns->arrowAngle = 180;
          if(strcmp(ns->sensDir,"DoubleDown")==0)
            ns->arrowAngle = 90;
          else 
            if(strcmp(ns->sensDir,"SingleDown")==0)
              ns->arrowAngle = 75;
            else 
                if(strcmp(ns->sensDir,"FortyFiveDown")==0)
                  ns->arrowAngle = 45;
                else 
                    if(strcmp(ns->sensDir,"Flat")==0)
                      ns->arrowAngle = 0;
                    else 
                        if(strcmp(ns->sensDir,"FortyFiveUp")==0)
                          ns->arrowAngle = -45;
                        else 
                            if(strcmp(ns->sensDir,"SingleUp")==0)
                              ns->arrowAngle = -75;
                            else 
                                if(strcmp(ns->sensDir,"DoubleUp")==0)
                                  ns->arrowAngle = -90;
                                else 
                                    if(strcmp(ns->sensDir,"NONE")==0)
                                      ns->arrowAngle = 180;
                                    else 
                                        if(strcmp(ns->sensDir,"NOT COMPUTABLE")==0)
                                          ns->arrowAngle = 180;
                                          
          Serial.print("sensDev = ");
          Serial.println(ns->sensDev);
          Serial.print("sensTime = ");
          Serial.print(ns->sensTime);
          sprintf(tmpstr, " (JSON %lld)", (long long) ns->rawtime);
          Serial.print(tmpstr);
          sprintf(tmpstr, " = %s", ctime(&ns->sensTime));
          Serial.print(tmpstr);
          Serial.print("sensSgv = ");
          Serial.println(ns->sensSgv);
          Serial.print("sensDir = ");
          Serial.println(ns->sensDir);
          // Serial.print(ns->sensTm.tm_year+1900); Serial.print(" / "); Serial.print(ns->sensTm.tm_mon+1); Serial.print(" / "); Serial.println(ns->sensTm.tm_mday);
          Serial.print("Sensor time: "); Serial.print(ns->sensTm.tm_hour); Serial.print(":"); Serial.print(ns->sensTm.tm_min); Serial.print(":"); Serial.print(ns->sensTm.tm_sec); Serial.print(" DST "); Serial.println(ns->sensTm.tm_isdst);
        } 
      } else {
        addErrorLog(httpCode);
        err=httpCode;
      }
    } else {
      addErrorLog(httpCode);
      err=httpCode;
    }
    http.end();

    if(err!=0)
      return err;
      
  } else {
    Serial.println("WiFi not connected");
    wifi_connect();
  }

  M5.Lcd.fillRect(icon_xpos[0], icon_ypos[0], 16, 16, BLACK);

  return err;
}

void drawLogWarningIcon() {
  if(err_log_ptr>5)
    drawIcon(icon_xpos[0], icon_ypos[0], (uint8_t*)warning_icon16x16, TFT_YELLOW);
  else
    if(err_log_ptr>0)
      drawIcon(icon_xpos[0], icon_ypos[0], (uint8_t*)warning_icon16x16, TFT_LIGHTGREY);
    else
      M5.Lcd.fillRect(icon_xpos[0], icon_ypos[0], 16, 16, BLACK);
}

void update_glycemia() {
  char tmpstr[255];
  
  M5.Lcd.setTextDatum(TL_DATUM);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(0, 0);

  switch(dispPage) {
    case 1: {
      readNightscout(cfg.url, cfg.token, &ns);
      
      sprintf(tmpstr, "Glyk: %4.1f %s", ns.sensSgv, ns.sensDir);
      Serial.println(tmpstr);
      
      char sensSgvStr[30];
      strcpy(sensSgvStr, "---");
      M5.Lcd.setFreeFont(FSSB24);

      struct tm timeinfo;
      // if we can't get timeinfo then skip it all

      if (getLocalTime(&localTimeInfo)) {
        time_t localTimeAsLong = mktime(&localTimeInfo);
        localTimeInSeconds = (unsigned long) localTimeAsLong;

        if (localTimeInSeconds > timeStampLatestNightScoutReadingInSeconds + (5 * 60 + 10)) {
          Serial.println("localTimeInSeconds > timeStampLatestNightScoutReadingInSeconds + (5 * 60 + 10), not showing value");
          // latest nightscout reading is more than 5 minutes old, don't show the value - value is "---"
          M5.Lcd.setFreeFont(FSSB24);
        } else {
                if( cfg.show_mgdl ) {
                  M5.Lcd.setFreeFont(FSSB24);
                  if(ns.sensSgvMgDl<100) {
                    sprintf(sensSgvStr, "%2.0f", ns.sensSgvMgDl);
                  } else {
                    sprintf(sensSgvStr, "%3.0f", ns.sensSgvMgDl);
                  }
                } else {
                  if(ns.sensSgv<10) {
                  sprintf(sensSgvStr, "%3.1f", ns.sensSgv);
                  M5.Lcd.setFreeFont(FSSB24);
                } else {
                  sprintf(sensSgvStr, "%4.1f", ns.sensSgv);
                  M5.Lcd.setFreeFont(FSSB18);
                }
              }

        }
      } else {
        Serial.println("could not get local time info");
        setLocalTimeInfo();
      }

      boolean previousEqualToNew = true;
      // check if the string to show is new
      for (int i = 0; i < sizeof(sensSgvStr); i++) {
        if (previousSensSgvStr[i] != sensSgvStr[i]) {
          previousEqualToNew = false;
        }
      }
      // if strings is new, then display the new string and copy to previousSensSgvStr
      if (!previousEqualToNew) {
         M5.Lcd.fillRect(0, 40, 320, 180, TFT_BLACK);
         M5.Lcd.setTextSize(4);
         M5.Lcd.setTextDatum(MC_DATUM);
         M5.Lcd.drawString(sensSgvStr, 160, 120, GFXFF);
        for (int i = 0; i < sizeof(sensSgvStr); i++) {
          previousSensSgvStr[i] = sensSgvStr[i];
        }
      }
    
      drawLogWarningIcon();
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
      M5.Lcd.drawString("Date  Time  Error Log", 0, 0, GFXFF);
      // M5.Lcd.drawString("Error", 143, 0, GFXFF);
      M5.Lcd.setFreeFont(FM9);
      if(err_log_ptr==0) {
        M5.Lcd.drawString("no errors in log", 0, 20, GFXFF);
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

  //Serial.println("in loop");

  delay(20);
  buttons_test();

  if (getLocalTime(&localTimeInfo)) {
        time_t localTimeAsLong = mktime(&localTimeInfo);
        localTimeInSeconds = (unsigned long) localTimeAsLong;
  }

  // update glycemia every 120 seconds, or if latest reading is more than 2 minutes old, then check every 15 seconds, or if localTimeInSeconds is still 0
  if((millis()-msCount>120000) || ((millis()-msCount>5000) && localTimeInSeconds == 0)  || ((millis()-msCount>15000) && localTimeInSeconds > 0 && (localTimeInSeconds-timeStampLatestNightScoutReadingInSeconds>120))) {
    update_glycemia();
    msCount = millis();  
    Serial.print("msCount = "); Serial.println(msCount);
  } else {
    if((cfg.restart_at_logged_errors>0) && (err_log_count>=cfg.restart_at_logged_errors)) {
      preferences.begin("M5StackNS", false);
      preferences.putBool("SoftReset", true);
      preferences.end();
      ESP.restart();
    }
    char lastResetTime[10];
    strcpy(lastResetTime, "Unknown");
    if(getLocalTime(&localTimeInfo)) {
      sprintf(localTimeStr, "%02d:%02d", localTimeInfo.tm_hour, localTimeInfo.tm_min);
      // no soft restart less than 5 minutes from last restart to prevent several restarts in the same minute
      if((millis()-msStart>300000) && (strcmp(cfg.restart_at_time, localTimeStr)==0)) {
        preferences.begin("M5StackNS", false);
        preferences.putBool("SoftReset", true);

        preferences.end();
        ESP.restart();
      }
    }
  }

  M5.update();
}

void setLocalTimeInfo() {

  if((WiFiMulti.run() != WL_CONNECTED)) {
    Serial.println("In setLocalTimeInfo but WiFi not connected");
    return;
  }

  
  configTime(cfg.timeZone, cfg.dst, ntpServer, "time.nist.gov", "time.google.com");
  delay(1000);
  Serial.println("Waiting for time.");
  int i = 0;
  while(!getLocalTime(&localTimeInfo)) {
    Serial.println(".");
    delay(1000);
    i++;
    if (i > MAX_TIME_RETRY) {
      Serial.println("Gave up waiting for time to have a valid value.");
      break;
    }
  }

}
