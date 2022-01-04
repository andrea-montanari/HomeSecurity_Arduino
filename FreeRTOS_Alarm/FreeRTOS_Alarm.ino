#include "FreeRTOS_Alarm.h"

// Uncomment the "#define PRINT_STACK_HWM" line in the header to print tasks' stack high water mark info periodically

#ifdef PRINT_STACK_HWM
UBaseType_t stackStamp = 0;
UBaseType_t stackPIN = 0;
UBaseType_t stackMotion1 = 0;
UBaseType_t stackMotion2 = 0;
UBaseType_t stackWindow = 0;
UBaseType_t stackSiren = 0;
UBaseType_t stackServo = 0;
UBaseType_t stackLED = 0;
UBaseType_t stackBlynk = 0;
#endif


const char *auth = BLYNK_AUTH_TOKEN;

// WiFi credentials.
const char *ssid = SECRET_SSID;
const char *pass = SECRET_PSW;

// Setup del keypad
char hexaKeys[ROWS][COLS] = { // Per il Pin
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}};

// Keypad pins
byte rowPins[ROWS] = {23, 22, 5, 17}; //connect to the row pinouts of the keypad
byte colPins[COLS] = {16, 4, 26, 27}; //connect to the column pinouts of the keypad


// inizializzazione del keypad
Keypad customKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);
Servo myservo;
char true_system_pin[LENGTH_PIN] = {'0', '0', '0', '0'}; // password giusta del sistema
char user_pin[LENGTH_PIN];                      // password inserita dall'utente
uint8_t index_pin;                                  // indice per gestire user_pin

// Blynk setup
WidgetLED led_alarm_blynk(V0);
WidgetLED led_pir1_blynk(V1);
WidgetLED led_pir2_blynk(V6);
WidgetLED led_window_blynk(V2);
WidgetLCD lcd(V5);

gestore g;

// Semafori privati e mutex
SemaphoreHandle_t mutex = NULL;
SemaphoreHandle_t s_pin = NULL;
SemaphoreHandle_t s_stamp = NULL;
SemaphoreHandle_t s_sensor = NULL;
SemaphoreHandle_t s_siren = NULL;
SemaphoreHandle_t s_LED = NULL;
SemaphoreHandle_t s_servo = NULL;

/**
 * Serve per prendere l'input dall'applicazione Blynk per la rotazione del servo (videocamera). Verrà aggiornata la posizione e
 * verrà applicata la rotazione svegliando il semaforo privato del servo.
 * 
 **/
BLYNK_WRITE (V4)
{
  uint8_t position = param.asInt();
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
    bool val=true;
    for (uint8_t i = 0; i < uint8_t(LENGTH_PIN); i++)
    {
        if (user_pin[i] != true_system_pin[i])
        {
            val=false;
            break;
        } // appena trova un elemento diverso esce dal ciclo
    }
    return val;
}

// usata per printare solamente fino all'indice a cui siamo arrivati
void print_user_pin()
{
    String lcd_string = "";
    if (index_pin == 1U) {
        lcd.clear();
        lcd.print(X_start, Y_first_raw, "PIN inserito:");
    }

    for (uint8_t k = 0; k < index_pin; k++)
    {
        //Serial.print("User pin: "); Serial.println(user_pin[k]);
        Serial.print(user_pin[k]);
        lcd_string += user_pin[k];
        //Serial.print("Stringa: "); Serial.println(lcd_string);
    }
    lcd.print(X_start, Y_second_raw, lcd_string);
    if (index_pin > 0U) {
        Serial.print('\n'); // così si risparmiano delle stampe
    }
}

void get_pin()
{
    char customKey = customKeypad.getKey(); // non posso fare il controllo direttamente altrimenti perderei il valore se non lo salvo prima
    if (customKey != NO_KEY)
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
    if (index_pin == 4U)
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
                g.position = POSITION_DEFAULT; // la videocamera deve riprendere il suo stato iniziale (ovvero 90 gradi: al centro)
                xSemaphoreGive(s_siren);
                xSemaphoreGive(s_LED);
                xSemaphoreGive(s_servo);
            }
            else
            {
                g.stato = ALARM_ON;
                while (g.b_sensor > 0)
                {                                                            // siccome ci sono 2 sensori devo svegliarli entrambi
                    g.b_sensor--;
                    xSemaphoreGive(s_sensor); //semaforo n-ario
                }
                xSemaphoreGive(s_LED);
            }
        }
        else
        {
            Serial.print("Il pin inserito è SBAGLIATO");
            Serial.println(user_pin);
            lcd.clear();
            lcd.print(X_start, Y_first_raw, "PIN errato");
        }
        // rinizializzazione del pin
        for (uint8_t k = 0; k > (uint8_t)LENGTH_PIN; k++)
        {
            user_pin[k] = -1;
        }
        index_pin = 0;
    }
    xSemaphoreGive(mutex);

}

