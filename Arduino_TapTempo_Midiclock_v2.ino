//TapTempoMalte_2
//
// quantized reststart
// initial BPM
// ws2812b LEDs
// Bugfixing


const String VERSION = "1.11";
#include <SoftwareSerial.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "MIDIUSB.h"
#include <ArduinoTapTempo.h>
#include <TimerOne.h>
//#include <FastLED.h>
#include <Adafruit_NeoPixel.h>


#define NUM_LEDS 4
#define DATA_PIN 9
Adafruit_NeoPixel pixels(NUM_LEDS, DATA_PIN, NEO_GRB + NEO_KHZ800);
#define LED_ON 60
#define LED_OFF 0

//#define ENCODER_DO_NOT_USE_INTERRUPTS
#include <Encoder.h>

#define TAP_PIN 4
#define GATEPIN 5
#define TAP_PIN_POLARITY FALLING
#define MINIMUM_TAPS 3
#define EXIT_MARGIN 100 // If no tap after 150% of last tap interval -> measure and set

#define BLINK_OUTPUT_PIN 10
#define BLINK_PIN_POLARITY 0  // 0 = POSITIVE, 255 - NEGATIVE
#define BLINK_TIME 8 // How long to keep LED lit in CLOCK counts (so range is [0,24])

#define START_STOP_INPUT_PIN 16

#define NUDGE_MINUS 6
#define NUDGE_PLUS 7
boolean bLastNudgePlus = false;
boolean bLastNudgeMinus = false;
#define DECODER_CLICK 8
#define STOP_BUTTON 14
//#define STOP_PIN_POLARITY 0

#define MIDI_START 0xFA
#define MIDI_STOP 0xFC
#define MIDI_TIMING_CLOCK 0xF8

#define DEBOUNCE_INTERVAL 500L // Milliseconds
#define CLOCKS_PER_BEAT 24
#define MINIMUM_BPM 400 // Used for debouncing
#define MAXIMUM_BPM 3000 // Used for debouncing

#define MODE_NORMAL 0
#define MODE_SETMEASURE 1
uint8_t iMode;

long intervalMicroSeconds;
int bpm = 940; // BPM in tenths of a BPM!!
int bpmCache = bpm; //Nudge

long minimumTapInterval = 60L * 1000 * 1000 * 10 / MAXIMUM_BPM;
long maximumTapInterval = 60L * 1000 * 1000 * 10 / MINIMUM_BPM;

volatile long firstTapTime = 0;
volatile long lastTapTime = 0;
volatile long timesTapped = 0;

volatile int blinkCount = 0;
boolean playing = false;
long lastStartStopTime = 0;

ArduinoTapTempo tapTempo;
Encoder myEnc(0, 1);
int encoder = 0;

boolean bQuantizeRestart = false;
const uint8_t MEASURE = 4; // Taktmass
uint8_t iMeasureCount = 0;

//----Display
//----SCL - 22
//----SDA - 21
#define OLED_RESET -1 // not used / nicht genutzt bei diesem Display
Adafruit_SSD1306 display(128, 64, &Wire, OLED_RESET);

SoftwareSerial mySerial(A2 /*Bogus*/ ,A3);

void setup(){

  iMode = MODE_NORMAL;
  pixels.begin();
  pixels.clear();
  pixels.setPixelColor(0, pixels.Color(LED_OFF, LED_OFF, LED_ON));
  pixels.show();
  
  mySerial.begin(31250);
  pinMode(BLINK_OUTPUT_PIN, OUTPUT);
  pinMode(GATEPIN, OUTPUT);
  // Attach the interrupt to send the MIDI clock and start the timer
  Timer1.initialize(intervalMicroSeconds);
  Timer1.setPeriod(calculateIntervalMicroSecs(bpm));
  Timer1.attachInterrupt(sendClockPulse);

  pixels.setPixelColor(1, pixels.Color(LED_OFF, LED_OFF, LED_ON));
  pixels.show();

  pinMode(NUDGE_PLUS, INPUT);
  pinMode(NUDGE_MINUS, INPUT);
  pinMode(START_STOP_INPUT_PIN, INPUT);
  pinMode(TAP_PIN, INPUT);
  pinMode(STOP_BUTTON, INPUT);
  pinMode(DECODER_CLICK, INPUT);

  pixels.setPixelColor(2, pixels.Color(LED_OFF, LED_OFF, LED_ON));
  pixels.show();

  //----Pullup
  digitalWrite(NUDGE_PLUS, HIGH);
  digitalWrite(NUDGE_MINUS, HIGH);
  digitalWrite(START_STOP_INPUT_PIN, HIGH);
  digitalWrite(TAP_PIN, HIGH);
  digitalWrite(STOP_BUTTON, HIGH);
  digitalWrite(DECODER_CLICK, HIGH);

  encoder = myEnc.read();  

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextColor(WHITE);

  pixels.setPixelColor(3, pixels.Color(LED_OFF, LED_OFF, LED_ON));
  pixels.show();
  
  displayInfo(3000);


  tapTempo.setBPM(.10*bpm);
  pixels.clear();
  pixels.show();
} 

