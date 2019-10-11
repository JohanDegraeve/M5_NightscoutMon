#include "M5NSconfig.h"

void printErrorMessage(uint8_t e, bool eol = true)
{
  switch (e) {
  case IniFile::errorNoError:
    Serial.print("no error");
    break;
  case IniFile::errorFileNotFound:
    Serial.print("file not found");
    break;
  case IniFile::errorFileNotOpen:
    Serial.print("file not open");
    break;
  case IniFile::errorBufferTooSmall:
    Serial.print("buffer too small");
    break;
  case IniFile::errorSeekError:
    Serial.print("seek error");
    break;
  case IniFile::errorSectionNotFound:
    Serial.print("section not found");
    break;
  case IniFile::errorKeyNotFound:
    Serial.print("key not found");
    break;
  case IniFile::errorEndOfFile:
    Serial.print("end of file");
    break;
  case IniFile::errorUnknownError:
    Serial.print("unknown error");
    break;
  default:
    Serial.print("unknown error value");
    break;
  }
  if (eol)
    Serial.println();
}

void readConfiguration(char *iniFilename, tConfig *cfg) {
  const size_t bufferLen = 80;
  char buffer[bufferLen];
    
  IniFile ini(iniFilename); //(uint8_t)"/M5NS.INI"
  
  if (!ini.open()) {
    Serial.print("Ini file ");
    Serial.print(iniFilename);
    Serial.println(" does not exist");
    M5.Lcd.println("No INI file");
    // Cannot do anything else
    while (1)
      ;
  }
  Serial.println("Ini file exists");

  // Check the file is valid. This can be used to warn if any lines
  // are longer than the buffer.
  if (!ini.validate(buffer, bufferLen)) {
    Serial.print("ini file ");
    Serial.print(ini.getFilename());
    Serial.print(" not valid: ");
    printErrorMessage(ini.getError());
    // Cannot do anything else
    M5.Lcd.println("Bad INI file");
    while (1)
      ;
  }
  
  // Fetch a value from a key which is present
  if (ini.getValue("config", "nightscout", buffer, bufferLen)) {
    Serial.print("section 'config' has an entry 'nightscout' with value ");
    Serial.println(buffer);
    strlcpy(cfg->url, buffer, 64);
  }
  else {
    Serial.print("Could not read 'nightscout' from section 'config', error was ");
    printErrorMessage(ini.getError());
    M5.Lcd.println("No Nightscout URL in INI file");
    while (1)
      ;
  }

  // begin Peter Leimbach
  // Read the value of the token parameter from the config file
  if (ini.getValue("config", "token", buffer, bufferLen)) {
    Serial.print("section 'config' has an entry 'token' with value ");
    Serial.println(buffer);
    strlcpy(cfg->token, buffer, 32);
  }
  else {
    // no token parameter set in INI file - no error just set cfg->token[0] to 0
    cfg->token[0] = '\0';
  }
  // end Peter Leimbach

  if (ini.getValue("config", "time_zone", buffer, bufferLen)) {
    Serial.print("time_zone = ");
    cfg->timeZone = atoi(buffer);
    Serial.println(cfg->timeZone);
  }
  else {
    Serial.println("NO time zone defined -> Central Europe");
    cfg->timeZone = 3600;
  }

  if (ini.getValue("config", "dst", buffer, bufferLen)) {
    Serial.print("dst = ");
    cfg->dst = atoi(buffer);
    Serial.println(cfg->dst);
  }
  else {
    Serial.println("NO DST defined -> summer time");
    cfg->dst = 3600;
  }

  if (ini.getValue("config", "show_mgdl", buffer, bufferLen)) {
    Serial.print("show_mgdl = ");
    cfg->show_mgdl = atoi(buffer);
    Serial.println(cfg->show_mgdl);
  }
  else {
    Serial.println("NO show_mgdl defined -> 0 = show mmol/L");
    cfg->show_mgdl = 0;
  }

  if (ini.getValue("config", "brightness1", buffer, bufferLen)) {
    Serial.print("brightness1 = ");
    Serial.println(buffer);
    cfg->brightness1 = atoi(buffer);
    if(cfg->brightness1<1 || cfg->brightness1>100)
      cfg->brightness1 = 50;
  }
  else {
    Serial.println("NO brightness1");
    cfg->brightness1 = 50;
  }
  
  if (ini.getValue("config", "brightness2", buffer, bufferLen)) {
    Serial.print("brightness2 = ");
    Serial.println(buffer);
    cfg->brightness2 = atoi(buffer);
    if(cfg->brightness2<1 || cfg->brightness2>100)
      cfg->brightness2 = 100;
  }
  else {
    Serial.println("NO brightness2");
    cfg->brightness2 = 100;
  }
  if (ini.getValue("config", "brightness3", buffer, bufferLen)) {
    Serial.print("brightness3 = ");
    Serial.println(buffer);
    cfg->brightness3 = atoi(buffer);
    if(cfg->brightness3<1 || cfg->brightness3>100)
      cfg->brightness3 = 10;
  }
  else {
    Serial.println("NO brightness3");
    cfg->brightness3 = 10;
  }

  for(int i=0; i<=9; i++) {
    char wlansection[10];
    sprintf(wlansection, "wlan%1d", i);

    if (ini.getValue(wlansection, "ssid", buffer, bufferLen)) {
      Serial.printf("[wlan%1d] ssid = %s\n", i, buffer);
      strlcpy(cfg->wlanssid[i], buffer,32);
    } else {
      Serial.printf("NO [wlan%1d] ssid\n", i);
      cfg->wlanssid[i][0] = 0;
    }
  
    if (ini.getValue(wlansection, "pass", buffer, bufferLen)) {
      Serial.printf("[wlan%1d] password found in config\n", i);
      strlcpy(cfg->wlanpass[i], buffer, 32);
    } else {
      Serial.printf("NO [wlan%1d] password found\n", i);
      cfg->wlanpass[i][0] = 0;
    }
  }

  if (ini.getValue("config", "blepassword", buffer, bufferLen)) {
    Serial.print("blepassword found in config\n");
    strlcpy(cfg->blepassword, buffer, 64);
  } else {
    Serial.println("NO blepassword in config\n");
    // default value , all 0
  }


}
