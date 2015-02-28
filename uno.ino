#include <DallasTemperature.h>

#include <NewPing.h>

#include <OneWire.h>

#include <LiquidCrystal.h>

#include <EEPROM.h>


const int red = 0;
const int green = 1;
const int blue = 12;


const int leftButton = 11;
const int rightButton = 10;

const int magneticSwitch = 2;

const int freshener = A4;

const int motionSensor = A1;



// Set up distance sensor
const int trigger = A2;
const int echo    = A3;
const int maxDistance = 190;
volatile unsigned long pingTimer;
const unsigned long pingSpeed = 40;
NewPing sonar(trigger,echo,maxDistance);


// set up temperature
const int temperatureSensor = A5;
OneWire oneWire(temperatureSensor);
DallasTemperature temperature(&oneWire);


// set up light sensor
const int lightSensor = A0;
const int lightThreshold = 200;

int ledState;


int ledPin = 13;


// this value is from EEPROM
short chargesRemaining = 1000;

LiquidCrystal lcd(9,8,7,6,5,4);


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



// TODO initial state
volatile state state = NOT_IN_USE;

volatile bool inMenu = false;
volatile menu_state menuState;

const long DEBOUNCE = 40;



#define DEFAULT_CHARGES_VALUE 2000
const unsigned short chargeAddr = 5;

#define TRIGGER_DELAY_INCREMENT 5000
#define MAX_TRIGGER_DELAY       60000
const unsigned short triggerDelayAddr = chargeAddr + 2;


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
void resetCharges() {
  setAt(chargeAddr,DEFAULT_CHARGES_VALUE);
}
void resetTriggerDelay() {
  setAt(triggerDelayAddr, TRIGGER_DELAY_INCREMENT);
}



// Sets the RGB led to a specific color
void setStatusColor(int r, int g, int b) {
  digitalWrite(red, !r);
  digitalWrite(green, !g);
  digitalWrite(blue, !b);
}

void setup() {

  lcd.begin(16,2);

  pinMode(A4, OUTPUT);
  pinMode(2, INPUT_PULLUP);
  pinMode(3, INPUT_PULLUP);
  pinMode(10, INPUT_PULLUP);
  pinMode(motionSensor,INPUT);

  pinMode (red, OUTPUT);
  pinMode (green, OUTPUT);
  pinMode (blue, OUTPUT);
  pinMode(ledPin, OUTPUT);
  setStatusColor(0, 0, 0);

  // set up spray and magnetic switch interrupts
  attachInterrupt(1, spray_isr, FALLING);
  attachInterrupt(2, magneticSwitch_isr, FALLING);
  
  
  pingTimer = millis();

  setAt(triggerDelayAddr, 5000);
  
}

// We proceed to define the senses that our freshener has. 
// These values will be updated by sensors. some of them asynchronously
// hence the volatile annonations.
volatile bool doorClosed;
bool          lightsOn;
volatile bool personPresent;
bool          flushing;



//unsigned long triggerDelay = 5000; // from EEPROM;

unsigned long inBetweenTriggersDelay = 200;

volatile unsigned long triggerTime;
volatile unsigned long inBetweenTriggersTime;



unsigned long triggeringTime = 0;

// TODO better nam
unsigned long  onTime = 3000;
bool triggerTwice = false;

void triggering() {

  setStatusColor(0, 0, 0);
  digitalWrite(freshener, HIGH);


  if (millis() - triggeringTime > onTime) {
    decrementAt(chargeAddr);
    digitalWrite(freshener, LOW);
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
  if (millis() - inBetweenTriggersTime > inBetweenTriggersDelay) {
    state = TRIGGERING;
    triggerTwice = false;
    triggeringTime = millis();
  }
}

void triggered() {

  setStatusColor(0, 1, 0);
  if (millis() - triggerTime > (unsigned short) getAt(triggerDelayAddr)) {
    state = TRIGGERING;
    triggeringTime = millis();
  }
}




// called when the door is closed
void magneticSwitch_isr() {
  static volatile unsigned long lastDebounceTime;
  static long debounceDelay = 50;

  if ((millis() - lastDebounceTime) > debounceDelay) {
    
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
  static long debounceDelay = 50;

  if ((millis() - lastDebounceTime) > debounceDelay) {
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
  static long debounceDelay = 100;
  static unsigned long lastDebounceTime;

  int reading = digitalRead(leftButton);


  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {

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
              setAt(chargeAddr, DEFAULT_CHARGES_VALUE);
              break;
            case SET_DELAY:
              setAt(triggerDelayAddr,TRIGGER_DELAY_INCREMENT+
                   (getAt(triggerDelayAddr))
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

  int reading = digitalRead(rightButton);

  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {

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


// Code for the motion sensor. It senses if we're flushing.
void handleMotionSensor() {
 
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
    pingTimer += pingSpeed;
    sonar.ping_timer(echo_isr);
  }
}




unsigned long maxUseUnknownTime = 5000;
unsigned long useUnknownTimer;
void useUnknown() {
  setStatusColor(1,1,0);
  if ((millis() - useUnknownTimer) > maxUseUnknownTime) {
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
      sprintf(str, "Delay: %.5u   ", getAt(triggerDelayAddr));
      lcd.print(str);
      lcd.setCursor(0,1);
      lcd.print("Increase    ");
      break;
    case RESET_CHARGES:
      sprintf(str, "Charges: %.4d", getAt(chargeAddr));
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
    sprintf(str, "%.4d", getAt(chargeAddr));
    lcd.print(str);
  } else {
    menuPrinter();
    
  }

}

