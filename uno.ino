#include <DallasTemperature.h>

#include <NewPing.h>

#include <OneWire.h>

#include <LiquidCrystal.h>

#include <EEPROM.h>


// Config values

// lcd pins
#define RS_PIN 9
#define E_PIN  8
#define D4_PIN 7
#define D5_PIN 6
#define D6_PIN 5
#define D7_PIN 4
 
// led pins
#define RED_PIN   0
#define GREEN_PIN 1
#define BLUE_PIN  12

// Button pins
#define LEFT_BUTTON_PIN      11
#define LEFT_BUTTON_DEBOUNCE 50

#define RIGHT_BUTTON_PIN      10
#define RIGHT_BUTTON_DEBOUNCE 50

// Spray button
#define SPRAY_BUTTON_INT       1
#define SPRAY_BUTTON_DEBOUNCE  50

// magnetc switch
#define MAGNETIC_SWITCH_INT      2
#define MAGNETIC_SWITCH_DEBOUNCE 50

// Freshener
#define FRESHENER_PIN           A4
// this is how long the freshener gets power when spraying
#define FRESHENER_ON_TIME       3000
// how much time there must be between triggers to fool the
// device in spraying twice
#define INBETWEEN_TRIGGERS_TIME 200

// motion sensor
#define MOTION_SENSOR_PIN A1

// distance sensor
#define DISTANCE_TRIGGER_PIN A2
#define DISTANCE_ECHO_PIN    A3
#define MAX_DISTANCE         190
#define DISTANCE_PING_SPEED  40


// light sensor
#define LIGHT_SENSOR_PIN       A0
#define LIGHT_SENSOR_THRESHOLD 200

// temperature sensor
#define TEMP_SENSOR_PIN A5




// EEPROM Stuff

#define CHARGE_ADDR             5
#define DEFAULT_CHARGES_VALUE   2000

#define TRIGGER_DELAY_ADDR      7
#define DEFAULT_TRIGGER_DELAY   5000
#define TRIGGER_DELAY_INCREMENT 5000
#define MAX_TRIGGER_DELAY       60000


// States for the state machine
enum state {
  NOT_IN_USE,
  USE_UNKNOWN,
  USE1,
  USE2,
  USE_CLEAN,
  TRIGGERED_ONCE,
  TRIGGERED_TWICE,
  TRIGGERING,
  IN_BETWEEN_TRIGGERS,
  IN_MENU,

};

enum menu_state {
  SET_DELAY,
  RESET_CHARGES,
  EXIT
};



// set up lcd
LiquidCrystal lcd(RS_PIN,E_PIN,D4_PIN,D5_PIN,D6_PIN,D7_PIN);

// set up distance sensor
unsigned volatile long pingTimer;
NewPing sonar(DISTANCE_TRIGGER_PIN,DISTANCE_ECHO_PIN,MAX_DISTANCE);


// set up temperature
OneWire oneWire(TEMP_SENSOR_PIN);
DallasTemperature temperature(&oneWire);




volatile state state = NOT_IN_USE;
volatile bool inMenu = false;
volatile menu_state menuState;







// utility code to acccess the EEPROM in steps of 16 bits

unsigned short getAt(unsigned short addr) {
  return EEPROM.read(addr)<<8 | EEPROM.read(addr+1);
}

void decrementAt(unsigned short addr) {
  setAt(addr,getAt(addr)-1);
}

void setAt(unsigned short addr, unsigned short value) {
  EEPROM.write(addr, value>>8);
  EEPROM.write(addr+1,value);
}


// specific EEPROM helper functions for resetting tigger delay and charges



// Sets the RGB led to a specific color
void setStatusColor(int r, int g, int b) {
  digitalWrite(RED_PIN, !r);
  digitalWrite(GREEN_PIN, !g);
  digitalWrite(BLUE_PIN, !b);
}

void setupStatusLeds() {
  pinMode (RED_PIN, OUTPUT);
  pinMode (GREEN_PIN, OUTPUT);
  pinMode (BLUE_PIN, OUTPUT);
}

