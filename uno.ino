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


#define MAGNETIC_SWITCH_PIN      A1
#define MAGNETIC_SWITCH_DEBOUNCE 50

// Freshener
#define FRESHENER_PIN           A4
// this is how long the freshener gets power when spraying
#define FRESHENER_ON_TIME       3000
// how much time there must be between triggers to fool the
// device in spraying twice
#define INBETWEEN_TRIGGERS_TIME 400

// motion sensor
#define MOTION_SENSOR_INT 0

// distance sensor
#define DISTANCE_TRIGGER_PIN A2
#define DISTANCE_ECHO_PIN    A3
#define MAX_DISTANCE         190
#define DISTANCE_PING_SPEED  40


// light sensor
#define LIGHT_SENSOR_PIN       A0
#define LIGHT_SENSOR_THRESHOLD 666   // \m/

// temperature sensor
#define TEMP_SENSOR_PIN A5



// EEPROM Stuff

#define CHARGE_ADDR             5
#define DEFAULT_CHARGES_VALUE   2000

#define TRIGGER_DELAY_ADDR      7
#define DEFAULT_TRIGGER_DELAY   5000
#define TRIGGER_DELAY_INCREMENT 5000
#define MAX_TRIGGER_DELAY       60000




//// CONFIGURATION

//  this should be tweaked. Test in field! :)

/// after this expires we go to USE_POO
#define MAX_PEE_TIME         30000

// The maximum shit time. After this we spray 2 times.  This timer is never
// reached if someone flushes after pooping or peeing.
// This timer could also be reached if you forget to flush after peeing.
// which I think is a good case to spray twice as pee smells.
// in version 2.0 we could automatically flush the toilet after this time.
// that'd be cool.
#define MAX_POO_TIME         1800000 // 30 mins


// this is a failsafe. We probably can successfully detect when someone
// finishes cleaning. but just in case we return to LEAVING if this timer
// expires.
#define MAX_CLEAN_TIME       1800000
// the time the system has to decide what's going on. After this time
// it returns to NOT_IN_USE
#define MAX_USE_UNKNOWN_TIME 5000


// stabiizing time. After a use (be it pee poo or clean) the system
// has 30 seconds time to stabilize. During this time you can still
// spray and activate the menu (non-blocking). But no sensor readings
// are futher processed.
#define MAX_AFTER_LEAVING_TIME 30000

