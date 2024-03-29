#include <Wire.h> // Library for I2C communication
#include <LiquidCrystal_I2C.h> // Library for LC
#include <EEPROM.h> // Library for Eeprom

// #define SIZEOF_ARRAY(a) (sizeof(a) / sizeof( a[0] ))

/** Digital Pins */
const byte _buttonInterrupt = 3;
const byte _pump = 4;
const byte _valve1 = 5;
const byte _valve2 = 6;
const byte _valve3 = 7;
const byte _valve4 = 8;
const byte _waterLevelSensor = 9;
const byte _menuButton = 10;
const byte _menuMinus = 11;
const byte _menuPlus = 12;
const byte _warningLed = 13;

/** Analog Pins*/
const byte _moistureSensor1 = A0;
const byte _moistureSensor2 = A1;
const byte _moistureSensor3 = A2;
const byte _moistureSensor4 = A3;

/** LCD */
LiquidCrystal_I2C lcd(0x27,20,4);

/** Variables */
byte pump;
byte valve;
byte waterSensor;
byte zone1;
byte zone2;
byte zone3;
byte zone4;
byte sensorZone1;
byte sensorZone2;
byte sensorZone3;
byte sensorZone4;
byte hour;
byte minutes;
byte clockHour;
byte clockMinutes;
byte clockState;
byte limit;

/** Eeprom's address,
    Parameter's name,
    Parameter's type (0: value, 1: ON/OFF, 2: menu item),
    Parameter's value  */
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
                                { 2, "Valve", 1, &valve}};
sub_menu_type menuZones[] = {{ NULL, "Back...", 2, NULL},
                                { 3, "Zone 1", 1, &zone1},
                                { 4, "Zone 2", 1, &zone2},
                                { 5, "Zone 3", 1, &zone3},
                                { 6, "Zone 4", 1, &zone4}};
sub_menu_type menuSensors[] = {{ NULL, "Back...", 2, NULL},
                                { 7, "Zone 1", 0, &sensorZone1},
                                { 8, "Zone 2", 0, &sensorZone2},
                                { 9, "Zone 3", 0, &sensorZone3},
                                { 10, "Zone 4", 0, &sensorZone4}};
sub_menu_type menuTime[] = {{ NULL, "Back...", 2, NULL},
                            { 11, "Hour", 0, &hour},
                            { 12, "Minutes", 0, &minutes}};