void setupButtons() {
  pinMode(LEFT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(RIGHT_BUTTON_PIN, INPUT_PULLUP);
}

void spray_isr();
void magneticSwitch_isr();
void setupIntButtons() {
  pinMode(SPRAY_BUTTON_INT+1, INPUT_PULLUP);
  pinMode(MAGNETIC_SWITCH_INT+1, INPUT_PULLUP);
  attachInterrupt(SPRAY_BUTTON_INT, spray_isr, FALLING);
  attachInterrupt(MAGNETIC_SWITCH_INT, magneticSwitch_isr, FALLING);
}

void setup() {

  lcd.begin(16,2);

  pinMode(FRESHENER_PIN, OUTPUT);
  pinMode(MOTION_SENSOR_PIN,INPUT);

  setupButtons();
  setupIntButtons();
  setupStatusLeds();
  setStatusColor(0, 0, 0);

  // distance sensor timer 
  pingTimer = millis();

  // used for the initial EEPROM flash
  #ifdef FACTORY_DEFAULT
  setAt(CHARGE_ADDR, DEFAULT_CHARGES_VALUE);
  setAt(TRIGGER_DELAY_ADDR, DEFAULT_TRIGGER_DELAY);
  #endif

}

// We proceed to define the senses that our freshener has. 
// These values will be updated by sensors. some of them asynchronously
// hence the volatile annonations.
volatile bool doorClosed;
bool          lightsOn;
volatile bool personPresent;
bool          flushing;



//unsigned long triggerDelay = 5000; // from EEPROM;


volatile unsigned long triggerTime;
volatile unsigned long inBetweenTriggersTime;



unsigned long triggeringTime = 0;

bool triggerTwice = false;

void triggering() {

  setStatusColor(0, 0, 0);
  digitalWrite(FRESHENER_PIN, HIGH);


  if (millis() - triggeringTime > FRESHENER_ON_TIME) {
    decrementAt(CHARGE_ADDR);
    digitalWrite(FRESHENER_PIN, LOW);
    if (!triggerTwice) {
       
      state = NOT_IN_USE;

    } else {
      state = IN_BETWEEN_TRIGGERS;
      inBetweenTriggersTime = millis();
      triggerTwice = false;
    }
  }
}

void inBetweenTriggers() {
  if (millis() - inBetweenTriggersTime > INBETWEEN_TRIGGERS_TIME) {
    state = TRIGGERING;
    triggerTwice = false;
    triggeringTime = millis();
  }
}

void triggered() {

  setStatusColor(0, 1, 0);
  if (millis() - triggerTime > (unsigned short) getAt(TRIGGER_DELAY_ADDR)) {
    state = TRIGGERING;
    triggeringTime = millis();
  }
}




// called when the door is closed
void magneticSwitch_isr() {
  static volatile unsigned long lastDebounceTime;

  if ((millis() - lastDebounceTime) > MAGNETIC_SWITCH_DEBOUNCE) {
    
    // TODO implement gevolgen
    
    
    triggerTime = lastDebounceTime = millis();

  }

}

// In this section we handle the three pushbuttons. 
// one is used to trigger a spray.
// the other two (left, and right) are used to control the operator menu.


// the interrupt service routine for when the spray button is pressed
void spray_isr() {
  static volatile unsigned long lastDebounceTime;

  if ((millis() - lastDebounceTime) > SPRAY_BUTTON_DEBOUNCE) {
    digitalWrite(A4, LOW);
    state = TRIGGERED_ONCE;
    triggerTime = lastDebounceTime = millis();

  }
}


// to get inside the menu. either of the buttons has to be pressed.
// once in the menu. the left button is the "action" button and the
// right button is the "scroll" button. 
void handleLeftButton() {
  static int lastButtonState = HIGH;
  static int buttonState;
  static unsigned long lastDebounceTime;

  int reading = digitalRead(LEFT_BUTTON_PIN);


  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > LEFT_BUTTON_DEBOUNCE) {

    if (reading != buttonState) {
      buttonState = reading;

   
      if (buttonState == LOW) {
        // any of the two buttons shoul get us into the menu
        if (!inMenu) {
          inMenu = true;
          menuState = SET_DELAY;
        } else {
          // this button is used to select actions in the operator menu
          switch (menuState) {
            case RESET_CHARGES:
              setAt(CHARGE_ADDR, DEFAULT_CHARGES_VALUE);
              break;
            case SET_DELAY:
              setAt(TRIGGER_DELAY_ADDR,TRIGGER_DELAY_INCREMENT+
                   (getAt(TRIGGER_DELAY_ADDR))
                        %  (MAX_TRIGGER_DELAY));
              break;
            case EXIT:
              inMenu = false;
              state = NOT_IN_USE;
              break;
          }

        }
      }
    }
  }
  lastButtonState = reading;
}

