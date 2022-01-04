#ifndef HEADER_FREERTOS_ALARM
#define HEADER_FREERTOS_ALARM

#include <dummy.h>

#include "Secrets.h"
#define BLYNK_TEMPLATE_ID SECRET_BLYNK_TEMPLATE_ID
#define BLYNK_DEVICE_NAME SECRET_BLYNK_DEVICE_NAME
#define BLYNK_AUTH_TOKEN SECRET_BLYNK_AUTH_TOKEN
#define BLYNK_PRINT Serial

#include <esp_task_wdt.h>

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <ESP32Servo.h>
#include <Keypad.h>
#include "freertos/task.h"

// Risorse per la stampa degli high water mark
//#include <stdio.h>
#include <stdlib.h>


// Uncomment this line to print tasks' stack high water mark info periodically
// #define PRINT_STACK_HWM

#ifdef PRINT_STACK_HWM
extern UBaseType_t stackStamp;
extern UBaseType_t stackPIN;
extern UBaseType_t stackMotion1;
extern UBaseType_t stackMotion2;
extern UBaseType_t stackWindow;
extern UBaseType_t stackSiren;
extern UBaseType_t stackServo;
extern UBaseType_t stackLED;
extern UBaseType_t stackBlynk;
#endif


extern const char auth[];

// WiFi credentials.
extern const char ssid[];
extern const char pass[];

#define INCLUDE_vTaskSuspend 1

// stati
#define ALARM_ON 19
#define ALARM_OFF 18
#define ALARM_TRIGGERED 21

// LED RGB
#define GREEN_LED 19
#define BLUE_LED 18
#define RED_LED 21

// lunghezza PIN
#define LENGTH_PIN 4
#define PIR1_PIN 25
#define PIR2_PIN 32
#define BUZZER_PIN 33
#define SERVO_PIN 14
#define WINDOW_PIN 12

// Blynk LCD positions
#define X_start 0
#define Y_first_raw 0
#define Y_second_raw 1

// Position Camera
#define POSITION_DEFAULT 90
#define POSITION_PIR1 180
#define POSITION_PIR2 0
#define POSITION_WINDOW 60

// Colors
#define MAX_COLOR_INTENSITY 255
#define MIN_COLOR_INTENSITY 0

// WIFI and Blynk connection
#define WIFI_CONNECTION_TRIES 30 // Number of tries before aborting WiFi connection
#define BLYNK_CONNECTION_TIMEOUT 10000

#define noTone 0

// CPUs
#define CPU_0 0

// Keypad
#define ROWS 4
#define COLS 4

// Setup del keypad
extern char hexaKeys[ROWS][COLS];
extern byte rowPins[ROWS]; //connect to the row pinouts of the keypad
extern byte colPins[COLS]; //connect to the column pinouts of the keypad


// inizializzazione del keypad
extern Keypad customKeypad;
extern Servo myservo;

extern char true_system_pin[LENGTH_PIN]; // password giusta del sistema
extern char user_pin[LENGTH_PIN];                      // password inserita dall'utente
extern uint8_t index_pin;                                  // indice per gestire user_pin

// Struttura dati con all'interno gli stati e i bloccati
struct gestore
{
    uint8_t stato; // stato dell'allarme = {'OFF','ON','TRIGGERED'}
    uint8_t b_sensor;
    uint8_t position;
};
extern gestore g;


// Semafori privati e mutex
extern SemaphoreHandle_t mutex;
extern SemaphoreHandle_t s_pin;
extern SemaphoreHandle_t s_stamp;
extern SemaphoreHandle_t s_sensor;
extern SemaphoreHandle_t s_siren;
extern SemaphoreHandle_t s_LED;
extern SemaphoreHandle_t s_servo;

// Task functions prototypes
void taskPin(void *pvParameters);
void taskStamp(void *pvParameters);
void taskMotionSensor(void *pvParameters);
void taskSiren(void *pvParameters);
void taskLED(void *pvParameters);
void taskWindowSensor(void *pvParameters); // task per il bottone
void taskServo(void *pvParameters);
void taskBlynk(void *pvParameters);

// Other functions prototypes
bool is_pin_valid(char *user_pin, char *true_system_pin);
void print_user_pin(void);
void get_pin(void);
void stamp(void);
void start_motion_sensor(void);
void motion_sensor(uint8_t pin_pir, uint8_t virtual_pin, char * str_code_blynk, uint8_t position_pir);
void start_window_sensor(void);
void window_sensor(void);
void start_servo(void);
void servo(void);
void siren(void);
void statusLED(void);


#endif