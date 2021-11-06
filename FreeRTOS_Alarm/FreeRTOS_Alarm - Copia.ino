#include <Arduino_FreeRTOS.h>
#include <semphr.h>
#include <Keypad.h>

#define INCLUDE_vTaskSuspend 1

#define PIETRO 42 // togli se sei monta

// stati
#define STAMP 0
#define SENSOR_MOVEMENT 1
#define SIREN 2

// lunghezza PIN
#define LENGTH_PIN 4

#define MOTION_SENSOR_PIN 31
#define BUZZER_PIN 29

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


char true_system_pin[4] = {'0', '0', '0', '0'}; // password giusta del sistema
char user_pin[LENGTH_PIN]; // password inserita dall'utente
int index_pin; // indice per gestire user_pin

// Struttura dati con all'interno gli stati e i bloccati
struct gestore
{
  int stato;
  bool alarm;          // mode: DISABLED/ENABLED
  bool siren;         // ON/OFF per far suonare l'allarme
  bool alarm_triggered;
  int b_motion_sensor;
  int b_siren;
  int b_stamp;
}g;

int movement_sensor_value; // Place to store read PIR Value


// Semafori privati e mutex
SemaphoreHandle_t mutex = NULL;
SemaphoreHandle_t s_pin = NULL;
SemaphoreHandle_t s_stamp = NULL;
SemaphoreHandle_t s_motion_sensor = NULL;
SemaphoreHandle_t s_siren = NULL;


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
    if(g.alarm)Serial.println("Alarm On");
    else Serial.println("Alarm Off");
}

/**
 * Funzioni usate per la sincronizzazione dei task (preambolo e postambolo)
 */


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
    if (g.b_stamp)
    {
        g.stato = STAMP;
        g.b_stamp--;
        xSemaphoreGive(s_stamp);
    }
    xSemaphoreGive(mutex);
}

void start_stamp(void *pvParameters)
{
    xSemaphoreTake(mutex, (TickType_t)100);
    //Serial.println("Sono lo start_stamp");
    if (g.stato == STAMP)
    {
        xSemaphoreGive(s_stamp);
    }
    else
        g.b_stamp++;
    xSemaphoreGive(mutex);
    xSemaphoreTake(s_stamp, (TickType_t)100); // mi blocco qui nel caso
}

// Qui la stampa è fatta su seriale (potremmo mantenerlo come un task a parte(?))
// Importante perchè qui si modificano i vari stati dell'allarme, e si svegliano anche dei task (sirena)
void stamp()
{
    print_user_pin();
    //print_alarm_state();
    if (index_pin == 4)
    {
        bool valid_pin = is_pin_valid(user_pin, true_system_pin); // check del pin con "0000"
        if (valid_pin)
        {
            Serial.print("Il pin inserito è GIUSTO ");
            Serial.println(user_pin);
            xSemaphoreTake(mutex, (TickType_t)100);
            // il pin è giusto, quindi bisogna cambiare lo stato dell'allarme: nel caso in cui sia off->on nel caso sia on->off
            // BISOGNERA' FARE REFACTORING, siccome non è ottimale
			if (g.alarm) {
				g.alarm = false;              //anche qui g.alarm è una var condivisa, quindi possibili race condition
                Serial.println("Allarme spento.");
                if (g.b_siren && g.alarm_triggered){
                    g.b_siren--;
                    xSemaphoreGive(s_siren); // sveglio la sirena per dirgli di spegnere il suono
                }
                g.alarm_triggered=false;
			}
			else {
				g.alarm = true;
			}
            xSemaphoreGive(mutex);
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
}

void end_stamp(void *pvParameters)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    //Serial.println("Sono l'end_stamp");
    if (g.b_motion_sensor && g.alarm) {	// Sblocca il sensore di movimento se era bloccato
					g.b_motion_sensor--;
                    g.stato=SENSOR_MOVEMENT;
					xSemaphoreGive(s_motion_sensor);
					Serial.println("END_STAMP: Sveglio sensore movimento."); // Dopo aver attivato il sensore non risponde più agli input da tastierino (?)
				}
    xSemaphoreGive(mutex);
}



void start_motion_sensor(void* pvParameters)
{
	xSemaphoreTake(mutex, portMAX_DELAY);
    //Serial.println("Sono lo start_motion_sensor");
	if ((g.alarm || g.alarm_triggered) && g.stato==SENSOR_MOVEMENT) //deve essere nello stato corretto
	{
		xSemaphoreGive(s_motion_sensor);
		//Serial.println("Sensore di movimento parte.");
	}
	else {
		Serial.println("START_MOTION_SENSOR: Sensore di movimento si BLOCCA.");
		g.b_motion_sensor++;
	}
	xSemaphoreGive(mutex);
	xSemaphoreTake(s_motion_sensor, portMAX_DELAY); // mi blocco qui nel caso
}

void motion_sensor()
{   //Serial.println("sono lo motion_sensor");
    xSemaphoreTake(mutex, portMAX_DELAY);
	movement_sensor_value = digitalRead(MOTION_SENSOR_PIN); // shared variable, uso mutex
    xSemaphoreGive(mutex);
}

void end_motion_sensor(void* pvParameters)
{
	xSemaphoreTake(mutex, portMAX_DELAY);
    //Serial.println("Sono end_motion_sensor");
    if (movement_sensor_value) { 
		g.alarm_triggered = true;
        if (!g.siren && g.b_siren){ // sveglio la sirena (siccome prima era spenta)
            g.stato=SIREN;
            g.b_siren--;
            xSemaphoreGive(s_siren);
            }
		Serial.println("\nMovimento rilevato!!!\n");
	}
    else if (!movement_sensor_value){
        Serial.println("END_MOTION_SENSOR: Nessun movimento, sveglio il PIN");
    }
	xSemaphoreGive(mutex);
}

void start_siren(void* pvParameters)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    //Serial.println("Sono lo start_motion_sensor");
	if (g.stato==SIREN) //deve essere nello stato corretto
	{
		xSemaphoreGive(s_siren);
		//Serial.println("Sensore di movimento parte.");
	}
	else {
		//Serial.println("Sirena si BLOCCA.");
		g.b_siren++;
        Serial.println("Start_siren: la sirena si è bloccata");

	}
	xSemaphoreGive(mutex);
	xSemaphoreTake(s_siren, portMAX_DELAY); // mi blocco qui nel caso
    Serial.println("La sirena si è svegliata!");
}

