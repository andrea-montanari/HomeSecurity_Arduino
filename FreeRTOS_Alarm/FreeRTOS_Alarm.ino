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


char auth[] = BLYNK_AUTH_TOKEN;

// WiFi credentials.
char ssid[] = SECRET_SSID;
char pass[] = SECRET_PSW;

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
#define MOTION_SENSOR_PIN 25
#define BUZZER_PIN 33
#define SERVO_PIN 14
#define WINDOW_PIN 12

// Blynk LCD positions
#define X_start 0
#define Y_first_raw 0
#define Y_second_raw 1

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

    // PIN associati al keypad monta
byte rowPins[ROWS] = {23, 22, 5, 17}; //connect to the row pinouts of the keypad
byte colPins[COLS] = {16, 4, 26, 27}; //connect to the column pinouts of the keypad


// inizializzazione del keypad
Keypad customKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);
Servo myservo;

char true_system_pin[4] = {'0', '0', '0', '0'}; // password giusta del sistema
char user_pin[LENGTH_PIN];                      // password inserita dall'utente
int index_pin;                                  // indice per gestire user_pin

// Blynk setup
BlynkTimer timer;
WidgetLED led_alarm_blynk(V0);
WidgetLED led_pir_blynk(V1);
WidgetLED led_window_blynk(V2);
WidgetLCD lcd(V5);

// Struttura dati con all'interno gli stati e i bloccati
struct gestore
{
    int stato; // stato dell'allarme = {'OFF','ON','TRIGGERED'}
    int b_motion_sensor;
    int position;
} g;


// Semafori privati e mutex
SemaphoreHandle_t mutex = NULL;
SemaphoreHandle_t s_pin = NULL;
SemaphoreHandle_t s_stamp = NULL;
SemaphoreHandle_t s_motion_sensor = NULL;
SemaphoreHandle_t s_siren = NULL;
SemaphoreHandle_t s_LED = NULL;
SemaphoreHandle_t s_servo = NULL;

void taskPin(void *pvParameters);
void taskStamp(void *pvParameters);
void taskMotionSensor(void *pvParameters);
void taskSiren(void *pvParameters);
void taskLED(void *pvParameters);
void taskWindowSensor(void *pvParameters); // task per il bottone
void taskServo(void *pvParameters);
void taskBlynk(void *pvParameters);


/**
 * Serve per prendere l'input dall'applicazione Blynk per la rotazione del servo (videocamera). Verrà aggiornata la posizione e
 * verrà applicata la rotazione svegliando il semaforo privato del servo.
 * 
 **/
BLYNK_WRITE (V4)
{
  int position = param.asInt();
  xSemaphoreTake(mutex, portMAX_DELAY);

  Serial.print("La pozione è: ");
  Serial.println(position);
  g.position=position;
  xSemaphoreGive(s_servo);
  
  xSemaphoreGive(mutex);
}



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

// usata per printare solamente fino all'indice a cui siamo arrivati
void print_user_pin()
{
    String lcd_string = "";
    if (index_pin == 1) {
        lcd.clear();
        lcd.print(X_start, Y_first_raw, "PIN inserito:");
    }

    for (int k = 0; k < index_pin; k++)
    {
        //Serial.print("User pin: "); Serial.println(user_pin[k]);
        Serial.print(user_pin[k]);
        lcd_string += user_pin[k];
        //Serial.print("Stringa: "); Serial.println(lcd_string);
    }
    lcd.print(X_start, Y_second_raw, lcd_string);
    if (index_pin > 0)
        Serial.print('\n'); // così si risparmiano delle stampe
}

void get_pin()
{
    char customKey = customKeypad.getKey(); // non posso fare il controllo direttamente altrimenti perderei il valore se non lo salvo prima
    if (customKey)
    {
        xSemaphoreTake(mutex, portMAX_DELAY);
        user_pin[index_pin] = customKey; // possibile race conditions su shared variable (per questo usato mutex)
        index_pin++;                     //aggiorno index_pin
        xSemaphoreGive(mutex);
        xSemaphoreGive(s_stamp);
    }
}


