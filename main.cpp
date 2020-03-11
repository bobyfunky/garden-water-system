#include <Wire.h> // Library for I2C communication
#include <LiquidCrystal_I2C.h> // Library for LC
#include <EEPROM.h> // Library for Eeprom
#include <DS3232RTC.h> // Library for RTC DS3231
#include <avr/sleep.h> // Library for the sleep mode
#include <Time.h>
#include <TimeLib.h>
#include "DHT.h"

// #define SIZEOF_ARRAY(a) (sizeof(a) / sizeof( a[0] ))

/** Digital Pins */
const byte _alarmInterrupt = 2;
const byte _buttonInterrupt = 3;
const byte _pump = 4;
const byte _valve1 = 5;
const byte _valve2 = 6;
const byte _fan = 7;
const byte _window = 8;
const byte _waterLevelSensor = 9;
const byte _menuButton = 10;
const byte _menuMinus = 11;
const byte _menuPlus = 12;
const byte _warningLed = 13;

/** Analog Pins*/
const byte _moistureSensor1 = A0;
const byte _moistureSensor2 = A1;
const byte _dhtSensor = A2;

/** LCD */
LiquidCrystal_I2C lcd(0x27,20,4);

/** DHT22 */
DHT dht(_dhtSensor, DHT22);

/** Variables */
byte pump;
byte valve;
byte waterSensor;
byte fan;
byte window;
byte zone1;
byte zone2;
byte sensorZone1;
byte sensorZone2;
byte sensorHumidity;
byte sensorTemp;
byte menuHour;
byte menuMinutes;
byte clockHour;
byte clockMinutes;
byte clockState;
byte limit;

/** Eeprom's address,
    Parameter's name,
    Parameter's type (0: value, 1: ON/OFF, 2: menu back),
    Parameter's value pointer  */
typedef struct
{
    byte address;
    String name;
    byte type;
    byte *value;
} sub_menu_type;

sub_menu_type menuHardware[] = {{ NULL, "Back...", 2, NULL},
                                { 0, "Pump", 1, &pump},
                                { 1, "Water sensor", 1, &waterSensor},
                                { 2, "Valve", 1, &valve},
                                { 3, "Fan", 1, &fan},
                                { 4, "Window", 1, &window}};
sub_menu_type menuZones[] = {{ NULL, "Back...", 2, NULL},
                                { 5, "Zone 1", 1, &zone1},
                                { 6, "Zone 2", 1, &zone2}};
sub_menu_type menuSensors[] = {{ NULL, "Back...", 2, NULL},
                                { 7, "Zone 1", 0, &sensorZone1},
                                { 8, "Zone 2", 0, &sensorZone2},
                                { 9, "Humidity", 0, &sensorHumidity},
                                { 10, "Temperature", 0, &sensorTemp}};
sub_menu_type menuTime[] = {{ NULL, "Back...", 2, NULL},
                            { 11, "menuHour", 0, &menuHour},
                            { 12, "menuMinutes", 0, &menuMinutes}};
sub_menu_type menuClock[] = {{ NULL, "Back...", 2, NULL},
                                { 13, "menuHour", 0, &clockHour},
                                { 14, "menuMinutes", 0, &clockMinutes},
                                { 15, "State", 1, &clockState}};
sub_menu_type menuDelay[] = {{ NULL, "Back...", 2, NULL},
                                { 16, "Limit", 0, &limit}};

/** SubMenu's name,
    SubMenu's pointer */
typedef struct
{
    String name;
    sub_menu_type *subMenu;
    byte length;
} menu_type;

menu_type menus[10] = {{"Hardwares", menuHardware, 6},
                        {"Zones", menuZones, 3},
                        {"Sensors", menuSensors, 5},
                        {"Time", menuTime, 3},
                        {"Clock", menuClock, 4},
                        {"Delay", menuDelay, 2},
                        {"Save", NULL, NULL},
                        {"Reset", NULL, NULL},
                        {"Launch tests", NULL, NULL},
                        {"Monitoring", NULL, NULL}};

byte measuresMoisture[2] = {0, 0};
float measuresHumTemp[2] = {0, 0};

/** Global variables */
volatile bool buttonPressed = false;
volatile byte currentButton = 0;
volatile bool watering = false;
byte menusPos = 0;
byte subMenuPos = 0;
bool subMenu = false;
bool editing = false;
bool testMode = false;
bool monitoring = false;
bool extracting = false;
unsigned long backlightStart = 0;
unsigned long warningLedStart = 0;
unsigned long wateringStart = 0;
unsigned long loopDelay = 0;

