#include <Arduino_FreeRTOS.h>
#include "FreeRTOSConfig.h"
#include <semphr.h>
#include <Keypad.h>
#include <Servo.h>

#define INCLUDE_vTaskSuspend 1

//#define PIETRO 42 // togli se sei monta

// stati
#define ALARM_ON 2
#define ALARM_OFF 3
#define ALARM_TRIGGERED 4
#define SERVO 5

// lunghezza PIN
#define LENGTH_PIN 4
#define MOTION_SENSOR_PIN 31
#define BUZZER_PIN 29
#define SERVO_PIN 9
#define WINDOW_PIN 13 // pin del bottone 

#define MAX_COLOR_INTENSITY 255
#define MIN_COLOR_INTENSITY 0

// Setup del keypad
const byte ROWS = 4; //four rows
const byte COLS = 4; //four columns

char hexaKeys[ROWS][COLS] = { // Per il Pin
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}};
    
#ifdef PIETRO
 // PIN associati al keypad pietro
byte rowPins[ROWS] = {9, 8, 7, 6}; //connect to the row pinouts of the keypad 
byte colPins[COLS] = {5, 4, 3, 2}; //connect to the column pinouts of the keypad
#else
 // PIN associati al keypad monta
byte rowPins[ROWS] = {39, 41, 43, 45}; //connect to the row pinouts of the keypad
byte colPins[COLS] = {47, 49, 51, 53}; //connect to the column pinouts of the keypad
#endif

// inizializzazione del keypad
Keypad customKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);
Servo myservo;


char true_system_pin[4] = {'0', '0', '0', '0'}; // password giusta del sistema
char user_pin[LENGTH_PIN]; // password inserita dall'utente
int index_pin; // indice per gestire user_pin

// Struttura dati con all'interno gli stati e i bloccati
struct gestore
{
  int stato;
//   bool alarm;          // mode: DISABLED/ENABLED
//   bool siren;         // ON/OFF per far suonare l'allarme
//   bool alarm_triggered;
  int b_motion_sensor;
//   int b_siren;
//   int b_stamp
  int n_sensor;
  int position;
}g;

int movement_sensor_value; // Place to store read PIR Value
int window_sensor_value;  // store del valore del bottone (che simula una finestra) (qui quando è = 0 è stato triggerato)


// Semafori privati e mutex
SemaphoreHandle_t mutex = NULL;
SemaphoreHandle_t s_pin = NULL;
SemaphoreHandle_t s_stamp = NULL;
SemaphoreHandle_t s_motion_sensor = NULL; // questo semaforo è usato per sincronizzare due sensori
SemaphoreHandle_t s_siren = NULL;
SemaphoreHandle_t s_LED = NULL;
SemaphoreHandle_t s_servo = NULL;


void taskPin(void *pvParameters);
void taskStamp(void *pvParameters);
void taskMotionSensor(void* pvParameters);
void taskSiren(void* pvParameters);
void taskServo(void* pvParameters);
void taskLED(void* pvParameters);
void taskWindow(void* pvParameters); // task per il bottone


/*funzioni di supporto - usate (e anche non ancora usate) per printare o per controllare */
bool is_pin_valid(char *user_pin, char *true_system_pin)
{
    for (int i = 0; i < LENGTH_PIN; i++)
    {
        if (user_pin[i] != true_system_pin[i])
        {
            return false;
        } // appena trova un elemento diverso ritorna false senza controllare gli altri
    }
    return true;
}
// non usata ancora (ho avuto problemi)
void init_pin(int *index_pin, int *user_pin)
{
    *index_pin = 0;
    for (int k = 0; k < LENGTH_PIN; k++)
    {
        user_pin[k] = -1;
    }
    //Serial.println(index_pin);
}

// usata per printare solamente fino all'indice a cui siamo arrivati
void print_user_pin()
{
    for (int k = 0; k < index_pin; k++)
    {
        Serial.print(user_pin[k]);
    }
    Serial.print('\n');
}

// usate per debug, per capire quando allarme è on e quando è off
void print_alarm_state(){
    // if(g.alarm)Serial.println("Alarm On");
    // else Serial.println("Alarm Off");
}


void get_pin()
{   
    //vTaskDelay(50 / portTICK_PERIOD_MS); 
    //Serial.println("-------");
    char customKey = customKeypad.getKey();
    if(customKey){
        xSemaphoreTake(mutex, (TickType_t)100);
        user_pin[index_pin] = customKey; // possibile race conditions su shared variable (per questo usato mutex)
        index_pin++; //aggiorno index_pin
        xSemaphoreGive(mutex);
        }                     
    
}