// Qui la stampa è fatta su seriale (potremmo mantenerlo come un task a parte(?))
// Importante perchè qui si modificano i vari stati dell'allarme, e si svegliano anche dei task (sirena)
void stamp()
{
    xSemaphoreTake(s_stamp, portMAX_DELAY);
    xSemaphoreTake(mutex, portMAX_DELAY);
    print_user_pin();
    if (index_pin == 4)
    {
        bool valid_pin = is_pin_valid(user_pin, true_system_pin); // check del pin con "0000"
        // Switch case per essere più efficienti??
        if (valid_pin)
        {
            Serial.print("Il pin inserito è GIUSTO ");
            Serial.println(user_pin);
            lcd.clear();
            lcd.print(X_start, Y_first_raw, "PIN corretto");
            if (g.stato == ALARM_ON)
            {
                g.stato = ALARM_OFF; //anche qui g.alarm è una var condivisa, quindi possibili race condition
                Serial.println("Allarme spento.");
                xSemaphoreGive(s_LED);
            }
            else if (g.stato == ALARM_TRIGGERED)
            {
                g.stato = ALARM_OFF;
                g.position = 90; // la videocamera deve riprendere il suo stato iniziale (ovvero 90 gradi: al centro)
                xSemaphoreGive(s_siren);
                xSemaphoreGive(s_LED);
                xSemaphoreGive(s_servo);
            }
            else
            {
                g.stato = ALARM_ON;
                while (g.b_motion_sensor)
                {                                                            // siccome ci sono 2 sensori devo svegliarli entrambi
                    g.b_motion_sensor--;
                    xSemaphoreGive(s_motion_sensor); //semaforo n-ario
                }
                xSemaphoreGive(s_LED);
            }
        }
        else
        {
            Serial.print("Il pin inserito è SBAGLIATO ");
            Serial.println(user_pin);
        }
        // rinizializzazione del pin
        for (int k = 0; k > LENGTH_PIN; k++)
        {
            user_pin[k] = -1;
        }
        index_pin = 0;
    }
    xSemaphoreGive(mutex);
}

void start_motion_sensor(void *pvParameters)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (g.stato == ALARM_ON || g.stato == ALARM_TRIGGERED) // se allarme è off mi blocco
    {
        xSemaphoreGive(s_motion_sensor);
    }
    else
    {
        g.b_motion_sensor++;
    }
    xSemaphoreGive(mutex);
    xSemaphoreTake(s_motion_sensor, portMAX_DELAY); // mi blocco qui nel caso
}

unsigned long myTime;


void motion_sensor(void *pvParameters)
{
    if(digitalRead(MOTION_SENSOR_PIN))
    {   
        xSemaphoreTake(mutex, portMAX_DELAY);
        Serial.println("-------- MOVIMENTO RILEVATO da PIR!!! -----------");
        if (g.stato == ALARM_ON)
        { // se c'è movimento e l'allarme è ON (e non triggered quindi)
            g.stato = ALARM_TRIGGERED;
            // per invio informazioni e notifiche ad app blynk
            Blynk.setProperty(V1, "color", "#ff0000");
            Blynk.setProperty(V1, "label", "PIR TRIGGERED");
            Blynk.logEvent("pir_triggered");
            xSemaphoreGive(s_siren);
            xSemaphoreGive(s_LED);
            if (g.position != 180)
            {                     // se è gia in posizione 180 sta gia filmando quindi non serve svegliare videocamera
                g.position = 180; // modifica la posizione
                xSemaphoreGive(s_servo);
            }
        }
        else if (g.stato == ALARM_TRIGGERED && g.position != 180)
        {                     // sveglio il servo per spostare videocamera
            g.position = 180; // modifica la posizione
            xSemaphoreGive(s_servo);
            Blynk.setProperty(V1, "color", "#ff0000");
            Blynk.setProperty(V1, "label", "PIR TRIGGERED");
            Blynk.logEvent("pir_triggered");
        }
        xSemaphoreGive(mutex);
        // Aggiunto delay
        Serial.println("Inizio delay PIR");
        vTaskDelay( 5200 / portTICK_PERIOD_MS); // il delay ha due funzioni: per risolvere il problema del periodo HIGH (vedi video) e per risparmiare cpu durante il periodo low (in cui so per certo che non diventerà HIGH) per limiti fisici PIR
        Serial.println("Fine delay PIR");
        
    }
}