/** Read which button have been pressed */
void readButtons() {

    static unsigned long lastInterruptTime = 0;
    unsigned long interruptTime = millis();

    // If interrupts come faster than 200ms, assume it's a bounce and ignore
    if (interruptTime - lastInterruptTime > 200)
    {
        buttonPressed = true;

        // Handle button Menu
        int val = digitalRead(_menuButton);
        currentButton = val == LOW ? 0 : currentButton;
        // Handle button Plus
        val = digitalRead(_menuPlus);
        currentButton = val == LOW ? 1 : currentButton;
        // Handle button Minus
        val = digitalRead(_menuMinus);
        currentButton = val == LOW ? 2 : currentButton;
    }
    lastInterruptTime = interruptTime;
}

/** Wake up the Atmega */
void launchWatering() {
    watering = true;
}

void setup() {
    Serial.begin(9600);
    dht.begin();

    // Output pins
    pinMode(_pump, OUTPUT);
    pinMode(_valve1, OUTPUT);
    pinMode(_valve2, OUTPUT);
    pinMode(_fan, OUTPUT);
    pinMode(_window, OUTPUT);
    pinMode(_warningLed, OUTPUT);

    // Input pins
    pinMode(_waterLevelSensor, INPUT_PULLUP);
    pinMode(_menuButton, INPUT_PULLUP);
    pinMode(_menuMinus, INPUT_PULLUP);
    pinMode(_menuPlus, INPUT_PULLUP);
    pinMode(_buttonInterrupt, INPUT_PULLUP);

    // Interrupt pins
    attachInterrupt(digitalPinToInterrupt(_alarmInterrupt), launchWatering, RISING);
    attachInterrupt(digitalPinToInterrupt(_buttonInterrupt), readButtons, LOW);

    // Init values
    digitalWrite(_warningLed, LOW);
    stopAll();

    // Load Settings
    loadParameters();

    // Initiate the LCD
    lcd.init();
    lcd.backlight();
    displayScreen();

    // Init alarm
    RTC.setAlarm(ALM1_MATCH_DATE, 0, 0, 0, 1);
    RTC.setAlarm(ALM2_MATCH_DATE, 0, 0, 0, 1);
    RTC.alarm(ALARM_1);
    RTC.alarm(ALARM_2);
    RTC.alarmInterrupt(ALARM_1, false);
    RTC.alarmInterrupt(ALARM_2, false);
    RTC.squareWave(SQWAVE_NONE);
    setClock();
}

/** Main loop */
void loop(){

    // True if a button have been pressed
    if (buttonPressed) {
        monitoring = false;
        handleInput();
    }

    // True if the a test is running
    if (testMode) {
        executeTest();
    } else if ((millis() - loopDelay) > 1000) {
        loopDelay = millis();

        // Show monitoring
        if (monitoring) {
            displayMonitoring();
        } else if ((millis() - backlightStart) > 20000) {
            // Turn off backlight after 20s
            lcd.noDisplay();
            lcd.noBacklight();
        }

        // Handle the watering
        if (watering == true) {
            handleWater();
        }
        // Handle the fan
        handleFan();
    }
}

//////////////////////////////////////////////////////////
//                    START INPUTS                      //
//////////////////////////////////////////////////////////

/** Handle button menu */
void handleButtonMenu() {

    if (subMenu == false) {
        menusPos++;
        if (menusPos >= 10) {
            menusPos = 0;
        }
    } else {
        subMenuPos++;
        if (subMenuPos >= menus[menusPos].length) {
            subMenuPos = 0;
        }
    }
    displayScreen();
}

/** Handle button plus */
void handleButtonPlus() {
    if (subMenu) {
        if (subMenuPos == 0) {
            // When exit the time or clock submenu then set the values on the DS3231 board
            if (menusPos == 3 || menusPos == 4) {
                setTime();
                setClock();
            }
            subMenu = false;
        } else if (menus[menusPos].subMenu[subMenuPos].type == 0) {
            *(menus[menusPos].subMenu[subMenuPos].value) += 1;
            editing = true;
        } else {
            *(menus[menusPos].subMenu[subMenuPos].value) = !*(menus[menusPos].subMenu[subMenuPos].value);
       }
    } else {
        if (menusPos == 6) {
            saveParameters();
        } else if (menusPos == 7) {
            resetParameters();
        } else if (menusPos == 8) {
            testMode = !testMode;
        } else if (menusPos == 9) {
            monitoring = true;
        } else {
            subMenu = true;
            editing = false;
            subMenuPos = 0;
        }
    }
    displayScreen();
}