void end_pin(void *pvParameters)
{
    xSemaphoreGive(s_stamp);
}


// Qui la stampa è fatta su seriale (potremmo mantenerlo come un task a parte(?))
// Importante perchè qui si modificano i vari stati dell'allarme, e si svegliano anche dei task (sirena)
void stamp()
{
    print_user_pin();
    //print_alarm_state();
    xSemaphoreTake(s_stamp, (TickType_t)100);

	xSemaphoreTake(mutex, (TickType_t)100);
    if (index_pin == 4)
    {
        bool valid_pin = is_pin_valid(user_pin, true_system_pin); // check del pin con "0000"

        // Switch case per essere più efficienti??
        if (valid_pin)
        {
            Serial.print("Il pin inserito è GIUSTO ");
			Serial.println(user_pin);
            // il pin è giusto, quindi bisogna cambiare lo stato dell'allarme: nel caso in cui sia off->on nel caso sia on->off
            // BISOGNERA' FARE REFACTORING, siccome non è ottimale
			if (g.stato == ALARM_ON) {
				g.stato = ALARM_OFF;              //anche qui g.alarm è una var condivisa, quindi possibili race condition
                Serial.println("Allarme spento.");
                xSemaphoreGive(s_LED);
			}
            else if (g.stato == ALARM_TRIGGERED) {
				g.stato = ALARM_OFF;
                g.position=90; // la videocamera deve riprendere il suo stato iniziale (ovvero 90 gradi: al centro)
                xSemaphoreGive(s_siren);
                xSemaphoreGive(s_LED);
                xSemaphoreGive(s_servo); // sveglio il servo in quanto si deve svegliare per applicare la rotazione alla videocamera
          	}
			else {
                g.stato = ALARM_ON;
                while (g.b_motion_sensor) { // siccome ci sono 2 sensori devo svegliarli entrambi
                	Serial.println("END_STAMP: Sveglio sensore movimento."); // potrebbe essere per questo
                    //vTaskDelay(20 / portTICK_PERIOD_MS); 
				    g.b_motion_sensor--;
                    g.n_sensor++;
				    xSemaphoreGive(s_motion_sensor);
                }
                xSemaphoreGive(s_LED);
			}
            
        }
        else
        {
            Serial.print("Il pin inserito è SBAGLIATO ");
            Serial.println(user_pin);
        }
        //init_pin(&index_pin); // rinizializzo il pin

        for (int k = 0; k > LENGTH_PIN; k++)
        {
            user_pin[k] = -1;
        }
        index_pin = 0;
    }
	//g.stato = 10;
	xSemaphoreGive(mutex);
}



void start_motion_sensor(void* pvParameters)
{
	xSemaphoreTake(mutex, portMAX_DELAY);
    //Serial.println("Sono lo start_motion_sensor");
	if (g.stato == ALARM_ON || g.stato == ALARM_TRIGGERED) //deve essere nello stato corretto
	{
		xSemaphoreGive(s_motion_sensor);
        g.n_sensor++;
		//Serial.println("Sensore di movimento parte.");
	}
	else {
        Serial.print("Stato allarme: "); Serial.println(g.stato);
		Serial.println("START_MOTION_SENSOR: Sensore di movimento si BLOCCA.");
		g.b_motion_sensor++;
	}
	xSemaphoreGive(mutex);

    xSemaphoreTake(mutex, portMAX_DELAY);
	xSemaphoreTake(s_motion_sensor, portMAX_DELAY); // mi blocco qui nel caso
    Serial.println("Sono il motion sensor e mi sono sbloccato");
    xSemaphoreGive(mutex);
}

void motion_sensor()
{   //Serial.println("sono lo motion_sensor");
    xSemaphoreTake(mutex, portMAX_DELAY);
    //Serial.println("Sono il motion sensor e mi sono sbloccato");
	movement_sensor_value = digitalRead(MOTION_SENSOR_PIN); // shared variable, uso mutex
    Serial.print("PIR value:");
    Serial.print(movement_sensor_value);
    Serial.print(" - n_sensor: ");
    Serial.println(g.n_sensor);
    xSemaphoreGive(mutex);
}