void start_window_sensor(void *pvParameters)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (g.stato == ALARM_ON || g.stato == ALARM_TRIGGERED) // se allarme è off mi blocco
    {
        xSemaphoreGive(s_motion_sensor);
    }
    else
    {
        g.b_motion_sensor++;
    }
    xSemaphoreGive(mutex);
    xSemaphoreTake(s_motion_sensor, portMAX_DELAY); // mi blocco qui nel caso
}


void window_sensor(void* pvParameters)
{
    if(!digitalRead(WINDOW_PIN))
    {
        xSemaphoreTake(mutex, portMAX_DELAY);
        Serial.println("-------- MOVIMENTO RILEVATO da finestra!!! -----------");
        if (g.stato==ALARM_ON){ // se c'è movimento e l'allarme è ON (e non triggered quindi)
            g.stato = ALARM_TRIGGERED;
            // per invio informazioni e notifiche ad app blynk
            Blynk.setProperty(V2, "color", "#ff0000");
            Blynk.setProperty(V2, "label", "Window OPENED");
            Blynk.logEvent("window_opened");
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
            Blynk.setProperty(V2, "color", "#ff0000");
            Blynk.setProperty(V2, "label", "Window OPENED");
            Blynk.logEvent("window_opened");
        }
	    xSemaphoreGive(mutex);
    }
}


void start_servo(void* pvParameters)
{
	xSemaphoreTake(s_servo, portMAX_DELAY); // mi blocco qui aspettando che mi svegliano i due sensori (nella loro END)
    Serial.println("Il servo si è svegliato!");
}

void servo(){
  xSemaphoreTake(mutex, portMAX_DELAY);
      myservo.write(g.position);    // applico la rotazione della cam (non so se aspetta i tempo di rotazione prima di rilasciare il mutex)
      Blynk.virtualWrite(V3,g.position); // scrivo il valore della rotazione sulla app blynk
  xSemaphoreGive(mutex);
}


void siren(void *pvParameters)
{
    xSemaphoreTake(s_siren, portMAX_DELAY);
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (g.stato == ALARM_TRIGGERED) //deve essere nello stato corretto
    {
        Serial.println("---------------------------- SIRENA ACCESA ----------------------------!!!");
        //tone(BUZZER_PIN, 1000); // Volendo si può usare la PWM per modificare il tono.
        //digitalWrite(BUZZER_PIN, HIGH);
    }
    else
    {
        Serial.println("---------------------------- SIRENA ACCESA ----------------------------!!!");
        digitalWrite(BUZZER_PIN, LOW);
    }
    xSemaphoreGive(mutex);
}

void statusLED(void *pvParameters)
{
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
    int stato = g.stato;
    xSemaphoreGive(mutex);

    digitalWrite(RED_LED, LOW);
    digitalWrite(BLUE_LED, LOW);
    digitalWrite(GREEN_LED, LOW);
    digitalWrite(stato, HIGH);
    
    
    switch (stato)
    {
    case ALARM_OFF:
        Blynk.setProperty(V0, "color", "#0000ff");
        Blynk.setProperty(V0, "label", "ALARM OFF");
        Blynk.setProperty(V1, "color", "#00ff00");
        //Blynk.setProperty(V1, "label", "PIR never triggered");
        Blynk.setProperty(V2, "color", "#00ff00");
        //Blynk.setProperty(V2, "label", "Window never opened");
        break;
    
    case ALARM_ON:
        Blynk.setProperty(V0, "color", "#00ff00");
        Blynk.setProperty(V0, "label", "ALARM ON");
        break;
    
    case ALARM_TRIGGERED:
        Blynk.setProperty(V0, "color", "#ff0000");
        Blynk.setProperty(V0, "label", "ALARM TRIGGERED");
        Blynk.logEvent("ALARM_TRIGGERED");
        break;
    }
    
    
}


