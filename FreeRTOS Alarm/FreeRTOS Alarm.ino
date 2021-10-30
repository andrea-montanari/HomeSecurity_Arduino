#include <Arduino_FreeRTOS.h>
#include <semphr.h>
#include <Keypad.h>

#define PIN 1
#define STAMP 0
#define LENGTH_PIN 4

SemaphoreHandle_t mutex = NULL;
SemaphoreHandle_t s_pin = NULL;
SemaphoreHandle_t s_stamp = NULL;

void taskPin(void *pvParameters);
void taskStamp(void *pvParameters);

struct gestore
{
  int stato;
  bool alarm;          // mode: DISABLED/ENABLED
  bool siren;         // ON/OFF per far suonare l'allarme
}g;
char user_pin[LENGTH_PIN];
int index_pin; // per scorrere array pin
char true_system_pin[4] = {'0', '0', '0', '0'};
const byte ROWS = 4; //four rows
const byte COLS = 4; //four columns
//define the cymbols on the buttons of the keypads
char hexaKeys[ROWS][COLS] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}};
byte rowPins[ROWS] = {9, 8, 7, 6}; //connect to the row pinouts of the keypad
byte colPins[COLS] = {5, 4, 3, 2}; //connect to the column pinouts of the keypad
Keypad customKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);

int b_pin, b_stamp;
int stato;

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
void start_pin(void *pvParameters)
{
    xSemaphoreTake(mutex, (TickType_t)100);
    //Serial.println("Sono lo start_pin");
    if (g.stato == PIN)
    {
        xSemaphoreGive(s_pin);
    }
    else
        b_pin++;
    xSemaphoreGive(mutex);
    xSemaphoreTake(s_pin, (TickType_t)100); //mi blocco qui nel caso
}

void get_pin()
{
    char customKey = customKeypad.getKey();
    while (!customKey)
    { // se ho preso input
        customKey = customKeypad.getKey();
        //Serial.println(customKey);
    }
    xSemaphoreTake(mutex, (TickType_t)100);
    user_pin[index_pin] = customKey; // possibile race conditions su shared variable (per questo usato mutex)
    index_pin++;                     //aggiorno index_pin
    xSemaphoreGive(mutex);
}

void end_pin(void *pvParameters)
{
    //Serial.println("Sono lo end_pin");
    xSemaphoreTake(mutex, (TickType_t)100);
    g.stato = STAMP;
    if (b_stamp)
    {
        b_stamp--;
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
        b_stamp++;
    xSemaphoreGive(mutex);
    xSemaphoreTake(s_stamp, (TickType_t)100); // mi blocco qui nel caso
}

// Qui la stampa è fatta su seriale (potremmo mantenerlo come un task a parte(?))
void stamp()
{
    print_user_pin();
    print_alarm_state();
    if (index_pin == 4)
    {
        bool valid_pin = is_pin_valid(user_pin, true_system_pin); // check del pin con "0000"
        if (valid_pin)
        {
            Serial.print("Il pin inserito è GIUSTO ");
            Serial.println(user_pin);
            xSemaphoreTake(mutex, (TickType_t)100);
            // il pin è giusto, quindi bisogna cambiare lo stato dell'allarme: nel caso in cui sia off->on nel caso sia on->off
            if (g.alarm)g.alarm=false;              //anche qui g.alarm è una var condivisa, quindi possibili race condition
            else g.alarm=true;
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
    xSemaphoreTake(mutex, (TickType_t)100);
    //Serial.println("Sono lo end_stamp");
    g.siren = PIN;
    if (b_pin)
    {
        b_pin--;
        xSemaphoreGive(s_pin);
    }
    xSemaphoreGive(mutex);
}

void setup()
{
    Serial.begin(9600);
    Serial.println("Inizio il setup");

    g.stato = PIN;
    g.alarm=false;
    g.siren=false;
    // inizializzo i pin
    for (int k = 0; k > LENGTH_PIN; k++)
    {
        user_pin[k] = -1;
    }
    index_pin = 0;
    Serial.println(user_pin[1], DEC);
    Serial.println(user_pin[0], DEC);


    //print_user_pin();
    b_pin = b_stamp = 0;
    
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
    xTaskCreate(
        taskStamp, "task-stamp", 128, NULL, 1 // priority
        ,
        NULL);

    xTaskCreate(
        taskPin, "task-pin", 128, NULL, 10 // priority
        ,
        NULL);
        
}

void taskStamp(void *pvParameters) // This is a task.
{
    (void)pvParameters;

    for (;;)
    {
        start_stamp(pvParameters);
        vTaskDelay(100 / portTICK_PERIOD_MS); // wait for one second
        stamp();
        vTaskDelay(100 / portTICK_PERIOD_MS);
        end_stamp(pvParameters);
    }
}

void taskPin(void *pvParameters)
{
    (void)pvParameters;
    //Serial.begin(4800);
    for (;;)
    {
        start_pin(pvParameters);
        vTaskDelay(100 / portTICK_PERIOD_MS); // wait for one second
        get_pin();
        vTaskDelay(100 / portTICK_PERIOD_MS); // wait for one second
        end_pin(pvParameters);
    }
}

void loop()
{
    //empty loop
}