void loop(){

  // Wenn bei STOPPED der encoderbutton lange gedrückt wird, kommen wir in ein menu
  if(!playing){
    
  }

  
  if(iMode==MODE_NORMAL){
    operationNormal();
  }

  
}//Loop

void operationNormal(){
  long now = millis();
  float tmpBPM1 = tapTempo.getBPM();
  bpm = (int)(tmpBPM1*10);
  
  boolean buttonDown = !digitalRead(TAP_PIN);
  
  tapTempo.update(buttonDown);
  float tmpBPM2 = tapTempo.getBPM();

  if(tmpBPM1 != tmpBPM2){  
    bpm = tmpBPM1;
    if(tmpBPM2>0){
      bpm = (int)(tmpBPM2*10);
    } 
    bpmCache = bpm;
    updateBpm( now );
  }


  /*
   * Check for start/stop button pressed
   */  
  boolean startStopPressed = !digitalRead(START_STOP_INPUT_PIN);
  if (startStopPressed && (lastStartStopTime + (DEBOUNCE_INTERVAL /* 500*/)) < now) {
    if(playing==false){
      iMeasureCount=0;
      restartMidi();
      ledIndicateStart();
    }else{
      bQuantizeRestart = true;
    }
 
    lastStartStopTime = now;
  }

  //----Check for Stop-Button
  boolean bStop = !digitalRead(STOP_BUTTON);
  if(bStop){
    stopMidi(); 
    iMeasureCount=0;
    ledOff();
    analogWrite(BLINK_OUTPUT_PIN, 0 + BLINK_PIN_POLARITY);
    digitalWrite(GATEPIN, LOW);
  }

  //----Check for Nudge
  boolean bNudgeMinus = !digitalRead(NUDGE_MINUS);
  if(bNudgeMinus){
    if(bLastNudgeMinus==false){
      bLastNudgeMinus=true;
      bpmCache = bpm;
      bpm = bpm*0.95;
      updateBpm( now );
    }
    
  }else{
    bLastNudgeMinus = false;
  }


  boolean bNudgePlus = !digitalRead(NUDGE_PLUS);
  if(bNudgePlus){
    if(bLastNudgePlus==false){
      bLastNudgePlus=true;
      bpmCache = bpm;
      bpm = bpm*1.025;
      updateBpm( now );
    }
  }else{
    bLastNudgePlus = false;
  }

  if((bNudgeMinus == false) && (bNudgePlus == false)){
    bpm = bpmCache;
    updateBpm( now );
  }

  //----Check rotary Encoder
  boolean bDecoderPressed = !digitalRead(DECODER_CLICK);
  
  int newEncoder =  myEnc.read();
  
  if(newEncoder > encoder){
    if(bDecoderPressed){
      bpm+=1;
    }else{
      bpm += 10;
    }
    
    bpmCache = bpm;
    updateBpm( now );
    encoder = newEncoder;
    tapTempo.setBPM(.10*bpm);
  }
  else if(newEncoder < encoder){
    if(bDecoderPressed){
      bpm-=1;
    }else{
      bpm -= 10;
    }
    
    bpmCache = bpm;
    updateBpm( now );
    encoder = newEncoder;
    tapTempo.setBPM(.10*bpm);
  } 
}



void stopMidi(){
  sendClock_Stop();
  
  blinkCount = 0;
  playing = false;
}

void restartMidi(){
  bQuantizeRestart = false;
  sendClock_Stop();
  sendClock_Start();
  
  blinkCount = 0;
  playing = true;
}

