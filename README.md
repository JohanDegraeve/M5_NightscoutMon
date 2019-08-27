# M5_NightscoutMon
## M5Stack Nightscout monitor

##### M5Stack Nightscout monitor<br/>Copyright (C) Johan Degraeve 

##### Original developer Martin Lukasek <martin@lukasek.cz>
###### This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
###### This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
###### You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>. 
###### This software uses some 3rd party libraries:<br/>IniFile by Steve Marple (GNU LGPL v2.1)<br/>ArduinoJson by Benoit BLANCHON (MIT License)<br/>IoT Icon Set by Artur Funk (GPL v3)<br/>Additions to the code:<br/>Peter Leimbach (Nightscout token)

This is a rework of the original project creatd by Martin Lukasek

The aim is to have a connection between my iPhone and the M5Stack using Bluetooth, so that it can be used for instance on bike, car, motorcycle

Unused functionality is removed : alarms, iob, ...

To compile, in Arduino IDE set Tools - Partition Scheme - No OTA (Large APP). This is because the BLE library needs a lot of memory

Still in progress