void end_motion_sensor(void* pvParameters)
{
    //vTaskDelay(50 / portTICK_PERIOD_MS); 
	xSemaphoreTake(mutex, portMAX_DELAY);
    g.n_sensor--;
    if(movement_sensor_value){
        Serial.println("-------- MOVIMENTO RILEVATO da PIR!!! -----------");
        if (g.stato==ALARM_ON){ // se c'è movimento e l'allarme è ON (e non triggered quindi)
            g.stato = ALARM_TRIGGERED;
            xSemaphoreGive(s_siren);
            xSemaphoreGive(s_LED);
            if (g.position!=180) { // se è gia in posizione 180 sta gia filmando quindi non serve svegliare videocamera
                g.position=180; // modifica la posizione
                xSemaphoreGive(s_servo);
	        }
        }
        else if (g.stato == ALARM_TRIGGERED && g.position!=180){ // sveglio il servo per spostare videocamera
            g.position=180; // modifica la posizione
            xSemaphoreGive(s_servo);
        }
    }
	xSemaphoreGive(mutex);
}

void start_window_sensor(void* pvParameters)
{
	xSemaphoreTake(mutex, portMAX_DELAY);
    //Serial.println("Sono lo start_motion_sensor");
	if (g.stato == ALARM_ON || g.stato == ALARM_TRIGGERED) //deve essere nello stato corretto
	{
		xSemaphoreGive(s_motion_sensor);
        g.n_sensor++;
		//Serial.println("Sensore finestra parte.");
	}
	else {
        Serial.print("Stato allarme: "); Serial.println(g.stato);
		Serial.println("START_WINDOW_SENSOR: Sensore finestra si BLOCCA.");
		g.b_motion_sensor++;
	}
	xSemaphoreGive(mutex);
    xSemaphoreTake(mutex, portMAX_DELAY);
	xSemaphoreTake(s_motion_sensor, portMAX_DELAY); // mi blocco qui nel caso
    Serial.println("Sono il window sensor e mi sono sbloccato");
    xSemaphoreGive(mutex);
}

void window_sensor()
{       
    xSemaphoreTake(mutex, portMAX_DELAY);
    //Serial.println("Sono il window sensor e mi sono sbloccato");
	window_sensor_value = digitalRead(WINDOW_PIN); // shared variable, uso mutex
    Serial.print("Window sensor value:");
    Serial.print(window_sensor_value);
    Serial.print(" - n_sensor: ");
    Serial.println(g.n_sensor);
    xSemaphoreGive(mutex);
}

void end_window_sensor(void* pvParameters)
{
    //vTaskDelay(50 / portTICK_PERIOD_MS); 
    xSemaphoreTake(mutex, portMAX_DELAY);
    g.n_sensor--;
    if(!window_sensor_value){
        Serial.println("-------- MOVIMENTO RILEVATO da finestra!!! -----------");
        if (g.stato==ALARM_ON){ // se c'è movimento e l'allarme è ON (e non triggered quindi)
            g.stato = ALARM_TRIGGERED;
            xSemaphoreGive(s_siren);
            xSemaphoreGive(s_LED);
            if (g.position!=0) { // se è gia in posizione 180 sta gia filmando quindi non serve svegliare videocamera
                g.position=0; // modifica la posizione
                xSemaphoreGive(s_servo);
	        }
        }
        else if (g.stato == ALARM_TRIGGERED && g.position!=0){ // sveglio il servo per spostare videocamera
            g.position=0; // modifica la posizione
            xSemaphoreGive(s_servo);
        }
    }
	xSemaphoreGive(mutex);
}

void start_servo(void* pvParameters)
{
	xSemaphoreTake(s_servo, portMAX_DELAY); // mi blocco qui aspettando che mi svegliano i due sensori (nella loro END)
    Serial.println("Il servo si è svegliato!");
}

void servo(){
  xSemaphoreTake(mutex, portMAX_DELAY);
      myservo.write(g.position);    // applico la rotazione della cam (non so se aspetta i tempo di rotazione prima di rilasciare il mutex)
  xSemaphoreGive(mutex);
}




void start_siren(void* pvParameters)
{
	xSemaphoreTake(s_siren, portMAX_DELAY);
    xSemaphoreTake(mutex, portMAX_DELAY);
    //Serial.println("Sono lo start_motion_sensor");
	if (g.stato == ALARM_TRIGGERED) //deve essere nello stato corretto
	{
		Serial.println("---------------------------- SIRENA ACCESA ----------------------------!!!");
		tone(BUZZER_PIN, 1000);
	}
	else {
		Serial.println("---------------------------- SIRENA ACCESA ----------------------------!!!");
		noTone(BUZZER_PIN);
	}
	xSemaphoreGive(mutex);
}

