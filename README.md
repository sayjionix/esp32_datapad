<img src="img\header.png" alt="Header">

# ESP32 Datapad & Jira Timelogger Introduction
The __ESP32 Datapad & Jira Timelogger__ is a handheld ESP32-based keypad with 20 keys, a 20x4 character LCD display and a provision for operating from a single 3,7V LiPo cell. While the __Datapad__ hardware can generally be used for multiple purposes and provides a universal hardware development platform, the __timelogger__ firmware specifically allows tracking and logging time to [__Jira__](https://www.atlassian.com/software/jira).

The main features are:
* Connect the Datapad with your Atlassian Jira account using the REST API
* Map up to 16 favorite Jira tasks to the physical keys of the Datapad
* Tap a key once to start logging your time, tap again to stop logging and transfer the time to Jira
* Seamlessly switch over from logging on one task to another
* Manually enter spent time to book to your task
* Supports configuration of 2 WiFis to use the Datapad at two locations, e.g. at home and at workplace
* NTP-based time synchronization
* Use the Datapad as a development platform for your own keypad projects

<img src="img\datapad_1.png" alt="datapad_1" height="300">
<img src="img\datapad_2.png" alt="datapad_2" height="300">

# Build Instructions
Build your own Datapad! It's fun! :)

## BOM
The following table provides a BOM with Mouser and Reichelt part numbers.

| Part     | Pcs | Type / Value                     | Mouser PN            | Reichelt PN        |
|----------|:---:|----------------------------------|----------------------|--------------------|
|          | 1   | LCD Display 20x4                 | 763-0420AZFSWGBW33V3 |                    |
|          | 1   | Olimex ESP32-S3-DevKit-Lipo      | 909-ESP32S3DEVKITLIP |                    |
| SW1-SW20 | 20  | Cherry MX Switch                 | 540-MX2A-E1NW        | CHERRY MX2A-E1NW   |
| D1-D20   | 20  | Diode 1N4148                     | 512-1N4148           | 1N 4148            |
| Q1       | 1   | Transistor NPN BC548             | 637-BC548B           | BC 548C            |
| RV1      | 1   | Trim Poti 10k flat               |                      | 64P-10K            |
| RV2      | 1   | Trim Poti 2k flat                |                      | 64P-2,0K           |
| J1       | 1   | JST PH2P Socket                  |                      | JST PH2P ST90      |
| J2       | 1   | Pin Socket 1x16, 2,54mm, THT     | 855-M20-7821646      |                    |
|          | 1   | Pin Header 1x16, 2,54mm, THT     |                      |                    |
|          | 2   | Pin Header 1x22, 2,54mm, THT     |                      |                    |
| R3       | 1   | Resistor 1/4W, 27k               |                      |                    |
| R4       | 1   | Resistor 1/4W, 0R                |                      |                    |
| SW21     | 1   | On-Off Switch                    | 611-OS102011MA1QN1   |                    |
|          | 1   | LiPo Battery 3,7V, >= 100mAh     |                      |                    |
|          | 4   | 11mm Hexagonal Spacer or similar |                      |                    |

## Ordering and populating the PCB
The PCB can be ordered at any PCB manufacturer, such as [JLCPCB](https://jlcpcb.com/) or [PCBWay](https://www.pcbway.com/). In the [pcb/Datapad/fab/](pcb/Datapad/fab/) directory, there is a ZIP file with the Gerber and drill files, that can be used as-is to upload it to the manufacturer. For most manufacturers, you can likely leave all order options with their default values.

Soldering the components to the PCB should be simple, as the project only uses through-hole components. A generic tip is, to start with the small and flat parts first, such as the diodes and resistors, and then progress with the more bulky parts, such as the Cherry MX switches.

Solder the two 1x22 pin headers to the bottom of the Olimex ESP32 board and solder the 1x16 pin header to the bottom of the LCD display.

<img src="img\display.png" alt="display" width="450">

It is recommended to solder the ESP32 with the attached pin headers to the main PCB as flush as possible to have enough clearance for the LCD display. Also, solder the ESP32 to the main PCB as one of the last steps. This way, you don't "lose" the ESP32 board in case any other step of the soldering goes wrong.

To power the ESP32 board from battery, solder a cable between the JST connector on the board and the Datapad PCB. Attach the battery with it's connector to the socket on the Datapad PCB.

<img src="img\esp32_board.png" alt="esp32_board" width="450">

<img src="img\battery_cable.png" alt="battery_cable" width="450">

## Programming the ESP32
This project is built and optimized using the PlatformIO ecosystem.

1. Download and install [VS Code](https://code.visualstudio.com/).
2. Install the PlatformIO IDE module directly from the VS Code Extensions Marketplace.
3. Connect the ESP32 board up to your PC with the right-side USB-C connector (when looking to the front of the USB ports. Labeled "USB-UART1").
4. Access the PlatformIO sidebar menu (represented by the Ant icon on the left panel grid) and open the timelogger project in the folder that contains the _platformio.ini_
5. Click the "Upload" action button (represented by the right-facing horizontal arrow icon along the bottom window status bar).
6. Wait for the code to be compiled and uploaded to the ESP32 via it's serial bootloader.

## 3D-printed Parts
There are *.stl files available in the [mechanical/](mechanical/) directory for two different variants of housings.

<img src="img\case1.png" alt="case1" width="450">

For the Cherry MX Keycaps, the following free online model is recommended. However, any Cherry MX compatible keycaps can be used.<br>
https://makerworld.com/de/models/132469-xda-style-keycaps-blank#profileId-143606

# Operating Manual
## Keypad Layout
<img src="img\keypad_layout.png" alt="keypad_layout" height="300">

## Energy Saving
After 60 seconds of inactivity, the Datapad will turn off the LCD display and backlight and it will bring the ESP32 into light sleep mode. Any running timers will keep on running in the background. To wake the Datapad up again, simply press any key on the keypad. This keypress will not trigger any action other than waking up.

## Display settings
Use the two potentiometers to adjust display contrast and backlight intensity.<br>
<img src="img\potentiometers.png" alt="potentiometers" height="200">

## Battery operation
The On/Off switch on the top of the Datapad connects/disconnects the battery from the circuit. That means:

* to charge the battery from USB, make sure the On/Off switch is set to "On".
* the Datapad will be powered independently of the On/Off switch, once any of the two USB-C ports is connected to a power source.

## Base configuration
The basic configuration of the Datapad is performed via a serial terminal connection to your PC. You can use the same USB-C connection that you used for programming the ESP32 board and either utilize PlatformIO's Serial Monitor feature or any other serial terminal program of your choice. The serial setting are 115200baud, 8N1.

The basic configuration via the serial terminal includes:
1. WiFi 1 Name (SSID)
2. WiFi 1 Password
3. WiFi 2 Name (SSID)
4. WiFi 2 Password
5. Jira-Host
6. Base64 Credentials
7. Max Timer Duration

If the Datapad is started the first time and there is no configuration yet, it will prompt you to connect to your PC. The firmware can not start without the basic setup and having at least WiFi 1 configured and the Jira-Host and the Base64 Credentials set.

You can re-enter the basic configuration again at any time by pressing and holding the _SHIFT_ key + _ENTER_ key, while turning on the Datapad.

Troubleshooting: If you don't see any output in the terminal, try sending anything (except a number between 1-8) to the Datapad. The firmware will reject this as an invalid choice for the main menu and re-render the main menu.

### Create your base64 credentials from your Jira API Token
A base64 representation of your Jira API token is required to access Jira's REST API. To get this, you have to perform the following steps:

1. Generate an API token for Jira using your Atlassian Account. Go to your account settings -> Security -> API Token.
2. Build a string of the form useremail:api_token.
3. BASE64 encode the string.

   Linux/Unix/MacOS:
   ```
   echo -n "user@example.com:api_token_string" | base64
   ```

   Windows 7 and later, using Microsoft Powershell:
   ```
   $Text = ‘user@example.com:api_token_string’
   $Bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
   $EncodedText = [Convert]::ToBase64String($Bytes)
   $EncodedText
   ```

## Jira task mapping
If the Datapad starts with a valid configuration, it will show a 4x4 matrix of your 16 favorite Jira IDs on the LCD display (each Jira ID is consisting of the alphanumeric Jira key, followed by an unique number, e.g. "MGMT-2"). This 4x4 matrix corresponds to the lower 4x4 segment of the physical keypad.

If no Jira IDs have been mapped yet, each cell of the matrix will show "****".<br>
<img src="img\lcd_empty_slots.png" alt="lcd_empty_slots" height="200">

Once Jira IDs have been mapped to the 4x4 matrix, the Datapad will periodically cycle through indicating the Jira key and the unique identifier at the given cell on the LCD display.<br>
<img src="img\lcd_favorites_key.png" alt="lcd_favorites_key" height="200">
<img src="img\lcd_favorites_num.png" alt="lcd_favorites_num" height="200">

### Map a Jira task ID to a physical key
To map a new Jira task to any of the free slots of your 4x4 matrix of favorites, simply press and hold the _SHIFT_ key and then press the key that corresponds to the slot you like to map. A dialog will show on the LCD display that prompts you for the Jira ID. The Jira ID can be entered using the T9-feature of the keypad. Each Jira ID is consisting of the alphanumeric Jira key, followed by an unique number, e.g. "MGMT-2".

<img src="img\lcd_task_entry.png" alt="lcd_task_entry" height="200">

To correct a typing mistake or to exit the dialog completely, simply press _CANCEL_ (repeatedly).
To conclude your entry and map the Jira ID, press _ENTER_. The Datapad will call the REST API to fetch information about the task, such as the task's name. If the task can not be found or there is any other issue with the connection to your Jira server, the mapping will be completed, calling the task "Unknown Task". 

### Remove a Jira task ID from a physical key
To remove a Jira task from the 4x4 matrix of favorites, simply press and hold the _CANCEL_ key before pressing the key of the task you like to remove. A confirmation dialoge will prompt you to either continue by pressing _ENTER_, or to cancel the removal by pressing _CANCEL_.

<img src="img\lcd_clear_task.png" alt="lcd_clear_task" height="200">

Once the Jira task is removed, the slot becomes free again and is displayed as "****".

### Show task name
When mapping a Jira task to one of the slots of the 4x4 matrix, the Datapad will try to obtain the task's name via the Jira REST API. To display the name of a task in your 4x4 matrix, simply press and hold the _ENTER_ key before pressing the key of the task you like to show. This can be helpful when you are unsure to recognize the right task just judging from the Jira ID.

<img src="img\lcd_task_name.png" alt="lcd_task_name" height="200">

## Log time
There are two ways to log time to your Jira tasks.

### Automatic timer mode
A worklog timer can be started for any of the Jira tasks from the 4x4 matrix of favorites by simply pressing the key that corresponds to the task you like to log. Once the timer has started, the LCD display switches to an indication of the running timer. To stop a running timer, press the according task key again. A dialog will prompt you to confirm whether you would like to log the recorded timer to Jira. Press the _ENTER_ key once you are ready to log the time. The Datapad will then transmit the time to Jira via the REST API and automatically calculate the starting time of the activity based on the recorded minutes. Press _CANCEL_ to discard the recorded time.

<img src="img\lcd_timer.png" alt="lcd_timer" height="200">

If you start a timer in parallel on a different Jira task, the Datapad will automatically stop the previously running timer and prompt you, whether to keep or discard the recording of the previous timer. It will then start the new timer.

Timers can only be started on non-empty slots.

### Direct entry
Time can also be entered directly to be logged to a certain Jira task. This is helpful if you forgot to start the timer before you started working on your activity and you would like to restrospecively log the time.

Press and hold _DIRECT_ and then press the key that corresponds to the task you like to log to. A dialog will prompt you to enter the time in minutes using the numeric keys. Press the _CANCEL_ key to correct your entry or to completely cancel the direct time entry. Press the _ENTER_ key once you are ready to log the time. The Datapad will then transmit the time to Jira via the REST API and automatically calculate the starting time of the activity based on the entered minutes.

<img src="img\lcd_manual_time.png" alt="lcd_manual_time" height="200">

## Information Display
When pressing the _SHIFT_ and _INFO_ keys simultaneously while being in the 4x4 overview, the LCD display will show:
* Firmware version
* Battery voltage (when running from battery) or "USB connected" (when being powered from USB)
* Actual time and date. Time and date will be fetched via NTP every 3 hours. This is to compensate for the poor crystal accuracy of the ESP32 board and the clock drift that comes as a result

<img src="img\lcd_info.png" alt="lcd_info" height="200">

Press _CANCEL_ or _ENTER_ to return.

# Hardware Changelog
**v1.0, 31.05.2026:** First fab release of PCB.

# License
The Datapad Timelogger is released under the GNU GPLv3.<br>
Please refer to the [LICENSE](./LICENSE) file for more information.