void start_motion_sensor()
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    //Serial.print("semaforo nel motion sensor: "); Serial.println(uxSemaphoreGetCount(s_motion_sensor));
    if (g.stato!=ALARM_OFF) // se allarme è off mi blocco
    {
        xSemaphoreGive(s_sensor);
    }
    else
    {
        g.b_sensor++;
    }
    xSemaphoreGive(mutex);
    xSemaphoreTake(s_sensor, portMAX_DELAY); // mi blocco qui nel caso
}


void motion_sensor(uint8_t pin_pir, uint8_t virtual_pin, char * str_code_blynk, uint8_t position_pir)
{

    if(digitalRead(pin_pir) != 0)
    {   
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (g.stato == ALARM_ON)
        { // se c'è movimento e l'allarme è ON (e non triggered quindi)
            Serial.println("----- MOVIMENTO RILEVATO da PIR!!! -------");
            g.stato = ALARM_TRIGGERED;
            // per invio informazioni e notifiche ad app blynk
            Blynk.setProperty(virtual_pin, "color", "#ff0000");
            Blynk.logEvent(str_code_blynk);
            xSemaphoreGive(s_siren);
            xSemaphoreGive(s_LED);
            if (g.position != position_pir)
            {                     // se è gia in posizione 180 sta gia filmando quindi non serve svegliare videocamera
                g.position = position_pir; // modifica la posizione
                xSemaphoreGive(s_servo);
            }
        }
        else if ( (g.stato == ALARM_TRIGGERED) && (g.position != position_pir) )
        {                     // sveglio il servo per spostare videocamera
            Serial.println("----- MOVIMENTO RILEVATO da PIR!!! -------");
            g.position = position_pir; // modifica la posizione
            xSemaphoreGive(s_servo);
            Blynk.setProperty(virtual_pin, "color", "#ff0000");
            Blynk.logEvent(str_code_blynk);
        }
        else {
            // No action required
        }
        xSemaphoreGive(mutex);
        // Aggiunto delay
        // diminuito di un po' il delay per essere piu responsive (siccome quando si sveglierà, sarà nel periodo LOW)
        vTaskDelay( 3400 / portTICK_PERIOD_MS); // il delay ha due funzioni: per risolvere il problema del periodo HIGH (vedi video) e per risparmiare cpu durante il periodo low (in cui so per certo che non diventerà HIGH) per limiti fisici PIR
        
    }
}

void start_window_sensor()
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    //Serial.print("semaforo nel window: "); Serial.println(uxSemaphoreGetCount(s_motion_sensor));
    if ( (g.stato == ALARM_ON) || (g.stato == ALARM_TRIGGERED) ) // se allarme è off mi blocco
    {
        xSemaphoreGive(s_sensor);
    }
    else
    {
        g.b_sensor++;
    }
    xSemaphoreGive(mutex);   
    xSemaphoreTake(s_sensor, portMAX_DELAY); // mi blocco qui nel caso
}


void window_sensor()
{
    if(!digitalRead(WINDOW_PIN))
    {
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (g.stato==ALARM_ON){ // se c'è movimento e l'allarme è ON (e non triggered quindi)
            Serial.println("---- MOVIMENTO RILEVATO da finestra!!! -----");
            g.stato = ALARM_TRIGGERED;
            // per invio informazioni e notifiche ad app blynk
            Blynk.setProperty(V2, "color", "#ff0000");
            Blynk.logEvent("window_opened");
            xSemaphoreGive(s_siren);
            xSemaphoreGive(s_LED);
            if (g.position!=POSITION_WINDOW) { // se è gia in posizione 180 sta gia filmando quindi non serve svegliare videocamera
                g.position=POSITION_WINDOW; // modifica la posizione
                xSemaphoreGive(s_servo);
	        }
        }
        else if ( (g.stato == ALARM_TRIGGERED) && (g.position!=POSITION_WINDOW) ){ // sveglio il servo per spostare videocamera
            Serial.println("---- MOVIMENTO RILEVATO da finestra!!! -----");
            g.position=POSITION_WINDOW; // modifica la posizione
            xSemaphoreGive(s_servo);
            Blynk.setProperty(V2, "color", "#ff0000");
            Blynk.logEvent("window_opened");
        }
        else {
            // No action required
        }
	    xSemaphoreGive(mutex);
    }
}


