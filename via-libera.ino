#include <IRremote.hpp>
#include "SR04.h"
#include <Servo.h>
#include <TaskScheduler.h>

#define HALL_PIN 2
#define IR_PIN 3
#define TRIG_PIN 4
#define ECHO_PIN 5
#define SERVO_PIN 6
#define RED_LED 10
#define YELLOW_LED 11
#define GREEN_LED 12

#define IR_CODE_PLAY 0xBF40FF00
#define IR_CODE_0    0xE916FF00
#define IR_CODE_1    0xF30CFF00
#define IR_CODE_2    0xE718FF00
#define IR_CODE_3    0xA15EFF00
#define IR_CODE_4    0xF708FF00
#define IR_CODE_5    0xE31CFF00
#define IR_CODE_6    0xA55AFF00
#define IR_CODE_7    0xBD42FF00
#define IR_CODE_8    0xAD52FF00
#define IR_CODE_9    0xB54AFF00

Servo gateServo;
const int SERVO_MIN = 60;
const int SERVO_MAX = 175;
const unsigned long SERVO_MOVE_INTERVAL = 20;

int currentServoPos = SERVO_MIN;
unsigned long servoLastMoveTime = 0;
unsigned long closingStartTime = 0;
const unsigned long CLOSING_TIMEOUT = 10000;

SR04 sr04 = SR04(ECHO_PIN, TRIG_PIN);
int currentDistance = 999;

bool vehicleDetected = false;
bool isGateLevel = false;

IRrecv irrecv(IR_PIN);

int correctCode[4] = {1, 2, 3, 4};
int inputCode[4];
int inputCodeIndex = 0;

long AUTHORIZATION_TIMEOUT = 60000;
long authorizationTimeoutStart;

long irLastReadTime = 0;
const long IR_DEBOUNCE_TIME = 100;

enum GateStatus {
  IDLE,
  VEHICLE_WAITING,
  AUTHORIZED,
  OPENING,
  OPEN,
  CLOSING,
  ERROR
};

GateStatus currentStatus = IDLE;

void handleStatuses();
void readSensors();

Scheduler ts;

Task t_FSM (
    50,               // freq
    TASK_FOREVER,     // span temporale
    &handleStatuses,  // attività
    &ts,              // aggancio
    true              // abilitata
);

Task t_Sensors (
    100,
    TASK_FOREVER,
    &readSensors,
    &ts,
    true
);

void setup() {
  Serial.begin(9600);

  pinMode(GREEN_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);

  digitalWrite(RED_LED, HIGH);

  pinMode(HALL_PIN, INPUT);

  currentStatus = IDLE;

  gateServo.attach(SERVO_PIN);
  gateServo.write(SERVO_MIN);
  delay(500);
  gateServo.detach();

  Serial.println(F("... lasciate ogni speranza voi che entrate ..."));
}

void loop() {
  ts.execute();
}

void handleStatuses() {
  switch (currentStatus) {
    case IDLE:
      handleIdle();
      break;

    case VEHICLE_WAITING:
      handleVehicleWaiting();
      break;

    case AUTHORIZED:
      handleAuthorized();
      break;

    case OPENING:
      handleOpening();
      break;

    case OPEN:
      handleOpen();
      break;

    case CLOSING:
      handleClosing();
      break;

    case ERROR:
      handleError();
      break;
      
    default:
      Serial.println(F("ERRORE: Stato FSM non valido!"));
      currentStatus = ERROR;
      break;
  }
}

void readSensors() {
  currentDistance = sr04.Distance();
  
  if (currentDistance < 10) { 
    vehicleDetected = true;
  } else {
    vehicleDetected = false;
  }

  if (digitalRead(HALL_PIN) == LOW) {
    isGateLevel = true;
  } else {
    isGateLevel = false;
  }
}

void handleIdle() {
  digitalWrite(RED_LED, HIGH);
  digitalWrite(YELLOW_LED, LOW);
  digitalWrite(GREEN_LED, LOW);

  if (vehicleDetected) {    
    Serial.println(F("Stato: IDLE -> VEHICLE_WAITING"));
    digitalWrite(RED_LED, LOW);
    digitalWrite(YELLOW_LED, HIGH);
    
    irrecv.enableIRIn();
    Serial.println(F("Inserire codice a 4 cifre e premere PLAY..."));
    
    startAuthorizationTimer(); 
    
    currentStatus = VEHICLE_WAITING;
  }
}

void startAuthorizationTimer() {
  authorizationTimeoutStart = millis();
  inputCodeIndex = 0;
  for(int i = 0; i < 4; i++) inputCode[i] = -1;
}


