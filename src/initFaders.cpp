//Fader init

#include "FaderControl.h"



//================================
// FADER INITIALIZATION
//================================

void initializeFaders() {
  // Initialize struct array fields
  for (int i = 0; i < NUM_FADERS; i++) {
    faders[i].analogPin = ANALOG_PINS[i];
    faders[i].pwmPin = PWM_PINS[i];
    faders[i].dirPin1 = DIR_PINS1[i];
    faders[i].dirPin2 = DIR_PINS2[i];
    faders[i].minVal = 0;
    faders[i].maxVal = 1023;
    faders[i].setpoint = 0;
    faders[i].current = 0;
    faders[i].smoothedPosition = 0;  //remove for testing and to make it more simple already getting average readings
    faders[i].motorOutput = 0;
    faders[i].lastMotorOutput = 0;
    faders[i].pidController = nullptr;  //remove for testing and to make it more simple already getting average readings
    faders[i].state = FADER_IDLE;
    faders[i].lastReportedValue = -1;  //remove for testing and to make it more simple already getting average readings
    faders[i].lastMoveTime = 0;
    faders[i].lastOscSendTime = 0;
    faders[i].suppressOSCOut = false;
    faders[i].oscID = OSC_IDS[i];
    
    // Initialize filter
    faders[i].readIndex = 0;
    faders[i].readingsTotal = 0;
    for (int j = 0; j < FILTER_SIZE; j++) {
      faders[i].readings[j] = 0;
    }
    
    faders[i].lastSentOscValue = -1;
    
    // Initialize color
    faders[i].red = baseBrightness;
    faders[i].green = baseBrightness;
    faders[i].blue = baseBrightness;
    faders[i].colorUpdated = true;

    // Initialize touch timing values
    faders[i].touched = false;
    faders[i].touchStartTime = 0;
    faders[i].touchDuration = 0;
    faders[i].releaseTime = 0;

    // Initialize brightness values
    faders[i].currentBrightness = baseBrightness;
    faders[i].targetBrightness = baseBrightness;
    faders[i].brightnessStartTime = 0;
    faders[i].lastReportedBrightness = 0;
  
  }
}

void configureFaderPins() {
  // Configure pins for each fader
  
  analogReadAveraging(16);  // Sets the ADC to always average 16 samples

  for (int i = 0; i < NUM_FADERS; i++) {
    Fader& f = faders[i];
    pinMode(f.pwmPin, OUTPUT);
    pinMode(f.dirPin1, OUTPUT);
    pinMode(f.dirPin2, OUTPUT);
    
    // Initialize readings with current value
    int initialReading = analogRead(f.analogPin);
    f.current = initialReading;
    f.smoothedPosition = initialReading;
    f.setpoint = initialReading;
    
    // Initialize moving average filter
    for (int j = 0; j < FILTER_SIZE; j++) {
      f.readings[j] = initialReading;
    }
    f.readingsTotal = initialReading * FILTER_SIZE;
    
    // Setup PID controller with loaded configuration
    f.pidController = new PID(&f.smoothedPosition, &f.motorOutput, &f.setpoint, 
                            Fconfig.pidKp, Fconfig.pidKi, Fconfig.pidKd, DIRECT);
    f.pidController->SetMode(AUTOMATIC);
    f.pidController->SetSampleTime(PID_SAMPLE_TIME);
    f.pidController->SetOutputLimits(-Fconfig.defaultPwm, Fconfig.defaultPwm);
    
    // Initialize state
    f.state = FADER_IDLE;
    f.touched = false;
  }
}