void setup()
{
    // Setting idle watchdog to 30s and disabling the controller reset if triggered
    // esp_task_wdt_init(999999999, false);

    // Unsubscribe idle task from the Task Watchdog Timer
    // TaskHandle_t idleTaskHandle = xTaskGetCurrentTaskHandle();
    esp_task_wdt_delete(xTaskGetCurrentTaskHandle());
    
    
        // Begin WiFi and Blynk connection
    Blynk.begin(auth, ssid, pass);
    Blynk.run();
        // Accensione LED su Blynk
    led_alarm_blynk.on();
    led_pir_blynk.on();
    led_window_blynk.on();
    timer.run();

    pinMode(MOTION_SENSOR_PIN, INPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(RED_LED, OUTPUT);
    pinMode(GREEN_LED, OUTPUT);
    pinMode(BLUE_LED, OUTPUT);
    pinMode(WINDOW_PIN, INPUT_PULLUP);  


    Serial.begin(9600);
    Serial.println("Inizio il setup");
    myservo.attach(SERVO_PIN); 
    g.position=90; // i due sensori devono stare a posizione 180 (pir) e 0 (window), e lo stato iniziale sarà a metà.
    myservo.write(g.position);
    Blynk.virtualWrite(V3,g.position); // possibile errore('?)


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
    if (s_motion_sensor == NULL)
    {
        s_motion_sensor = xSemaphoreCreateCounting( 2, 0 ); // è un semaforo per 2 sensori
    }
    if (s_servo == NULL)
    {
        s_servo = xSemaphoreCreateBinary();
    }
    if (s_siren == NULL)
    {
        s_siren = xSemaphoreCreateBinary();
    }
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
        1); // Core

    xTaskCreatePinnedToCore(
        taskPin,
        "task-pin",
        20000,
        NULL,
        0, // priority
        NULL,
        1);

    xTaskCreatePinnedToCore(
        taskMotionSensor,
        "task-motion-sensor",
        20000,
        NULL,
        0, // priority
        NULL,
        1);
    xTaskCreatePinnedToCore(
        taskWindowSensor,
        "task-window-sensor",
        20000,
        NULL,
        0, // priority
        NULL,
        1);
    xTaskCreatePinnedToCore(
        taskSiren,
        "task-siren",
        20000,
        NULL,
        0, // priority
        NULL,
        1);
    xTaskCreatePinnedToCore(
        taskServo,
        "task-servo",
        20000,
        NULL,
        0, // priority
        NULL,
        1);
    xTaskCreatePinnedToCore(
        taskLED,
        "task-LED",
        10000,
        NULL,
        0, // priority
        NULL,
        1);  
    xTaskCreatePinnedToCore(
        taskBlynk,
        "task-blynk",
        30000,
        NULL,
        1, // messa con priorità maggiore perchè altrimenti dava problemi con primitiva pbuf_free() e abortiva tutto
        NULL,
        0);
       
    
    // Prima accensione del LED
    xSemaphoreGive(s_LED);
    Serial.println("Fine setup.");

    // Delete "setup and loop" task
    vTaskDelete(NULL);
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
    }
}

void taskMotionSensor(void *pvParameters)
{
    (void)pvParameters;
    //Serial.begin(4800);
    for (;;)
    {
        start_motion_sensor(pvParameters);
        motion_sensor(pvParameters);
        taskYIELD();
    }
}

void taskWindowSensor(void *pvParameters) // This is a task.
{
    (void)pvParameters;

    for (;;)
    {
        start_window_sensor(pvParameters);
        window_sensor(pvParameters);
        taskYIELD();
    }
}



void taskServo(void *pvParameters) // This is a task.
{
    (void)pvParameters;

    for (;;)
    {
        start_servo(pvParameters);
        servo();
    }
}

void taskSiren(void *pvParameters)
{
    (void)pvParameters;
    for (;;)
    {
        siren(pvParameters);
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


void taskBlynk(void *pvParameters)
{
    (void)pvParameters;
    for (;;)
    {
        Blynk.run();
        vTaskDelay(20 / portTICK_PERIOD_MS); // wait for one second
    }
}


void loop()
{

}