/** Handle button minus */
void handleButtonMinus() {
    if (menus[menusPos].subMenu[subMenuPos].type == 0) {
        *(menus[menusPos].subMenu[subMenuPos].value) -= 1;
        editing = true;
    } else {
        *(menus[menusPos].subMenu[subMenuPos].value) = !*(menus[menusPos].subMenu[subMenuPos].value);
    }
    displayScreen();
}

/** Handle buttons - call when a button is pressed */
void handleInput() {
    buttonPressed = false;

    // Handle button Menu
    if (currentButton == 0) {
        handleButtonMenu();
    }
    // Handle button Plus
    if (currentButton == 1) {
        handleButtonPlus();
    }
    // Handle button Minus
    if (currentButton == 2 && subMenu == true && subMenuPos != 0) {
        handleButtonMinus();
    }
}

//////////////////////////////////////////////////////////
//                    END INPUTS                        //
//////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////
//                    START DISPLAY                     //
//////////////////////////////////////////////////////////

/** Refresh display */
void displayScreen() {
    lcd.display();
    lcd.backlight();
    backlightStart = millis();

    if (subMenu) {
        handleSubMenus(menus[menusPos].subMenu);
    } else {
        handleMenus();
    }
}

/** Display main menu */
void handleMenus() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(menus[menusPos].name);
}

/** Display submenu */
void handleSubMenus(const sub_menu_type *subMenu) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(subMenu[subMenuPos].name);
    if (subMenu[subMenuPos].type == 2) {
        return;
    }

    lcd.setCursor(0, 1);
    if (subMenu[subMenuPos].type == 0) {
        if (editing != true && (menusPos == 3 || menusPos == 4)) {
            getTimeValues();
        }

        lcd.print(*(subMenu[subMenuPos].value));

        if (menusPos == 2) {
            readMoistureSensors();
            readHumidityTempSensor();
            if (subMenuPos == 1 || subMenuPos == 2) {
                lcd.setCursor(12, 1);
                lcd.print(measuresMoisture[subMenuPos - 1]);
                lcd.print("%");
            } else if (subMenuPos == 3) {
                lcd.setCursor(10, 1);
                lcd.print(measuresHumTemp[0], 1);
                lcd.print("%");
            } else if (subMenuPos == 4) {
                lcd.setCursor(10, 1);
                lcd.print(measuresHumTemp[1], 1);
                lcd.print("*C");
            }
        }
    } else {
        lcd.print(*(subMenu[subMenuPos].value) ? "ON" : "OFF");
    }
}

/** Display monitoring */
void displayMonitoring() {
    readMoistureSensors();
    readHumidityTempSensor();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(measuresMoisture[0]);
    lcd.print("%");
    lcd.setCursor(0, 1);
    lcd.print(measuresMoisture[1]);
    lcd.print("%");

    lcd.setCursor(10, 0);
    lcd.print(measuresHumTemp[0], 1);
    lcd.print("%");
    lcd.setCursor(10, 1);
    lcd.print(measuresHumTemp[1], 1);
    lcd.print("*C");
}

//////////////////////////////////////////////////////////
//                    END DISPLAY                       //
//////////////////////////////////////////////////////////

/** Factory reset of all parameters and menus */
void resetParameters() {
    // Menu
    menusPos = 0;
    subMenuPos = 0;
    subMenu = false;
    editing = false;

    // Variables
    pump = 0;
    valve = 0;
    waterSensor = 0;
    zone1 = 0;
    zone2 = 0;
    fan = 0;
    window = 0;
    sensorZone1 = 50;
    sensorZone2 = 50;
    sensorHumidity = 50;
    sensorTemp = 50;
    menuHour = 0;
    menuMinutes = 0;
    clockHour = 0;
    clockMinutes = 0;
    clockState = 0;
    limit = 0;
}

//////////////////////////////////////////////////////////
//                   START EEPROM                       //
//////////////////////////////////////////////////////////