void start_servo()
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


void siren()
{    
    xSemaphoreTake(s_siren, portMAX_DELAY);
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (g.stato == ALARM_TRIGGERED) //deve essere nello stato corretto
    {
        Serial.println("---- SIRENA ACCESA ----!!!");
        digitalWrite(BUZZER_PIN, HIGH);
    }
    else
    {
        digitalWrite(BUZZER_PIN, LOW);
    }
    xSemaphoreGive(mutex);

}

void statusLED()
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

    digitalWrite(RED_LED, LOW);
    digitalWrite(BLUE_LED, LOW);
    digitalWrite(GREEN_LED, LOW);

    xSemaphoreTake(mutex, portMAX_DELAY);
    digitalWrite(g.stato, HIGH);
    
    switch (g.stato)
    {
    case ALARM_OFF:
        Blynk.setProperty(V0, "color", "#0000ff");
        Blynk.setProperty(V0, "label", "ALARM OFF");
        Blynk.setProperty(V1, "color", "#00ff00");
        Blynk.setProperty(V2, "color", "#00ff00");
        Blynk.setProperty(V6, "color", "#00ff00");
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

    default:
        Serial.println("ERRORE: stato del sistema non valido!!!");
        break;
    }
    xSemaphoreGive(mutex);

    
}


