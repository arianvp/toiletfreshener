#include <OneWire.h>

#include <LiquidCrystal.h>

#include <EEPROM.h>


const int red = 0;
const int green = 1;
const int blue = 12;

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


volatile state state;


const long DEBOUNCE = 40;


enum button_state
{
  DEBOUNCED,
  UNSTABLE,
  CHANGED
};
struct button
{
  long pin;
  long last_debounce_time;
  int  last_button_state;
  int  button_state;
  long  debounce;
  
  
};



button flush_switch = {2,0,HIGH,LOW, DEBOUNCE};
button spray_button = {3,0,HIGH,LOW, DEBOUNCE};

button opt1_button  = {11,0,HIGH,LOW, DEBOUNCE};
button opt2_button  = {10,0,HIGH,LOW, DEBOUNCE};


// handled by external interrupts
button *isr_buttons[2] = {
  &flush_switch,
  &spray_button,
};

// handled by polling
button *buttons[2] = { &opt1_button, &opt2_button };


int ledState = 0;

void setup() {
  // put your setup code here, to run once:
  
  pinMode(A4,OUTPUT);
  pinMode(2,INPUT_PULLUP);
  pinMode(3,INPUT_PULLUP);
  
  pinMode (red, OUTPUT);
  pinMode (green, OUTPUT);
  pinMode (blue, OUTPUT);
  
  setStatusColor(0,0,0);
  attachInterrupt(1,spray_isr,FALLING);
  
}



unsigned long triggerDelay = 5000; // from EEPROM;

unsigned long inBetweenTriggersDelay = 200;

volatile unsigned long triggerTime;
volatile unsigned long inBetweenTriggersTime;



unsigned long triggeringTime = 0;

unsigned long  onTime = 4000;


bool triggerTwice = false;

void triggering() {

  
  setStatusColor(0,0,0);
  digitalWrite(A4,HIGH);
  
  
  
  if (millis() - triggeringTime > onTime) {
    digitalWrite(A4,LOW);
    if (!triggerTwice){
      state = NOT_IN_USE;
    } else{
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
 
  setStatusColor(0,1,0);
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
      state = TRIGGERED_TWICE;
      triggerTime = lastDebounceTime = millis();
      
  }
  
} 

void exitMenu() {
  state = NOT_IN_USE;
}




void handleOpt1Press() {
  static int lastButtonState = LOW;
  static int buttonState;
  static long debounceDelay = 50;
  static unsigned long lastDebounceTime;
  
  // read the state of the switch into a local variable:
  int reading = digitalRead(10);

  // check to see if you just pressed the button 
  // (i.e. the input went from LOW to HIGH),  and you've waited 
  // long enough since the last press to ignore any noise:  

  // If the switch changed, due to noise or pressing:
  if (reading != lastButtonState) {
    // reset the debouncing timer
    lastDebounceTime = millis();
  } 
  
  if ((millis() - lastDebounceTime) > debounceDelay) {
    // whatever the reading is at, it's been there for longer
    // than the debounce delay, so take it as the actual current state:

    // if the button state has changed:
    if (reading != buttonState) {
      buttonState = reading;

      // only toggle the LED if the new button state is HIGH
      if (buttonState == HIGH) {
        ledState = !ledState;
      }
    }
  }
  // save the reading.  Next time through the loop,
  // it'll be the lastButtonState:
  lastButtonState = reading;
}



void processSensors() {
}


void setStatusColor(int r, int g, int b) {
  digitalWrite(red, !r);
  digitalWrite(green, !g);
  digitalWrite(blue, !b);
}
void notInUse() {
  
}


void stateMachine() {
 
  switch (state) {
  case USE_UNKNOWN: break;
  case NOT_IN_USE: notInUse(); break;
  case USE1: break;
  case USE2: break;
  case USE_CLEAN: break;
  case TRIGGERED_ONCE: triggered(); break;
  case TRIGGERED_TWICE: triggerTwice = true; triggered(); break;
  case TRIGGERING: triggering(); break;
  case IN_BETWEEN_TRIGGERS: inBetweenTriggers(); break;
  case IN_MENU: break;
  }
}

void loop() {
  // put your main code here, to run repeatedly:

  
  handleOpt1Press();
  stateMachine();

}

