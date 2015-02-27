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
volatile bool person = false;
//<70 = person detected, >70 = no person detected
volatile unsigned long detectionTime = millis();
unsigned int timeOut = 4000;
volatile bool prevDistState = false;



// set up temperature
const int temperatureSensor = A5;
OneWire oneWire(temperatureSensor);
DallasTemperature temperature(&oneWire);


// set up light sensor
const int lightSensor = A0;
const int lightThreshold = 200;
bool lightOn = false;

int ledState;


int ledPin = 13;


// this value is from EEPROM
short chargesRemaining = 1000;

enum state {
  USE_UNKNOWN,
  NOT_IN_USE,
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
  LOLJEMOEDER
};



// TODO initial state
volatile state state = TRIGGERED_TWICE;


const long DEBOUNCE = 40;


LiquidCrystal lcd(9,8,7,6,5,4);


const unsigned short chargeAddr = 5;
const unsigned short triggerDelayAddr = chargeAddr + 2;


short getAt(unsigned short addr) {
  return EEPROM.read(addr)<<8 | EEPROM.read(addr+1);
}


void decrementAt(unsigned short addr) {
  setAt(addr,getAt(addr)-1);
}

void setAt(unsigned short addr, unsigned short value) {
  EEPROM.write(addr, value>>8);
  EEPROM.write(addr+1,value);
}
void resetCharges() {
  setAt(chargeAddr,2000);
}
void resetTriggerDelay() {
  setAt(triggerDelayAddr, 5000);
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
  attachInterrupt(1, spray_isr, FALLING);
  
  attachInterrupt(2, magneticSwitch_isr, FALLING);
  pingTimer = millis();
  
  state = NOT_IN_USE;

}



unsigned long triggerDelay = 5000; // from EEPROM;

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
  if (millis() - triggerTime > triggerDelay) {
    state = TRIGGERING;
    triggeringTime = millis();
  }
}



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


// called when the door is closed
void magneticSwitch_isr() {
  static volatile unsigned long lastDebounceTime;
  static long debounceDelay = 50;

  if ((millis() - lastDebounceTime) > debounceDelay) {
    
    // TODO implement gevolgen
    
    
    triggerTime = lastDebounceTime = millis();

  }

}


void exitMenu() {
  state = NOT_IN_USE;
}




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
        // TODO do stuff
        
        digitalWrite(ledPin,HIGH);
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

        digitalWrite(ledPin, LOW);
      }
    }
  }
  lastButtonState = reading;
}


void handleMotionSensor() {
  
}




void echo_isr() {
  
  if (sonar.check_timer()) {
    int cm = sonar.ping_result / US_ROUNDTRIP_CM;
    prevDistState = person;
    if (cm < 70) {
      detectionTime = millis();
      person = true;
    }
    /*
    char str[4];
    sprintf(str, "%.3d", cm);
    lcd.setCursor(0,0);
    lcd.print(str);
    */
    // TODO Echo
  }
}



void handleDistanceSensor() {
  if (millis() >= pingTimer) {
    pingTimer += pingSpeed;
    sonar.ping_timer(echo_isr);
    
  }
}




void handleLDR() {
  if (analogRead(lightSensor > lightThreshold)) {
    lightOn = true;
  } else {
    lightOn = false;
  }  
}


void setStatusColor(int r, int g, int b) {
  digitalWrite(red, !r);
  digitalWrite(green, !g);
  digitalWrite(blue, !b);
}
void notInUse() {
  if (person){
    state = USE_UNKNOWN;
  } else{
    setStatusColor(0,0,0);
  }

}

void useUnknown() {
  setStatusColor(1,0,1); 
  if (millis() - detectionTime > timeOut) {
    state = NOT_IN_USE; 
  } else {
    
  }
}

void use1() {
  setStatusColor(1,1,0);
  if (!lightOn) {
    state = USE_UNKNOWN;
  }
}



void stateMachine() {

  switch (state) {
    case USE_UNKNOWN: useUnknown(); break;
    case NOT_IN_USE: notInUse(); break;
    case USE1: use1(); break;
    case USE2: break;
    case USE_CLEAN: break;
    case TRIGGERED_ONCE: triggerTwice = false; triggered(); break;
    case TRIGGERED_TWICE: triggerTwice = true; triggered(); break;
    case TRIGGERING: triggering(); break;
    case IN_BETWEEN_TRIGGERS: inBetweenTriggers(); break;
    case IN_MENU: break;
  }
}

void loop() {
  // put your main code here, to run repeatedly:


  if (true) {
    handleLeftButton();
    handleRightButton();
  
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
  }

}

