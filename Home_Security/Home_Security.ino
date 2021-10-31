//www.elegoo.com
//2016.12.9

/* @file CustomKeypad.pde
|| @version 1.0
|| @author Alexander Brevig
|| @contact alexanderbrevig@gmail.com
||
|| @description
|| | Demonstrates changing the keypad size and key values.
|| #
*/
#include <Keypad.h>
#include <LiquidCrystal.h>
#include "pitches.h"
// initialize the library with the numbers of the interface pins
LiquidCrystal lcd(7, 8, 9, 10, 11, 12);

#define CICALINO 29
#define LENGTH_PIN 4
 // STATI
#define ALARM_OFF 0
#define ALARM_ON 1
#define ALARM_TRIGGERED 2

int ledPin = 13;  // LED on Pin 13 of Arduino
int pirPin = 5; // Input for HC-S501

int movement_sensor_value; // Place to store read PIR Value


int STATO;
char user_pin [LENGTH_PIN];
int index_pin = 0; // per scorrere array pin
char true_system_pin [4] = {'0','0','0','0'};
const byte ROWS = 4; //four rows
const byte COLS = 4; //four columns
//define the cymbols on the buttons of the keypads
char hexaKeys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {39, 41, 43, 45}; //connect to the row pinouts of the keypad
byte colPins[COLS] = {47, 49, 51, 53}; //connect to the column pinouts of the keypad

//initialize an instance of class NewKeypad
Keypad customKeypad = Keypad( makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS); 

bool is_pin_valid(char * user_pin , char * true_system_pin){
  for (int i=0; i<LENGTH_PIN; i++){
    if(user_pin[i]!=true_system_pin[i]){
      return false;} // appena trova un elemento diverso ritorna false senza controllare gli altri
    }
    return true;
  }

void check_alarm_triggered(){
  if (STATO==ALARM_TRIGGERED){
    tone(CICALINO, 500); // suona
  }
  else noTone(CICALINO);
}

void setup(){
  STATO=ALARM_OFF;
  // sensore di movimento
  pinMode(ledPin, OUTPUT);
  pinMode(pirPin, INPUT);
  digitalWrite(ledPin, LOW);
  
  // LCD
  Serial.begin(9600); // probabilmente questa linea si può togliere
  lcd.begin(16, 2);
  lcd.print("Alarm OFF");
  lcd.setCursor(0, 1);
  lcd.print("PIN: ");
 
}

void change_alarm_state(){
 lcd.setCursor(5+index_pin, 1);
  char customKey = customKeypad.getKey();
  if (customKey){ // se ho preso input
    lcd.print(customKey);
    user_pin[index_pin] = customKey;
    index_pin++; //aggiorno index_pin
    if (index_pin == 4){
      bool valid_pin = is_pin_valid(user_pin, true_system_pin);
      if (valid_pin){
        Serial.print("Il pin inserito è GIUSTO ");
        Serial.println(user_pin);
        lcd.setCursor(0, 0);
        if (STATO==ALARM_OFF){
          lcd.print("Alarm ON ");
          STATO=ALARM_ON;
        }
        else if (STATO==ALARM_ON || STATO==ALARM_TRIGGERED){
          lcd.print("Alarm OFF ");
          STATO=ALARM_OFF;
          }
        lcd.setCursor(0, 1);
        lcd.print("PIN:        ");
      }
      else {
        Serial.print("Il pin inserito è SBAGLIATO ");
        Serial.println(user_pin);
        lcd.setCursor(5, 1);
        lcd.print("    ");
        }
      index_pin=0; // rinizializzo l'indice
      }
  Serial.print("Stato ");
  Serial.println(STATO);
  }
for(int i=0; i<2; i++) {
  Serial.println(i);
  }
}

bool check_movement_sensor(){
  if (STATO==ALARM_ON){
     movement_sensor_value = digitalRead(pirPin);  // legge input sensore solamente se allarme è attivo
     if (movement_sensor_value){
      digitalWrite(ledPin, movement_sensor_value);
      return true;
     }
    }
    return false;
  }
  
void loop(){
  check_alarm_triggered();
  change_alarm_state();
  bool movement = check_movement_sensor();
  if (movement){
    // cambio di stato
    STATO=ALARM_TRIGGERED;
  }
  
}