void handleVehicleWaiting() {
  if (millis() - authorizationTimeoutStart > AUTHORIZATION_TIMEOUT) {
    Serial.println(F("Authorization timeout!"));
    Serial.println(F("Stato: VEHICLE_WAITING -> IDLE"));
    
    irrecv.disableIRIn();
    currentStatus = IDLE;
    return;
  }
  
  if (!vehicleDetected) {
    Serial.println(F("Vehicle moved away."));
    Serial.println(F("Stato: VEHICLE_WAITING -> IDLE"));
    
    irrecv.disableIRIn();
    currentStatus = IDLE;
    return;
  }

  if (irrecv.decode()) {
    if (millis() - irLastReadTime > IR_DEBOUNCE_TIME) {
      irLastReadTime = millis();
      
      uint32_t code = irrecv.decodedIRData.decodedRawData;
      int digit = -1;

      if(code == IR_CODE_0) digit = 0;
      else if(code == IR_CODE_1) digit = 1;
      else if(code == IR_CODE_2) digit = 2;
      else if(code == IR_CODE_3) digit = 3;
      else if(code == IR_CODE_4) digit = 4;
      else if(code == IR_CODE_5) digit = 5;
      else if(code == IR_CODE_6) digit = 6;
      else if(code == IR_CODE_7) digit = 7;
      else if(code == IR_CODE_8) digit = 8;
      else if(code == IR_CODE_9) digit = 9;
      
      if (digit != -1 && inputCodeIndex < 4) {
        inputCode[inputCodeIndex] = digit;
        inputCodeIndex++;
        Serial.print(digit);
        
      } else if (code == IR_CODE_PLAY) {
        if (inputCodeIndex != 4) {
          Serial.println(F("\nCodice incompleto! Riprova."));
          startAuthorizationTimer();
        } else {
          bool correct = true;
          Serial.print(F("\nCodice inserito: "));
          for(int i = 0; i < 4; i++) {
            Serial.print(inputCode[i]);
            if(inputCode[i] != correctCode[i]) {
              correct = false;
            }
          }
          
          if (correct) {
            Serial.println(F("\nAutorizzazione OK!"));
            Serial.println(F("Stato: VEHICLE_WAITING -> AUTHORIZED"));
            
            irrecv.disableIRIn();
            currentStatus = AUTHORIZED;
            
          } else {
            Serial.println(F("\nCodice errato! Riprova."));
            startAuthorizationTimer();
          }
        }
      }
      
      irrecv.resume();
    }
  }
}

void handleAuthorized() {
  Serial.println(F("Stato: AUTHORIZED -> OPENING"));
  
  digitalWrite(YELLOW_LED, LOW);
  digitalWrite(GREEN_LED, HIGH);
  
  gateServo.attach(SERVO_PIN);
  servoLastMoveTime = millis();
  
  currentStatus = OPENING;
}

void handleOpening() {
  if (millis() - servoLastMoveTime > SERVO_MOVE_INTERVAL) {
    servoLastMoveTime = millis();
    
    if (currentServoPos < SERVO_MAX) {
      currentServoPos++; 
      gateServo.write(currentServoPos);
    }
  }
  
  if (currentServoPos >= SERVO_MAX) {
    Serial.println(F("Cancello aperto!"));
    Serial.println(F("Stato: OPENING -> OPEN"));
    
    digitalWrite(GREEN_LED, HIGH);
    gateServo.detach();
    
    currentStatus = OPEN;
  }
}

void handleOpen() {
  digitalWrite(GREEN_LED, HIGH);
  digitalWrite(RED_LED, LOW);
  
  if (!vehicleDetected) {    
    Serial.println(F("Veicolo passato, inizio chiusura."));
    Serial.println(F("Stato: OPEN -> CLOSING"));
    
    digitalWrite(GREEN_LED, LOW);
    digitalWrite(RED_LED, HIGH);
    
    // while(Serial.available()) Serial.read(); 
    // Serial.println(F("Premi un tasto quando la sbarra è chiusa."));
    
    gateServo.attach(SERVO_PIN);
    servoLastMoveTime = millis();
    closingStartTime = millis();
    
    currentStatus = CLOSING;
  }
}

void handleClosing() {
  if (vehicleDetected) {
    Serial.println(F("!!! OSTACOLO RILEVATO DURANTE LA CHIUSURA !!!"));
    Serial.println(F("Stato: CLOSING -> OPENING"));
    
    digitalWrite(GREEN_LED, HIGH);
    digitalWrite(RED_LED, LOW);
    
    servoLastMoveTime = millis(); 
    
    currentStatus = OPENING; 
    
    return; 
  }

  if (isGateLevel) {
    Serial.println(F("Finecorsa rilevato! Chiusura completata."));
    Serial.println(F("Stato: CLOSING -> IDLE"));
    
    gateServo.detach();
    digitalWrite(RED_LED, HIGH);

    currentStatus = IDLE;
    return;
  }

  if (millis() - servoLastMoveTime > SERVO_MOVE_INTERVAL) {
    servoLastMoveTime = millis();
    
    if (currentServoPos > SERVO_MIN) {
      currentServoPos--;
      gateServo.write(currentServoPos);
    }
  }
  
  if (currentServoPos <= SERVO_MIN) {
    Serial.println(F("ERRORE: Finecorsa non rilevato!"));
    Serial.println(F("Stato: CLOSING -> ERROR"));
    
    gateServo.detach();
    currentStatus = ERROR;
    return;
  }
  
  if (millis() - closingStartTime > CLOSING_TIMEOUT) {
    Serial.println(F("ERRORE: Timeout chiusura! Il cancello potrebbe essere bloccato."));
    Serial.println(F("Stato: CLOSING -> ERROR"));
    
    gateServo.detach();
    currentStatus = ERROR;
    return;
  }
}

void handleError() {
  bool ledState = (millis() / 250) % 2;
  
  digitalWrite(RED_LED, ledState);
  digitalWrite(YELLOW_LED, ledState);
  digitalWrite(GREEN_LED, ledState);
  
  // nessun return perché andrà fatto un reset manuale
}