// States for the state machine
enum state {
  NOT_IN_USE,
  USE_UNKNOWN,
  USE_PEE,
  USE_POO,
  USE_CLEAN,
  LEAVING,
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
void setupInterrupts() {
  pinMode(SPRAY_BUTTON_INT+2, INPUT_PULLUP);
  pinMode(MOTION_SENSOR_INT+2, INPUT);
  attachInterrupt(SPRAY_BUTTON_INT, spray_isr, RISING);
  attachInterrupt(MOTION_SENSOR_INT, motionSensor_isr, RISING);
}

void setup() {

  lcd.begin(16,2);


  pinMode(13,OUTPUT);
  pinMode(FRESHENER_PIN, OUTPUT);

  pinMode(MAGNETIC_SWITCH_PIN,INPUT_PULLUP);


  // distance sensor timer 
  pingTimer = millis();

  // used for the initial EEPROM flash
  #ifdef FACTORY_DEFAULT
  setAt(CHARGE_ADDR, DEFAULT_CHARGES_VALUE);
  setAt(TRIGGER_DELAY_ADDR, DEFAULT_TRIGGER_DELAY);
  #endif

  pinMode(MOTION_SENSOR_INT+2,INPUT);
  digitalWrite(MOTION_SENSOR_INT+2, LOW);
  // callibrate PIR. takes 60 seconds
  lcd.print("Calibrating...");
  for (int i = 5; i != 0; i--)
  {
    lcd.setCursor(0,1);
    lcd.print(i);
    delay(1000);
  }
  setupButtons();
  setupInterrupts();
  setupStatusLeds();
  //setStatusColor(0, 0, 0);

}

// We proceed to define the senses that our freshener has. 
// These values will be updated by sensors. some of them asynchronously
// hence the volatile annonations.
volatile bool doorOpen = false;
bool          lightsOn;
volatile bool personPresent;
volatile bool flushing;



//unsigned long triggerDelay = 5000; // from EEPROM;


volatile unsigned long triggerTime;
volatile unsigned long inBetweenTriggersTime;



unsigned long triggeringTime = 0;

bool triggerTwice = false;

unsigned long leavingTime;

void triggering() {

  //setStatusColor(0, 0, 0);
  digitalWrite(FRESHENER_PIN, HIGH);


  if (millis() - triggeringTime > FRESHENER_ON_TIME) {
    decrementAt(CHARGE_ADDR);
    digitalWrite(FRESHENER_PIN, LOW);
    if (!triggerTwice) {
       
      state = LEAVING;
      leavingTime = millis();

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
  //setStatusColor(0, 1, 0);
  if (millis() - triggerTime > (unsigned short) getAt(TRIGGER_DELAY_ADDR)) {
    state = TRIGGERING;
    triggeringTime = millis();
  }
}




void motionSensor_isr() {
  personPresent = true;
}
// In this section we handle the three pushbuttons. 
// one is used to trigger a spray.
// the other two (left, and right) are used to control the operator menu.


// the interrupt service routine for when the spray button is pressed
void spray_isr() {
  static volatile unsigned long lastDebounceTime;
  if ((millis() - lastDebounceTime) > SPRAY_BUTTON_DEBOUNCE) {
    digitalWrite(FRESHENER_PIN, LOW);
    state = TRIGGERED_TWICE;
    triggerTime = lastDebounceTime = millis();

    digitalWrite(13,HIGH);
  }
}


// to get inside the menu. either of the buttons has to be pressed.
// once in the menu. the left button is the "perform action" button and the
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
              // cycle between 5 to 60 secs in 5 sec increments
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

void handleMagneticSwitch() {
  static int lastButtonState = HIGH;
  static int buttonState;
  static unsigned long lastDebounceTime;

  int reading = digitalRead(MAGNETIC_SWITCH_PIN);

  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > MAGNETIC_SWITCH_DEBOUNCE) {

    if (reading != buttonState) {
      buttonState = reading;


      if (buttonState == LOW) {
        flushing = false;
      } else {
        flushing = true;
      }
    }
  }
  lastButtonState = reading;
}

// This button is used to cycle through menus
void handleRightButton() {
  static int lastButtonState = HIGH;
  static int buttonState;
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


void handleLightSensor() {
  lightsOn = analogRead(LIGHT_SENSOR_PIN) > LIGHT_SENSOR_THRESHOLD;
}


// Code for the distance sensor . It senses if a person is present
void echo_isr() {
  if (sonar.check_timer()) {
    int cm = sonar.ping_result / US_ROUNDTRIP_CM;
    doorOpen = cm > 10;
//    personPresent = cm < 70;
  }
}


void handleDistanceSensor() {
  if (millis() - pingTimer > DISTANCE_PING_SPEED) {
    pingTimer = 0;
    sonar.ping_timer(echo_isr);
  }
}


unsigned long useUnknownTime;

unsigned long useTime;

void useUnknown() {
  if ((millis() - useUnknownTime) < MAX_USE_UNKNOWN_TIME && lightsOn) {
    if (!doorOpen) { 
      state = USE_PEE;
      useTime = millis();
    } else {
    }
  } else {
    if (doorOpen) {
      state = USE_CLEAN;
      useTime = millis();
    } else {
      state = NOT_IN_USE;
    }
  }
}

void notInUse() {
  if (personPresent) {
    state = USE_UNKNOWN;
    useUnknownTime = millis();
  }
}


void usePee() {
  if (flushing) {
    state = TRIGGERED_ONCE;
    triggerTime = millis();
  } else {
    // if we exceeded pee time
    if ((millis() - useTime) > MAX_PEE_TIME) {
      // we are probably taking a shit
      state = USE_POO;
    }
  }
}

// FIXME. setting timers manually is error-prone. make helper method
// TODO We could get stuck in this state if people forget to flush.
// after a certain timeout we trigger anyway
void usePoo() {
  if (flushing || ((millis() - useTime) < MAX_POO_TIME)) {
    state = TRIGGERED_TWICE;
    triggerTime = millis();
  }
}


void useClean() {
  
  if ((!lightsOn && !doorOpen) || ((useTime - millis()) > MAX_CLEAN_TIME))
  {
    state = LEAVING;
    leavingTime = millis();
  }
}


// We added this state to stop the following scenario:
// Say we just took a shit or a pee. Or we just cleaned.
// We have been signaled that this state ended. Either by flushing (poo,pee)
// or by closing the door (cleaning).  We should give some time to let the
// system stabilize. Otherwise it might detect the person leaving as a new
// action and it would get into a state unwillingly.
// basically we're spinlocking. Though an interrupt could still bring us into
// the spraying state. (as the spray button is connected through an interrupt)

// 
void leaving() {
  if ((millis() - leavingTime) > MAX_AFTER_LEAVING_TIME) {
     state = NOT_IN_USE;
  }
}


// we tried giving every state a color but we ran out of colors.
// we decided only to give the states that were in the original FSM a color
void stateMachine() {
  switch (state) {
    case NOT_IN_USE: 
      setStatusColor(0,0,0);
      notInUse(); break;
    case USE_UNKNOWN:
      setStatusColor(1,0,1);
      useUnknown();
      break;
    case USE_PEE:
      setStatusColor(1,1,0);
      usePee(); break;
    case USE_POO:
      setStatusColor(1,0,0);
      usePoo(); break;
    case USE_CLEAN:
      setStatusColor(0,0,1);
      useClean(); break;
    case LEAVING:
      setStatusColor(0,0,0);
      leaving(); break;
    case TRIGGERED_ONCE:
      setStatusColor(0,1,0);
      triggerTwice = false;
      triggered();
      break;
    case TRIGGERED_TWICE:
      setStatusColor(0,1,1);
      triggerTwice = true; triggered();
      break;
    case TRIGGERING:
      setStatusColor(0,0,0);
      triggering();
      break;
    case IN_BETWEEN_TRIGGERS:
      setStatusColor(0,0,0);
      inBetweenTriggers();
      break;
    case IN_MENU:
      setStatusColor(1,1,1);
      break;
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

  handleLeftButton();
  handleRightButton();


  if (!inMenu) {
    handleMagneticSwitch();
    handleDistanceSensor();
    handleLightSensor();
    
    stateMachine();
    
    lcd.setCursor(0,0);
    temperature.requestTemperatures();
    lcd.print(temperature.getTempCByIndex(0));
    lcd.print((char)223);
    lcd.print("C       ");
    
    
    lcd.setCursor(0,1);
    lcd.print("Charges:");
    char str[5];
    sprintf(str, "%.4d", getAt(CHARGE_ADDR));
    lcd.print(str);
  } else {
    setStatusColor(1,1,1);
    menuPrinter();
    
  }

}

