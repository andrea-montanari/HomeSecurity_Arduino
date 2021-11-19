//#include <Arduino_FreeRTOS.h>
//#include "FreeRTOSConfig.h"
//#include "freertos/FreeRTOS.h"
//#include <semphr.h>
#include <Keypad.h>

#define INCLUDE_vTaskSuspend 1

//#define PIETRO 42 // togli se sei monta
#define ESP32

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
#define MOTION_SENSOR_PIN 25
#define BUZZER_PIN 33

#define MAX_COLOR_INTENSITY 255
#define MIN_COLOR_INTENSITY 0
#define noTone 0

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
#endif
#ifdef ESP32
 // PIN associati al keypad monta
byte rowPins[ROWS] = {23, 22, 5, 17}; //connect to the row pinouts of the keypad
byte colPins[COLS] = {16, 4, 26, 27}; //connect to the column pinouts of the keypad
#endif

// inizializzazione del keypad
Keypad customKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);


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
//   int b_stamp;
}g;

int movement_sensor_value; // Place to store read PIR Value


// Semafori privati e mutex
SemaphoreHandle_t mutex = NULL;
SemaphoreHandle_t s_pin = NULL;
SemaphoreHandle_t s_stamp = NULL;
SemaphoreHandle_t s_motion_sensor = NULL;
SemaphoreHandle_t s_siren = NULL;
SemaphoreHandle_t s_LED = NULL;



void taskPin(void *pvParameters);
void taskStamp(void *pvParameters);
void taskMotionSensor(void* pvParameters);
void taskSiren(void* pvParameters);


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
    //Serial.println("Sono la get_pin - stato: "+String(g.stato));
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
    //Serial.println("Sono lo end_pin");
    xSemaphoreTake(mutex, (TickType_t)100);
    // if (g.b_stamp)
    // {
        //g.stato = STAMP;
        //g.b_stamp--;
        xSemaphoreGive(s_stamp);
    // }
    xSemaphoreGive(mutex);
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

               // if (g.b_siren && g.alarm_triggered){
                    //g.b_siren--;
                    //xSemaphoreGive(s_siren); // sveglio la sirena per dirgli di spegnere il suono
                //}
			}
            else if (g.stato == ALARM_TRIGGERED) {
				g.stato = ALARM_OFF;
                xSemaphoreGive(s_siren);
                xSemaphoreGive(s_LED);
          	}
			else {
				// g.alarm = true;
                g.stato = ALARM_ON;
                if (g.b_motion_sensor) {
				    g.b_motion_sensor--;
				    xSemaphoreGive(s_motion_sensor);
                }
                xSemaphoreGive(s_LED);
				Serial.println("END_STAMP: Sveglio sensore movimento.");
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
		//Serial.println("Sensore di movimento parte.");
	}
	else {
        Serial.print("Stato allarme: "); Serial.println(g.stato);
		//Serial.println("START_MOTION_SENSOR: Sensore di movimento si BLOCCA.");
		g.b_motion_sensor++;
	}
	xSemaphoreGive(mutex);
	xSemaphoreTake(s_motion_sensor, portMAX_DELAY); // mi blocco qui nel caso
}

void motion_sensor()
{   //Serial.println("sono lo motion_sensor");
    xSemaphoreTake(mutex, portMAX_DELAY);
	  movement_sensor_value = digitalRead(MOTION_SENSOR_PIN); // shared variable, uso mutex
    Serial.print("-- Valore sensore di movimento: "); Serial.println(movement_sensor_value);
    xSemaphoreGive(mutex);
}

void end_motion_sensor(void* pvParameters)
{
	Serial.println("END_MOTION_SENSOR: Nessun movimento");
	xSemaphoreTake(mutex, portMAX_DELAY);
  Serial.println("Sono end_motion_sensor");
  if (movement_sensor_value && g.stato == ALARM_ON) { 
		g.stato = ALARM_TRIGGERED;
    xSemaphoreGive(s_siren);
    xSemaphoreGive(s_LED);
		Serial.println("\n----------------------------------------- MOVIMENTO RILEVATO!!! -----------------------------------------\n");
	}

	xSemaphoreGive(mutex);
}

void start_siren(void* pvParameters)
{
	xSemaphoreTake(s_siren, portMAX_DELAY);
    xSemaphoreTake(mutex, portMAX_DELAY);
    Serial.println("Sono lo start_motion_sensor");
	if (g.stato == ALARM_TRIGGERED) //deve essere nello stato corretto
	{
		Serial.println("---------------------------- SIRENA ACCESA ----------------------------!!!");

		//tone(BUZZER_PIN, 1000); // Volendo si può usare la PWM per modificare il tono.
    digitalWrite(BUZZER_PIN, HIGH);
	}
	else {
		Serial.println("---------------------------- SIRENA ACCESA ----------------------------!!!");
		digitalWrite(BUZZER_PIN, LOW);
	}
	xSemaphoreGive(mutex);
}