void setup()
{
    // Unsubscribe idle task from the Task Watchdog Timer
    esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(CPU_0));

    Serial.begin(9600);
    Serial.println("Inizio il setup");

    // WiFi and Blynk conenction
    uint8_t connection_tries = 0;
    WiFi.begin(ssid, pass);
    Serial.print("Connecting to WiFi...");
    while ( (WiFi.status() != WL_CONNECTED) && (connection_tries < (uint8_t)WIFI_CONNECTION_TRIES) ) {
        delay(500);
        connection_tries++;
        Serial.println(".");
    }
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.disconnect(); // Stop trying to connect
        Serial.println("Not connected - check ssid, pwd and network connection");
    }
    else {
        Serial.println("\nConnected");
        Blynk.config(auth);
        Blynk.connect(BLYNK_CONNECTION_TIMEOUT);

        // Accensione LED su Blynk
        led_alarm_blynk.on();
        led_pir1_blynk.on();
        led_pir2_blynk.on();
        led_window_blynk.on();
    }

    pinMode(PIR1_PIN, INPUT);
    pinMode(PIR2_PIN, INPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(RED_LED, OUTPUT);
    pinMode(GREEN_LED, OUTPUT);
    pinMode(BLUE_LED, OUTPUT);
    pinMode(WINDOW_PIN, INPUT_PULLUP); 

    myservo.attach(SERVO_PIN); 
    g.position=POSITION_DEFAULT; // i due sensori devono stare a posizione 180 (pir) e 0 (window), e lo stato iniziale sarà a metà.
    myservo.write(g.position);
    Blynk.virtualWrite(V3,g.position); // possibile errore('?)


    g.stato = ALARM_OFF;
    g.b_sensor = 0;

    // inizializzo i pin
    for (uint8_t k = 0; k > (uint8_t)LENGTH_PIN; k++)
    {
        user_pin[k] = -1;
    }
    index_pin = 0;

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
    if (s_sensor == NULL)
    {
        s_sensor = xSemaphoreCreateCounting( 3, 0 ); // è un semaforo per 3 sensori
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
    
    // ho aggiunto 200 a tutti (tranne il blynk) i tasksize, quindi per vedere come era prima , basta che fai - 200 a tutti
    xTaskCreatePinnedToCore(
        taskStamp,
        "task-stamp",
        2920, // task overhead: 768 bytes (?)
        NULL,
        1, // priority
        NULL,
        1); // Core

    xTaskCreatePinnedToCore(
        taskPin,
        "task-pin",
        1020,
        NULL,
        1, // priority
        NULL,
        1);

    uint8_t motion_sensor_1_id = 1;
    xTaskCreatePinnedToCore(
        taskMotionSensor,
        "task-motion-sensor1",
        2772,
        (void *)&motion_sensor_1_id,
        1, // priority
        NULL,
        1);

    uint8_t motion_sensor_2_id = 2;
    xTaskCreatePinnedToCore(
        taskMotionSensor,
        "task-motion-sensor2",
        2772,
        (void *)&motion_sensor_2_id,
        1, // priority
        NULL,
        1);
    xTaskCreatePinnedToCore(
        taskWindowSensor,
        "task-window-sensor",
        2740,
        NULL,
        1, // priority
        NULL,
        1);
    xTaskCreatePinnedToCore(
        taskSiren,
        "task-siren",
        948,
        NULL,
        1, // priority
        NULL,
        1);
    xTaskCreatePinnedToCore(
        taskServo,
        "task-servo",
        2772,
        NULL,
        1, // priority
        NULL,
        1);
    xTaskCreatePinnedToCore(
        taskLED,
        "task-LED",
        2860,
        NULL,
        1, // priority
        NULL,
        1);  
        
    if (WiFi.status() == WL_CONNECTED) {
        xTaskCreatePinnedToCore(
            taskBlynk,
            "task-blynk",
            1456,
            NULL,
            1, // messa con priorità maggiore perchè altrimenti dava problemi con primitiva pbuf_free() e abortiva tutto
            NULL,
            0);
    }
       
    
    // First lighting of the LED
    xSemaphoreGive(s_LED);
    Serial.println("Fine setup.");

    // Delete "setup and loop" task
    #ifndef PRINT_STACK_HWM
    vTaskDelete(NULL);
    #endif
}


void taskStamp(void *pvParameters) // This is a task.
{
    (void)pvParameters;
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = 400 / portTICK_PERIOD_MS; // periodo di 200 ms
    xLastWakeTime = xTaskGetTickCount();
    for (;;)
    {
        stamp();
        xLastWakeTime = xTaskGetTickCount();
        #ifdef PRINT_STACK_HWM
        stackStamp = uxTaskGetStackHighWaterMark(NULL);
        #endif
    }
    
}

void taskPin(void *pvParameters)
{
    (void)pvParameters;
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = 50 / portTICK_PERIOD_MS; // periodo di 200 ms
    xLastWakeTime = xTaskGetTickCount();
    for (;;)
    {
        get_pin();
        vTaskDelayUntil(&xLastWakeTime, xFrequency); 
        #ifdef PRINT_STACK_HWM
        stackPIN = uxTaskGetStackHighWaterMark(NULL);
        #endif

        taskYIELD();
    }
}

void taskMotionSensor(void *pvParameters)
{
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = 700 / portTICK_PERIOD_MS; // periodo di 200 ms
    xLastWakeTime = xTaskGetTickCount();
    uint8_t id_pir = *((uint8_t *)pvParameters); 
    Serial.print("id_pir: "); Serial.println(id_pir);
    Serial.print("Il PIN del PIR è: ");
    Serial.println(id_pir);
    uint8_t pin_pir;
    uint8_t virtual_pin;
    char * str_code_blynk;
    // char a[] = "abc"; // equivalent to char a[4] = {'a', 'b', 'c', '\0'};
    
    char str_code_blynk1[15]={'p','i','r','1','_','t','r','i','g','g','e','r','e','d','\0'};
    char str_code_blynk2[15]={'p','i','r','2','_','t','r','i','g','g','e','r','e','d','\0'};
    uint8_t position_pir;
    //uint32_t pin_pir = (id_pir==1) ? PIR1_PIN : PIR2_PIN; // per fare un if con assegnamento piu efficiente
    if (id_pir==1U){
        pin_pir=PIR1_PIN;
        virtual_pin=V1;
        str_code_blynk=str_code_blynk1;
        position_pir=(uint8_t)POSITION_PIR1;
    }
    else{
        pin_pir=PIR2_PIN;
        virtual_pin=V6;
        str_code_blynk=str_code_blynk2;
        position_pir=POSITION_PIR2;
    }
    for (;;)
    {
        start_motion_sensor();
        motion_sensor(pin_pir, virtual_pin, str_code_blynk, position_pir);
        vTaskDelayUntil(&xLastWakeTime, xFrequency); 
        #ifdef PRINT_STACK_HWM
        if (id_pir == 1U) {
            stackMotion1 = uxTaskGetStackHighWaterMark(NULL);
        }
        else {
            stackMotion2 = uxTaskGetStackHighWaterMark(NULL);
        }
        #endif

        taskYIELD();
    }
}

void taskWindowSensor(void *pvParameters) // This is a task.
{
    (void)pvParameters;
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = 150 / portTICK_PERIOD_MS; // periodo di 200 ms
    xLastWakeTime = xTaskGetTickCount();
    for (;;)
    {
        start_window_sensor();
        window_sensor();
        vTaskDelayUntil(&xLastWakeTime, xFrequency); 
        #ifdef PRINT_STACK_HWM
        stackWindow = uxTaskGetStackHighWaterMark(NULL);
        #endif

        taskYIELD();
    }
}



void taskServo(void *pvParameters) // This is a task.
{
    (void)pvParameters;
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = 300 / portTICK_PERIOD_MS; // periodo di 200 ms
    xLastWakeTime = xTaskGetTickCount();
    for (;;)
    {
        start_servo();
        servo();
        vTaskDelayUntil(&xLastWakeTime, xFrequency); 

        #ifdef PRINT_STACK_HWM
        stackServo = uxTaskGetStackHighWaterMark(NULL);
        #endif
    }
}

void taskSiren(void *pvParameters)
{
    (void)pvParameters;
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = 300 / portTICK_PERIOD_MS; // periodo di 200 ms
    xLastWakeTime = xTaskGetTickCount();
    for (;;)
    {
        siren();
        vTaskDelayUntil(&xLastWakeTime, xFrequency); 
        #ifdef PRINT_STACK_HWM
        stackSiren = uxTaskGetStackHighWaterMark(NULL);
        #endif
    }
}

void taskLED(void *pvParameters)
{
    (void)pvParameters;
    TickType_t xLastWakeTime;
    const TickType_t xFrequency = 300 / portTICK_PERIOD_MS; // periodo di 200 ms
    xLastWakeTime = xTaskGetTickCount();
    for (;;)
    {
        statusLED();
        vTaskDelayUntil(&xLastWakeTime, xFrequency); 
        #ifdef PRINT_STACK_HWM
        stackLED = uxTaskGetStackHighWaterMark(NULL);
        #endif
    }
}

void taskBlynk(void *pvParameters)
{
    (void)pvParameters;
    
    for (;;)
    {
        Blynk.run();
        #ifdef PRINT_STACK_HWM
        stackBlynk = uxTaskGetStackHighWaterMark(NULL);
        #endif
        vTaskDelay(400 / portTICK_PERIOD_MS);

        // if (int(xPortGetMinimumEverFreeHeapSize()) != heap) {
            //heap = int(xPortGetMinimumEverFreeHeapSize());
            //Serial.print ("Free Heap: ");
           // Serial.println(heap);
        // }

    }
}

void loop()
{
    #ifdef PRINT_STACK_HWM 
    xSemaphoreTake(mutex, portMAX_DELAY);
    // Serial.print ("Free Heap: ");      Serial.println(xPortGetMinimumEverFreeHeapSize());
    Serial.print("stackIdle0:\t");     Serial.println(uxTaskGetStackHighWaterMark(xTaskGetIdleTaskHandleForCPU(CPU_0)));
    Serial.print("stackIdle1:\t");     Serial.println(uxTaskGetStackHighWaterMark(xTaskGetIdleTaskHandleForCPU(1)));
    Serial.print("stackStamp:\t");     Serial.println(stackStamp);
    Serial.print("stackPIN:\t");       Serial.println(stackPIN);
    Serial.print("stackMotion1:\t");   Serial.println(stackMotion1);
    Serial.print("stackMotion2:\t");   Serial.println(stackMotion2);
    Serial.print("stackWindow:\t");    Serial.println(stackWindow);
    Serial.print("stackServo:\t");     Serial.println(stackServo);
    Serial.print("stackSiren:\t");     Serial.println(stackSiren);
    Serial.print("stackLED:\t");       Serial.println(stackLED);
    Serial.print("stackBlynk:\t");     Serial.println(stackBlynk);
    xSemaphoreGive(mutex);
    vTaskDelay(2000 / portTICK_PERIOD_MS);   
    #endif
}