void statusLED(void *pvParameters) {
    xSemaphoreTake(s_LED, portMAX_DELAY);
    // Critical section da tenere?
    // Mettere fuori solo le prime 3 write?
    xSemaphoreTake(mutex, portMAX_DELAY);
    // Lo stato viene usato per indicare i pin da accendere. 
    // I collegamenti sono fatti in modo da avere:
    // - lo stato ALARM_OFF che accende la luce blu;
    // - lo stato ALARM_ON che accende la luce verde;
    // - lo stato ALARM_TRIGGERED che accende la luce rossa;
    analogWrite(ALARM_OFF, MIN_COLOR_INTENSITY);
    analogWrite(ALARM_ON, MIN_COLOR_INTENSITY);
    analogWrite(ALARM_TRIGGERED, MIN_COLOR_INTENSITY);
    analogWrite(g.stato, MAX_COLOR_INTENSITY);
    xSemaphoreGive(mutex);
}


void setup()
{
	pinMode(MOTION_SENSOR_PIN, INPUT);
    Serial.begin(9600);
    Serial.println("Inizio il setup");
    Serial.println("No delays");
    myservo.attach(SERVO_PIN); // pin 9
    g.position=90; // i due sensori devono stare a posizione 180 (pir) e 0 (window), e lo stato iniziale sarà a metà.
    myservo.write(g.position);
    //g.stato = PIN;
    // g.alarm=false;
    // g.siren=false;
	// g.alarm_triggered = false;
    g.stato = ALARM_OFF;
	g.b_motion_sensor = 0;

    // inizializzo i pin
    for (int k = 0; k > LENGTH_PIN; k++)
    {
        user_pin[k] = -1;
    }
    index_pin = 0;
    // g.b_stamp = 0;
    
	// Credo che i controlli si possano togliere (?)
    if (mutex == NULL) // Check to confirm that the Serial Semaphore has not already been created.
    {
        mutex = xSemaphoreCreateMutex();
    }
    if (s_pin == NULL)
    {
        s_pin = xSemaphoreCreateBinary();
    }
    if (s_stamp == NULL)
    {
        s_stamp = xSemaphoreCreateBinary();
    }
	s_motion_sensor = xSemaphoreCreateBinary();
    s_siren = xSemaphoreCreateBinary();
    s_servo = xSemaphoreCreateBinary();
    if (s_LED == NULL)
    {
        s_LED = xSemaphoreCreateBinary();
    }

    // Controlla se funziona anche dandogli meno stack-size (es.128)
    xTaskCreate(
        taskStamp, "task-stamp", 256, NULL, 1 // priority  
        ,
        NULL);

    xTaskCreate(
        taskPin, "task-pin", 256, NULL, 1 // priority
        ,
        NULL);

	xTaskCreate(
		taskMotionSensor, "task-motion-sensor", 256, NULL, 1 // priority
		,
		NULL);
    xTaskCreate(
		taskWindow, "task-window-sensor", 256, NULL, 1 // priority
		,
		NULL);
    xTaskCreate(
		taskServo, "task-servo", 256, NULL, 1 // priority
		,
		NULL);
    xTaskCreate(
		taskSiren, "task-siren", 256, NULL, 1 // priority
		,
		NULL);
    xTaskCreate(
		taskLED, "task-LED", 256, NULL, 1 // priority
		,
		NULL);

    // Prima accensione del LED
    xSemaphoreGive(s_LED);
        
}

void taskStamp(void *pvParameters) // This is a task.
{
    (void)pvParameters;

    for (;;)
    {
        stamp();
    }
}

void taskPin(void *pvParameters)
{
    (void)pvParameters;
    for (;;)
    {
        get_pin();
        taskYIELD();
        end_pin(pvParameters);
        taskYIELD();
    }
}

void taskMotionSensor(void* pvParameters)
{
	(void)pvParameters;
	//Serial.begin(4800);
	for (;;)
	{
        //vTaskDelay(50 / portTICK_PERIOD_MS); 
		start_motion_sensor(pvParameters);
        taskYIELD();
		motion_sensor();
        end_motion_sensor(pvParameters);
        taskYIELD();
	}
}

void taskSiren(void* pvParameters)
{
	(void)pvParameters;
	//Serial.begin(4800);
	for (;;)
	{
		start_siren(pvParameters);
	}
}

void taskWindow(void *pvParameters) // This is a task.
{
    (void)pvParameters;

    for (;;)
    {
        start_window_sensor(pvParameters);
        taskYIELD();
        window_sensor();
        end_window_sensor(pvParameters);
        taskYIELD();
    }
}
void taskServo(void *pvParameters) // This is a task.
{
    (void)pvParameters;

    for (;;)
    {
        Serial.println("Servo");
        start_servo(pvParameters);
        servo();
    }
}

void taskLED(void *pvParameters)
{
    (void)pvParameters;
    for (;;)
    {
        statusLED(pvParameters);
    }
}

void loop()
{
    //empty loop
}