/** Update all parameters in the Eeprom */
void saveParameters() {

    for  (int i = 0; i < 9; i++) {
        for (int j = 0; j < menus[i].length; j++) {
            if (menus[i].subMenu[j].type != 2 && i != 3) {
                EEPROM.update(menus[i].subMenu[j].address, *(menus[i].subMenu[j].value));
            }
        }
    }
}

/** Read all parameters from the Eeprom */
void loadParameters() {

    for  (int i = 0; i < 9; i++) {
        for (int j = 0; j < menus[i].length; j++) {
            if (menus[i].subMenu[j].type != 2) {
                *(menus[i].subMenu[j].value) = EEPROM.read(menus[i].subMenu[j].address);
            }
        }
    }
}

//////////////////////////////////////////////////////////
//                    END EEPROM                        //
//////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////
//                   START DS3231                       //
//////////////////////////////////////////////////////////

/** Set the time on the DS3231 */
void setTime() {

    tmElements_t tm;
    tm.Month = 1;
    tm.Day = 1;
    tm.Year = 2019 - 1970;
    tm.Hour = menuHour;
    tm.Minute = menuMinutes;
    tm.Second = 0;

    time_t t = makeTime(tm);
    RTC.set(t);
}

/** Set global values for the time in the menu */
void getTimeValues() {
    time_t myTime;
    myTime = RTC.get();
    menuHour = hour(myTime);
    menuMinutes = minute(myTime);
}

/** Set the clock on the DS3231 */
void setClock() {

    RTC.setAlarm(ALM1_MATCH_HOURS, 0, clockMinutes, clockHour, 0);
    RTC.alarm(ALARM_1);
    RTC.alarmInterrupt(ALARM_1, true);
}

//////////////////////////////////////////////////////////
//                    END DS3231                        //
//////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////
//                    START WINDOW                      //
//////////////////////////////////////////////////////////

/** Window motor */
void handleWindow() {

    // If the window is activated
    // if (window == true) {
    //     readAnemometer();

    //     if (windSpeed > sensorWind) {
    //         digitalWrite(_window, HIGH);
    //     } else {
    //         digitalWrite(_window, LOW);
    //     }
    // }
}

/** Read value of the anemometer */
void readAnemometer() {
    // Empty
}

//////////////////////////////////////////////////////////
//                    END WINDOW                        //
//////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////
//                    START FAN                         //
//////////////////////////////////////////////////////////

/** Extractor fan */
void handleFan() {

    // If the fan is activated
    if (fan == true) {
        readHumidityTempSensor();

        if (measuresHumTemp[0] > sensorHumidity || measuresHumTemp[1] > sensorTemp) {
            digitalWrite(_fan, HIGH);
            extracting = true;
        } else if (extracting && (measuresHumTemp[0] > sensorHumidity - 5 || measuresHumTemp[1] > sensorTemp - 5)) {
            digitalWrite(_fan, HIGH);
        } else {
            digitalWrite(_fan, LOW);
            extracting = false;
        }
    } else {
        digitalWrite(_fan, LOW);
    }
}

/** Read values of DHT22
    Reading temperature or humidity takes about 250 milliseconds */
void readHumidityTempSensor() {
    // Read humidity
    measuresHumTemp[0] = dht.readHumidity();
    // Read temperature
    measuresHumTemp[1] = dht.readTemperature();
    Serial.print(measuresHumTemp[0]);
    Serial.println("%");
    Serial.print(measuresHumTemp[1]);
    Serial.println("*C");

    // Check if any reads failed and exit early (to try again).
    if (isnan(measuresHumTemp[0]) || isnan(measuresHumTemp[1])) {
        Serial.println("Failed to read from DHT sensor!");
    }
}

//////////////////////////////////////////////////////////
//                    END FAN                           //
//////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////
//                    START WATER                       //
//////////////////////////////////////////////////////////