void siren(){
 // A duration can be specified, otherwise the wave continues until a call to noTone(). 
 if(g.siren){ // la sirena è accesa, sono stato svegliato dal task STAMP per spegnerla (oppure, nelle seguenti implementazioni anche da un timer, che quando scade dice di finire di suonare)
    noTone(BUZZER_PIN);
    xSemaphoreTake(mutex, portMAX_DELAY); // race condition
    g.siren=false;
    xSemaphoreGive(mutex);
 }
 else{ // la sirena è spenta, sono stato svegliato dal task sensore_movimento perché devo accenderla
    tone(BUZZER_PIN, 1000); 
    xSemaphoreTake(mutex, portMAX_DELAY); // race condition
    g.siren=true;
    xSemaphoreGive(mutex);
 }
}




void setup()
{
	pinMode(MOTION_SENSOR_PIN, INPUT);
    Serial.begin(9600);
    Serial.println("Inizio il setup");

    //g.stato = PIN;
    g.alarm=false;
    g.siren=false;
	g.alarm_triggered = false;
	g.b_motion_sensor = false;

    // inizializzo i pin
    for (int k = 0; k > LENGTH_PIN; k++)
    {
        user_pin[k] = -1;
    }
    index_pin = 0;
    g.b_stamp = 0;
    
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
		taskSiren, "task-siren", 256, NULL, 1 // priority
		,
		NULL);
        
}

void taskStamp(void *pvParameters) // This is a task.
{
    (void)pvParameters;

    for (;;)
    {
        start_stamp(pvParameters);
        vTaskDelay(20 / portTICK_PERIOD_MS); // wait for one second
        stamp();
        vTaskDelay(20 / portTICK_PERIOD_MS);
        end_stamp(pvParameters);
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
    }
}

void taskMotionSensor(void* pvParameters)
{
	(void)pvParameters;
	//Serial.begin(4800);
	for (;;)
	{
		start_motion_sensor(pvParameters);
		vTaskDelay(100 / portTICK_PERIOD_MS); // wait for one second
		motion_sensor();
		vTaskDelay(100 / portTICK_PERIOD_MS); // wait for one second
        end_motion_sensor(pvParameters);

	}
}

void taskSiren(void* pvParameters)
{
	(void)pvParameters;
	//Serial.begin(4800);
	for (;;)
	{
		start_siren(pvParameters);
		vTaskDelay(100 / portTICK_PERIOD_MS); // wait for one second
		siren();
		vTaskDelay(100 / portTICK_PERIOD_MS); // wait for one second

	}
}

void loop()
{
    //empty loop
}
