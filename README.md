# M5_NightscoutMon with bluetooth connection with xdrip for iOS
## M5Stack Nightscout monitor
### Intro
##### M5Stack Nightscout monitor<br/>Copyright (C) Johan Degraeve 

##### Original developer Martin Lukasek <martin@lukasek.cz>
###### This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
###### This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
###### You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>. 
###### This software uses some 3rd party libraries:<br/>IniFile by Steve Marple (GNU LGPL v2.1)<br/>ArduinoJson by Benoit BLANCHON (MIT License)<br/>IoT Icon Set by Artur Funk (GPL v3)<br/>Additions to the code:<br/>Peter Leimbach (Nightscout token)

This is a rework of the original project creatd by Martin Lukasek, removed a lot because either I don't use it and also because this type of development is new for me.
I recommend to first read https://github.com/mlukasek/M5_NightscoutMon

### What is available in this project

There's only one screen, with the bloodglucose value and an arrow. Always the same color. A bluetooth connection can be made with an iOS device that has xdrip installed : https://github.com/JohanDegraeve/xdripswift. The iOS device can send readings directly to the M5Stack, but the M5Stack can also download readings from NightScout - a microSD card can be installed but is not required (see further on)

With xdrip one can
 * Scan for and connect to multiple M5Stacks. It's ony been tested with one for now, more tests are planned with two M5Stacks
 * Configure WiFi Access points : names and passwords, maximum 3 access points (the first 3, the original project allows up to 10 configured in the ini file)
 * configure NightScout url and token
 * configure brightness (still under development)
 * configure the text color
 * configure (optional) a password ==> if a password is set in the M5Stack, then the same password must be configured in the xdrip app - this is not strictly necessary, see further on
 * the selected bloodglucose unit (mgdl or mmol) will also be sent to the M5Stack
 * Send readings : the M5Stack can receive readings from xdrip. If it doesn't receive readings, and if there's a WiFi connection and also a NightScout url (optionally also token) configured, then the M5Stack will try to download readings from NightScout 

### To compile

To compile, in Arduino IDE set Tools - Partition Scheme - No OTA (Large APP). This is because the BLE library needs a lot of memory

### Installaton and Support

See the page created by Martin Lukasek <martin@lukasek.cz> https://github.com/mlukasek/M5_NightscoutMon#installation-and-support , just take into account previous remark about large APP, and download/clone this repository here in stead of Martin's repository

Additional useful info https://randomnerdtutorials.com/installing-the-esp32-board-in-arduino-ide-windows-instructions/

### microSD card

The M5Stack can work without microSD card and without the M5NS.INI file. If the file is missing, then whenever the M5Stack restarts, xdrip will need to be connected to configure the required parameters

### authentication - passwords

There's no strong security implemented. Just to avoid that someone else (my neighbour) connects to my M5Stack and starts sending readings, there's some authentication: xDrip (on iOS device) needs to authenticate itself towards the M5Stack.
There's two options :
* a fixed password : in this case a micro SD card must be used in the M5Stack, the ini file must have a password must be stored in the variable blepassword. Each iOS device that has xdrip installed (usually there's only one but you may have more), must have the same password configured in the Settings (see Settings - M5Stack)
* a random password : if there's no parameter named blepassword configured in the ini file, then at each startup, and the first time it connects to xDrip, it will generate a random password and send this to xdrip. You can see it in the M5Stack settings. If every there's a disconnect and a reconnect, the xdrip app must authenticate with this password