void statusLED(void *pvParameters) {
    xSemaphoreTake(s_LED, portMAX_DELAY);
    // Critical section da tenere?
    // Trovare una logica migliore? Spegnere solo il led che era acceso prima?
    // Mettere fuori solo le prime 3 write?
    // Lo stato viene usato per indicare i pin da accendere. 
    // I collegamenti sono fatti in modo da avere:
    // - lo stato ALARM_OFF che accende la luce blu;
    // - lo stato ALARM_ON che accende la luce verde;
    // - lo stato ALARM_TRIGGERED che accende la luce rossa;
    xSemaphoreTake(mutex, portMAX_DELAY);
    digitalWrite(RED_LED, LOW);
    digitalWrite(BLUE_LED, LOW);
    digitalWrite(GREEN_LED, LOW);
    digitalWrite(g.stato, HIGH);
    xSemaphoreGive(mutex);
}


void setup()
{
	pinMode(MOTION_SENSOR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);
  
    Serial.begin(2400);
    Serial.println("Inizio il setup");
    Serial.println("No delays");

    //g.stato = PIN;
    // g.alarm=false;
    // g.siren=false;
	// g.alarm_triggered = false;
    g.stato = ALARM_OFF;
	g.b_motion_sensor = false;

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
    if (s_LED == NULL)
    {
        s_LED = xSemaphoreCreateBinary();
    }

    // Controlla se funziona anche dandogli meno stack-size (es.128)

    
    xTaskCreatePinnedToCore(
        taskStamp, 
        "task-stamp", 
        20000, // task overhead: 768 bytes (?)
        NULL, 
        0, // priority  
        NULL,
        0); // Core

    xTaskCreatePinnedToCore(
        taskPin, 
        "task-pin",
        20000, 
        NULL, 
        0, // priority  
        NULL,
        0);

	xTaskCreatePinnedToCore(
		taskMotionSensor, 
        "task-motion-sensor", 
        20000, 
        NULL, 
        0, // priority  
        NULL,
        0);
    xTaskCreatePinnedToCore(
		taskSiren, 
        "task-siren",
        20000, 
        NULL, 
        0, // priority  
        NULL,
        0);
    xTaskCreatePinnedToCore(
		taskLED, 
        "task-LED", 
        10000, 
        NULL, 
        0, // priority  
        NULL,
        0);

    // Prima accensione del LED
    xSemaphoreGive(s_LED);
    Serial.println("Fine setup.");

    // Delete "setup and loop" task
    vTaskDelete(NULL);
}


// Funziona per avere il nome del task corrente
void getTaskName(char* name)
{
  strcpy(name, pxCurrentTCB->taskname);
}  

char ptrTaskList[250];

void taskStamp(void *pvParameters) // This is a task.
{
    (void)pvParameters;

    for (;;)
    {
        //start_stamp(pvParameters);
        //vTaskDelay(20 / portTICK_PERIOD_MS); // wait for one second
        stamp();
        xSemaphoreTake(mutex, portMAX_DELAY);
        Serial.print("Stack della stampa: "); Serial.println(uxTaskGetStackHighWaterMark(NULL));
        xSemaphoreGive(mutex);
        taskYIELD();
        //vTaskDelay(20 / portTICK_PERIOD_MS);
        //end_stamp(pvParameters);
    }
}

void taskPin(void *pvParameters)
{
    (void)pvParameters;
    for (;;)
    {
        get_pin();
        //vTaskDelay(50 / portTICK_PERIOD_MS); // wait for one second
        end_pin(pvParameters);
        xSemaphoreTake(mutex, portMAX_DELAY);
        Serial.print("Stack del PIN: "); Serial.println(uxTaskGetStackHighWaterMark(NULL));
        xSemaphoreGive(mutex);
        taskYIELD();
    }
}

void taskMotionSensor(void* pvParameters)
{
	(void)pvParameters;
	//Serial.begin(4800);
	for (;;)
	{
		start_motion_sensor(pvParameters);
		//vTaskDelay(100 / portTICK_PERIOD_MS); // wait for one second
		motion_sensor();
		//vTaskDelay(100 / portTICK_PERIOD_MS); // wait for one second
    end_motion_sensor(pvParameters);
    Serial.print("Stack del motion sensor: "); Serial.println(uxTaskGetStackHighWaterMark(NULL));
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
    Serial.print("Stack della sirena: "); Serial.println(uxTaskGetStackHighWaterMark(NULL));
		//vTaskDelay(100 / portTICK_PERIOD_MS); // wait for one second
		//siren();
		//vTaskDelay(100 / portTICK_PERIOD_MS); // wait for one second

	}
}

void taskLED(void *pvParameters)
{
    (void)pvParameters;
    for (;;)
    {
        statusLED(pvParameters);
        Serial.print("Stack del LED: "); Serial.println(uxTaskGetStackHighWaterMark(NULL));
    }
}

void loop()
{
    //empty loop
}