void handleRightButton() {
  static int lastButtonState = HIGH;
  static int buttonState;
  static long debounceDelay = 100;
  static unsigned long lastDebounceTime;

  int reading = digitalRead(RIGHT_BUTTON_PIN);

  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > RIGHT_BUTTON_DEBOUNCE) {

    if (reading != buttonState) {
      buttonState = reading;


      if (buttonState == LOW) {
        // any of the two buttons shoul get us into the menu
        if (!inMenu) {
          inMenu = true;
        }

        // uber hax :)
        menuState = (menu_state) (((int)menuState + 1) % ((int)EXIT+1));
      }
    }
  }
  lastButtonState = reading;
}


handleLightSensor() {
  lightsOn = analogRead(LIGHT_SENSOR_PIN) > LIGHT_SENSOR_THRESHOLD;
}
// Code for the motion sensor. It senses if we're flushing.
void handleMotionSensor() {
  flushing = digitalRead(MOTION_SENSOR_PIN);
}


// Code for the distance sensor . It senses if a person is present
void echo_isr() {
  
  if (sonar.check_timer()) {
    int cm = sonar.ping_result / US_ROUNDTRIP_CM;

    personPresent = cm < 70;
  }
}

// TODO this might overflow
void handleDistanceSensor() {
  if (millis() >= pingTimer) {
    pingTimer += DISTANCE_PING_SPEED;
    sonar.ping_timer(echo_isr);
  }
}


#define MAX_USE_UNKNOWN_TIME 5000
unsigned long useUnknownTimer;

void useUnknown() {
  setStatusColor(1,1,0);
  if ((millis() - useUnknownTimer) > MAX_USE_UNKNOWN_TIME) {
    state = NOT_IN_USE;
  } else {
    if (lightsOn) {
      if (doorClosed) {
        state = USE1;
      } else {
      }
    }
  }
}

void notInUse() {
  if (personPresent) {
    state = USE_UNKNOWN;
    useUnknownTimer = millis();
  }
  setStatusColor(0,0,0);
}

void use1() {
}

void use2() {
}

void useClean() {
}


void stateMachine() {

  switch (state) {
    case USE_UNKNOWN: useUnknown(); break;
    case NOT_IN_USE: notInUse(); break;
    case USE1: use1(); break;
    case USE2: use2(); break;
    case USE_CLEAN: useClean(); break;
    case TRIGGERED_ONCE: triggerTwice = false; triggered(); break;
    case TRIGGERED_TWICE: triggerTwice = true; triggered(); break;
    case TRIGGERING: triggering(); break;
    case IN_BETWEEN_TRIGGERS: inBetweenTriggers(); break;
    case IN_MENU: break;
  }
}


void menuPrinter() {
  lcd.setCursor(0,0);
  char str[17];
  switch (menuState) {
    case SET_DELAY:
      sprintf(str, "Delay: %.5u   ", getAt(TRIGGER_DELAY_ADDR));
      lcd.print(str);
      lcd.setCursor(0,1);
      lcd.print("Increase    ");
      break;
    case RESET_CHARGES:
      sprintf(str, "Charges: %.4d", getAt(CHARGE_ADDR));
      lcd.print(str);
      lcd.setCursor(0,1);
      lcd.print("Reset    ");
      break;
    case EXIT:
      lcd.print("                 ");
      lcd.setCursor(0,1);
      lcd.print("Exit             ");
      break;
  }
}

void loop() {
  // put your main code here, to run repeatedly:

  handleLeftButton();
  handleRightButton();

  if (!inMenu) {
    handleMotionSensor();
    handleDistanceSensor();
    handleLightSensor();
    
    //digitalWrite(13,digitalRead(motionSensor));
    stateMachine();
    
    lcd.setCursor(0,0);
    temperature.requestTemperatures();
    lcd.print(temperature.getTempCByIndex(0));
    lcd.print((char)223);
    lcd.print("C");
    
    
    lcd.setCursor(0,1);
    lcd.print("Charges:");
    char str[5];
    sprintf(str, "%.4d", getAt(CHARGE_ADDR));
    lcd.print(str);
  } else {
    menuPrinter();
    
  }

}