/** Water zones */
void handleWater() {

    // Init the starting time of watering
    if (wateringStart == 0) {
        wateringStart = millis();
    }

    readMoistureSensors();

    if (readWaterLevelSensor() == HIGH || !waterSensor) {
        digitalWrite(_warningLed, LOW);

        // Time limit of watering
        if ((millis() - wateringStart) > (limit * 60000)) {
            stopAllWatering();
            watering = false;
            wateringStart = 0;
            RTC.alarm(ALARM_1);
        }

        // Handle per zone or directly with the pump
        if (valve == true && (zone1 == true && measuresMoisture[0] < sensorZone1)
            || (zone2 == true && measuresMoisture[1] < sensorZone2)) {
            if (pump == true) {
                digitalWrite(_pump, HIGH);
            }
            handleZone(_valve1, zone1 == true && measuresMoisture[0] < sensorZone1);
            handleZone(_valve2, zone2 == true && measuresMoisture[1] < sensorZone2);
        } else if (pump == true) {
            digitalWrite(_pump, HIGH);
        } else {
            stopAllWatering();
            watering = false;
            wateringStart = 0;
            RTC.alarm(ALARM_1);
        }
    } else {
        // Warning led blinking
        if ((millis() - warningLedStart) > 1000) {
            digitalWrite(_warningLed, !digitalRead(_warningLed));
            warningLedStart = millis();
        }
        stopAllWatering();
        watering = false;
        wateringStart = 0;
    }
}

/** Active/Desactive valves */
void handleZone(const byte valve, const bool state) {

    if (state) {
        digitalWrite(_valve1, HIGH);
    } else {
        digitalWrite(_valve1, LOW);
    }
}

/** Read the value of a moisture sensor */
void readMoistureSensors() {
    measuresMoisture[0] = map(analogRead(_moistureSensor1), 1023, 0, 0, 100);
    measuresMoisture[1] = map(analogRead(_moistureSensor2), 1023, 0, 0, 100);
}

/** Read the value of the water level sensor */
int readWaterLevelSensor() {
    return digitalRead(_waterLevelSensor);
}

/** Stop all watering systems */
void stopAllWatering() {
    digitalWrite(_pump, LOW);
    digitalWrite(_valve1, LOW);
    digitalWrite(_valve2, LOW);
}

//////////////////////////////////////////////////////////
//                    STOP WATER                        //
//////////////////////////////////////////////////////////

/** Stop all systems */
void stopAll() {
    digitalWrite(_pump, LOW);
    digitalWrite(_valve1, LOW);
    digitalWrite(_valve2, LOW);
    digitalWrite(_fan, LOW);
    digitalWrite(_window, LOW);
}

//////////////////////////////////////////////////////////
//                    START TESTS                       //
//////////////////////////////////////////////////////////

/** Test hardwares connections */
void executeTest() {
    stopAll();

    lcd.setCursor(0, 1);
    lcd.print("In progress...");

    // Actuators
    digitalWrite(_warningLed, HIGH);
    delay(3000);
    digitalWrite(_warningLed, LOW);
    digitalWrite(_pump, HIGH);
    delay(3000);
    digitalWrite(_pump, LOW);
    digitalWrite(_valve1, HIGH);
    delay(3000);
    digitalWrite(_valve1, LOW);
    digitalWrite(_valve2, HIGH);
    delay(3000);
    digitalWrite(_valve2, LOW);
    digitalWrite(_fan, HIGH);
    delay(3000);
    digitalWrite(_fan, LOW);
    digitalWrite(_window, HIGH);
    delay(3000);
    stopAll();

    // Sensors
    // Humidity/temp sensor
    lcd.clear();
    lcd.setCursor(0, 0);
    readHumidityTempSensor();
    lcd.print("Humidity");
    lcd.setCursor(10, 0);
    lcd.print(measuresHumTemp[0], 1);
    lcd.print("%");
    lcd.setCursor(0, 1);
    lcd.print("Temp");
    lcd.setCursor(10, 1);
    lcd.print(measuresHumTemp[1], 1);
    lcd.print("*C");
    delay(3000);
    // Moisture sensor
    lcd.clear();
    lcd.setCursor(0, 0);
    readMoistureSensors();
    lcd.print("Sensor 1");
    lcd.setCursor(12, 0);
    lcd.print(measuresMoisture[0]);
    lcd.print("%");
    lcd.setCursor(0, 1);
    lcd.print("Sensor 2");
    lcd.setCursor(12, 1);
    lcd.print(measuresMoisture[1]);
    lcd.print("%");
    delay(3000);
    // Water level sensor
    lcd.clear();
    lcd.setCursor(0, 0);
    readWaterLevelSensor();
    lcd.print("Water level ");
    if (readWaterLevelSensor() == HIGH) {
        lcd.print("high");
    } else {
        lcd.print("low");
    }
    delay(3000);

    // Stop the test mode and show the menu
    testMode = !testMode;
    displayScreen();
}

//////////////////////////////////////////////////////////
//                    STOP TESTS                        //
//////////////////////////////////////////////////////////