void sendClockPulse() {

  if(playing){
    sendClock();
  }

  blinkCount = (blinkCount + 1) % CLOCKS_PER_BEAT;
  if (blinkCount == 0) {
    if(iMeasureCount<MEASURE-1){
      iMeasureCount++;
      //noteOn(0x00, 0x00, iMeasureCount);
    }else{
      iMeasureCount=0;

      if(bQuantizeRestart){
        restartMidi();
      }
    }
    
    // Turn led on
#ifdef BLINK_OUTPUT_PIN
    if(playing==true){
      analogWrite(BLINK_OUTPUT_PIN, 255 - BLINK_PIN_POLARITY);
      digitalWrite(GATEPIN, HIGH);
   
      if(iMeasureCount==0){
        ledIndicateStart();
      }else{
        ledIndicateMeasure(iMeasureCount);
      }
    }
#endif
    
  } else {

#ifdef BLINK_OUTPUT_PIN
    if (blinkCount == BLINK_TIME) {
      // Turn led off
      if(playing==true){
        analogWrite(BLINK_OUTPUT_PIN, 0 + BLINK_PIN_POLARITY);
        digitalWrite(GATEPIN, LOW);

        // Blinken der '1' nur kurz, weil's irgendwie nervig ist
        //if(iMeasureCount==0){
          ledOff();
        //}
      }
    }
#endif
  }
}

void updateBpm(long now) {
  // Update the timer
  long interval = calculateIntervalMicroSecs(bpm);
  Timer1.setPeriod(interval);
  displayBPM(bpm);
}

long calculateIntervalMicroSecs(int bpm) {
  // Take care about overflows!
  return 60L * 1000 * 1000 * 10 / bpm / CLOCKS_PER_BEAT;
}



// First parameter is the event type (0x09 = note on, 0x08 = note off).
// Second parameter is note-on/note-off, combined with the channel.
// Channel can be anything between 0-15. Typically reported to the user as 1-16.
// Third parameter is the note number (48 = middle C).
// Fourth parameter is the velocity (64 = normal, 127 = fastest).

void noteOn(byte channel, byte pitch, byte velocity) {
  midiEventPacket_t noteOn = {0x09, 0x90 | channel, pitch, velocity};
  MidiUSB.sendMIDI(noteOn);
  MidiUSB.flush();
}

void noteOff(byte channel, byte pitch, byte velocity) {
  midiEventPacket_t noteOff = {0x08, 0x80 | channel, pitch, velocity};
  MidiUSB.sendMIDI(noteOff);
  MidiUSB.flush();
}

void sendClock_Start(){
  midiEventPacket_t p = {0x0F, MIDI_START, 0, 0};
  MidiUSB.sendMIDI(p);
  MidiUSB.flush();
  mySerial.write(MIDI_START);
}

void sendClock_Stop(){
  midiEventPacket_t p = {0x0F, MIDI_STOP, 0, 0};
  MidiUSB.sendMIDI(p);
  MidiUSB.flush();
  mySerial.write(MIDI_STOP);
}
void sendClock(){
  midiEventPacket_t p = {0x0F, MIDI_TIMING_CLOCK, 0, 0};
  MidiUSB.sendMIDI(p);
  MidiUSB.flush();
  mySerial.write(MIDI_TIMING_CLOCK);
}


void displayInfo(int pMiliseconds){
  
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(1, 0);
  display.println("ClockBox");
  display.setCursor(1, 20);
  display.println("v:" + VERSION);
  display.setTextSize(1);
  display.setCursor(10, 50);
  display.println("www.Andyland.info");
  display.display();
  delay(pMiliseconds);
}

void displayBPM(int pBPM){

  String sState="STOPPED";
  if(playing){
    sState="PLAYING";
  }

  if(bQuantizeRestart){
    sState=" " + String(iMeasureCount+1) + "...";
  }
  
  display.setTextSize(3);
  float f=pBPM/10.;
  display.clearDisplay();
  display.setCursor(1, 0);
  display.println(String(f,1));

  display.setCursor(1, 35);
  display.println(sState);
  display.display();
}

void displayMeasure(){

  String sState="STOPPED";
  if(playing){
    sState="PLAYING";
  }

  
  display.setTextSize(3);
  display.clearDisplay();
  display.setCursor(1, 0);
  display.println("MEASURE");

  display.setCursor(1, 35);
  display.println(String(MEASURE));
  display.display();
}


void ledIndicateStart(){
  pixels.clear();
  for(int i=0;i<NUM_LEDS; i++){
    pixels.setPixelColor(i, pixels.Color(LED_OFF, LED_ON*.25, LED_OFF));
  }
    pixels.show();
}

void ledIndicateMeasure(int pMeasure){
  pixels.clear();
  if(bQuantizeRestart){
    pixels.setPixelColor(pMeasure, pixels.Color(LED_ON, LED_OFF, LED_ON));
  }else{
    pixels.setPixelColor(pMeasure, pixels.Color(LED_OFF, LED_OFF, LED_ON));
  }
  pixels.show();
}

void ledOff(){
  pixels.clear();
  pixels.show();
}