sub_menu_type menuClock[] = {{ NULL, "Back...", 2, NULL},
                                { 13, "Hour", 0, &clockHour},
                                { 14, "Minutes", 0, &clockMinutes},
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

menu_type menus[8] = {{"Hardwares", menuHardware, 4},
                        {"Zones", menuZones, 5},
                        {"Sensors", menuSensors, 5},
                        {"Time", menuTime, 3},
                        {"Clock", menuClock, 4},
                        {"Delay", menuDelay, 2},
                        {"Save", NULL, NULL},
                        {"Reset", NULL, NULL}};

byte measures[4] = {0, 0, 0, 0};

/** Global variables */
volatile bool buttonPressed = false;
volatile byte currentButton = 0;
byte menusPos = 0;
byte subMenuPos = 0;
bool subMenu = false;
unsigned long backlightStart = 0;
unsigned long warningLedStart = 0;

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

void setup() {
    Serial.begin(9600);

    // Output pins
    pinMode(_pump, OUTPUT);
    pinMode(_valve1, OUTPUT);
    pinMode(_valve2, OUTPUT);
    pinMode(_valve3, OUTPUT);
    pinMode(_valve4, OUTPUT);
    pinMode(_warningLed, OUTPUT);

    // Input pins
    pinMode(_waterLevelSensor, INPUT_PULLUP);
    pinMode(_menuButton, INPUT_PULLUP);
    pinMode(_menuMinus, INPUT_PULLUP);
    pinMode(_menuPlus, INPUT_PULLUP);
    pinMode(_buttonInterrupt, INPUT_PULLUP);

    // Interrupt pins
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
}

/** Main loop */
void loop(){

    // True if a button have been pressed
    if (buttonPressed) {
        handleInput();
    }

    // Turn off backlight after 20s
    if ((millis() - backlightStart) > 20000) {
        lcd.noDisplay();
        lcd.noBacklight();
    }

    handleWater();
}

//////////////////////////////////////////////////////////
//                    START INPUTS                      //
//////////////////////////////////////////////////////////

/** Handle button menu */
void handleButtonMenu() {
    if (subMenu == false) {
        menusPos++;
        if (menusPos >= 8) {
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
            subMenu = false;
        } else if (menus[menusPos].subMenu[subMenuPos].type == 0) {
            *(menus[menusPos].subMenu[subMenuPos].value) += 1;
        } else {
            *(menus[menusPos].subMenu[subMenuPos].value) = !*(menus[menusPos].subMenu[subMenuPos].value);
       }
    } else {
        if (menusPos == 6) {
            saveParameters();
        } else if (menusPos == 7) {
            resetParameters();
        } else {
            subMenu = true;
        }
    }
    displayScreen();
}

/** Handle button minus */
void handleButtonMinus() {
    if (menus[menusPos].subMenu[subMenuPos].type == 0) {
        *(menus[menusPos].subMenu[subMenuPos].value) -= 1;
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
        lcd.print(*(subMenu[subMenuPos].value));
        if (menusPos == 2) {
            readMoistureSensors();
            lcd.setCursor(12, 1);
            lcd.print(measures[subMenuPos - 1]);
            lcd.print("%");
        }
    } else {
        lcd.print(*(subMenu[subMenuPos].value) ? "ON" : "OFF");
    }
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

    // Variables
    pump = 0;
    valve = 0;
    waterSensor = 0;
    zone1 = 0;
    zone2 = 0;
    zone3 = 0;
    zone4 = 0;
    sensorZone1 = 50;
    sensorZone2 = 50;
    sensorZone3 = 50;
    sensorZone4 = 50;
    hour = 0;
    minutes = 0;
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

    for  (int i = 0; i < 8; i++) {
        for (int j = 0; j < menus[i].length; j++) {
            if (menus[i].subMenu[j].address != 2) {
                EEPROM.update(menus[i].subMenu[j].address, *(menus[i].subMenu[j].value));
            }
        }
    }
}

/** Read all parameters from the Eeprom */
void loadParameters() {

    for  (int i = 0; i < 8; i++) {
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
//                    START WATER                       //
//////////////////////////////////////////////////////////

/** Water zones */
void handleWater() {

    readMoistureSensors();

    if (readWaterLevelSensor() == HIGH || !waterSensor) {
        digitalWrite(_warningLed, LOW);

        if (zone1 == true && measures[0] < sensorZone1) {
            digitalWrite(_pump, LOW);
            digitalWrite(_valve1, LOW);
        } else if (zone2 == true && measures[1] < sensorZone2) {
            digitalWrite(_pump, LOW);
            digitalWrite(_valve2, LOW);
        } else if (zone3 == true && measures[2] < sensorZone3) {
            digitalWrite(_pump, LOW);
            digitalWrite(_valve3, LOW);
        } else if (zone4 == true && measures[3] < sensorZone4) {
            digitalWrite(_pump, LOW);
            digitalWrite(_valve4, LOW);
        } else {
            stopAll();
        }
    } else {
        // warning led blinking
        if ((millis() - warningLedStart) > 1000) {
            digitalWrite(_warningLed, !digitalRead(_warningLed));
            warningLedStart = millis();
        }
        stopAll();
    }
}

/** Read the value of a moisture sensor */
void readMoistureSensors() {
    measures[0] = map(analogRead(_moistureSensor1), 1023, 0, 0, 100);
    measures[1] = map(analogRead(_moistureSensor2), 1023, 0, 0, 100);
    measures[2] = map(analogRead(_moistureSensor3), 1023, 0, 0, 100);
    measures[3] = map(analogRead(_moistureSensor4), 1023, 0, 0, 100);
}

/** Read the value of the water level sensor */
int readWaterLevelSensor() {
    return digitalRead(_waterLevelSensor);
}

/** Stop all systems */
void stopAll() {
    digitalWrite(_pump, HIGH);
    digitalWrite(_valve1, HIGH);
    digitalWrite(_valve2, HIGH);
    digitalWrite(_valve3, HIGH);
    digitalWrite(_valve4, HIGH);
}

//////////////////////////////////////////////////////////
//                    STOP WATER                        //
//////////////////////////////////////////////////////////
