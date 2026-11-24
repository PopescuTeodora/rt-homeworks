// Semafor pentru pietoni, cu buton, pe ESP32-C3 Super Mini.
//
// Placa este controlerul semaforului; lămpile sunt desenate de dashboard, care
// le aprinde după mesajele "lights" publicate de placă. Patru activități:
//
//   ISR buton BOOT ----+
//                      +--(coada butonului)--> task sporadic
//   command_received --+   (BUTTON)                 |
//                                                   v
//                                         [ starea semaforului ] <-- mutex
//                                            ^          |
//                                            |          v
//                         task periodic de control   task periodic de ieșire
//                         (fazele, T = 100 ms)       (lămpile, T = 500 ms) --> lights
//
// Gata implementate (nu trebuie modificate):
//   config.h         setările: Wi-Fi, broker, STUDENT, pinii, perioadele, duratele fazelor
//   connection.h/cpp Wi-Fi, ora exactă (SNTP), clientul MQTT, mqtt_publish(), epoch_ms()
//
// Așa cum este, schița se conectează la Wi-Fi și la broker și afișează pe
// Serial comenzile primite. Restul îl scrieți voi: TODO 0 ... TODO 3.
//
// Placa: ESP32C3 Dev Module, USB CDC On Boot: Enabled. Serial Monitor: 115200 baud.

#include "config.h"
#include "connection.h"

// Fazele semaforului (vezi tabelul și diagrama din README.md). Numele din
// PHASE_NAMES sunt cele din mesajul "lights", în aceeași ordine ca în enum.
enum Phase { CAR_GREEN, CAR_YELLOW, ALL_RED, PED_GREEN };
const char* const PHASE_NAMES[] = {"CAR_GREEN", "CAR_YELLOW", "ALL_RED", "PED_GREEN"};

// Culorile unei lămpi, cu numele din mesajul "lights".
enum Light { RED, YELLOW, GREEN };
const char* const LIGHT_NAMES[] = {"RED", "YELLOW", "GREEN"};

// TODO 0 (c): STAREA SEMAFORULUI, partajată de taskuri și protejată de un
//   mutex (xSemaphoreCreateMutex). De exemplu un struct cu:
//   - faza curentă
//   - momentul intrării în faza curentă (millis()), din care se calculează
//     cât a durat faza și cât mai are
//   - cererea pietonului (lampa AȘTEPTAȚI): setată de taskul sporadic,
//     ștearsă de taskul de control la intrarea în PED_GREEN
//   Mutexul se ține cât mai puțin: în el doar citiți sau scrieți câmpurile
//   (eventual copiați starea într-o variabilă locală). Fără mqtt_publish(),
//   Serial sau așteptări cât timp țineți mutexul.


struct SemState {
    Phase phase;
    uint32_t phaseEnteredMs;
    bool pedRequest;
};

//o apasare de buton
struct ButtonPress {
    uint32_t atMs;
};


SemState state;
SemaphoreHandle_t stateMutex;
QueueHandle_t buttonQueue;





// TODO 1: TASK PERIODIC DE CONTROL, cu perioada CONTROL_PERIOD_MS.
//   La fiecare job, cu mutexul luat, decide dacă faza curentă s-a terminat și
//   trece în faza următoare, după tabelul din README.md:
//     CAR_GREEN  -> CAR_YELLOW  după CAR_GREEN_MIN_MS, doar dacă există o cerere
//     CAR_YELLOW -> ALL_RED     după CAR_YELLOW_MS
//     ALL_RED    -> PED_GREEN   după ALL_RED_MS; aici se șterge cererea pietonului
//     PED_GREEN  -> CAR_GREEN   după PED_GREEN_MS
//   La fiecare schimbare de fază notați momentul intrării în faza nouă.
//   Taskul nu publică nimic: doar actualizează starea. După ce eliberează
//   mutexul, afișează pe Serial faza nouă, dacă s-a schimbat.
//   Eliberările respectă grila r_k = r_0 + kT: așteptare absolută (xTaskDelayUntil).
//   Durata unei faze se măsoară cu un timp monoton (millis()), nu cu epoch_ms().


void control_task(void* pv) {
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        bool changed = false;
        Phase oldPhase, newPhase;

        xSemaphoreTake(stateMutex, portMAX_DELAY);     // luăm cheia

        uint32_t now = millis();
        uint32_t elapsed = now - state.phaseEnteredMs; // cât a durat faza până acum
        oldPhase = state.phase;

        switch (state.phase) {
            case CAR_GREEN:
                // trece mai departe doar dacă a trecut verdele minim ȘI cineva a apăsat
                if (elapsed >= CAR_GREEN_MIN_MS && state.pedRequest) {
                    state.phase = CAR_YELLOW;
                    changed = true;
                }
                break;
            case CAR_YELLOW:
                if (elapsed >= CAR_YELLOW_MS) {
                    state.phase = ALL_RED;
                    changed = true;
                }
                break;
            case ALL_RED:
                if (elapsed >= ALL_RED_MS) {
                    state.phase = PED_GREEN;
                    state.pedRequest = false;          // cererea se șterge aici
                    changed = true;
                }
                break;
            case PED_GREEN:
                if (elapsed >= PED_GREEN_MS) {
                    state.phase = CAR_GREEN;
                    changed = true;
                }
                break;
        }

        if (changed) {
            state.phaseEnteredMs = now;                // notăm momentul intrării în faza nouă
        }
        newPhase = state.phase;

        xSemaphoreGive(stateMutex);                    // dăm cheia înapoi

        // Serial abia după ce am dat cheia înapoi
        if (changed) {
            Serial.printf("[control] %s -> %s\n", PHASE_NAMES[oldPhase], PHASE_NAMES[newPhase]);
        }

        xTaskDelayUntil(&last, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
    }
}
// TODO 2: TASK PERIODIC DE IEȘIRE, cu perioada OUTPUT_PERIOD_MS.
//   La fiecare job:
//   - copiază starea (cu mutexul luat, cât mai scurt)
//   - stabilește culorile celor două lămpi din faza curentă (tabelul din README.md)
//   - calculează secundele rămase din verdele pietonilor: în PED_GREEN,
//     rotunjite în sus (7,2 s rămase -> 8); 0 în celelalte faze
//   - publică: mqtt_publish("lights", "deviceId;timestamp;faza;masini;pietoni;asteptati;secunde")
//     de exemplu "esp32-a1b2c3;1727600000000;PED_GREEN;RED;GREEN;0;8"
//   - aprinde LED-ul de pe placă (LED_PIN, aprins pe LOW) cât timp lampa
//     AȘTEPTAȚI este aprinsă
//   Așteptare absolută (xTaskDelayUntil), ca și la taskul de control.



void output_task(void* pv) {
    TickType_t last = xTaskGetTickCount();   // „ceasul” pentru așteptarea absolută
    for (;;) {                               // buclă infinită: taskul nu se termină niciodată

        // 1. Copiem starea într-o variabilă locală
        SemState s;
        uint32_t now;
        xSemaphoreTake(stateMutex, portMAX_DELAY);   // luăm cheia
        s = state;
        now = millis();
        xSemaphoreGive(stateMutex);                  // dăm cheia înapoi

        // 2. Stabilim lămpile, după fază
        Light cars, peds;
        switch (s.phase) {
            case CAR_GREEN:  cars = GREEN;  peds = RED;   break;
            case CAR_YELLOW: cars = YELLOW; peds = RED;   break;
            case ALL_RED:    cars = RED;    peds = RED;   break;
            default:         cars = RED;    peds = GREEN; break;   // PED_GREEN
        }

        // 3. Secundele rămase (doar în PED_GREEN), rotunjite în sus
        int secs = 0;
        if (s.phase == PED_GREEN) {
            uint32_t elapsed = now - s.phaseEnteredMs;
            uint32_t remaining = (elapsed >= PED_GREEN_MS) ? 0 : (PED_GREEN_MS - elapsed);
            secs = (remaining + 999) / 1000;
        }

        // 4. Construim și publicăm mesajul
        char msg[128];
        snprintf(msg, sizeof(msg), "%s;%lld;%s;%s;%s;%d;%d",
                 DEVICE_ID, (long long)epoch_ms(),
                 PHASE_NAMES[s.phase], LIGHT_NAMES[cars], LIGHT_NAMES[peds],
                 s.pedRequest ? 1 : 0, secs);
        mqtt_publish("lights", msg);
        //Serial.printf("[output] %s\n", msg);

        // 5. LED-ul plăcii: aprins (LOW) cât timp lampa AȘTEPTAȚI e aprinsă
        digitalWrite(LED_PIN, s.pedRequest ? LOW : HIGH);

        // 6. Așteptăm până la următoarea eliberare
        xTaskDelayUntil(&last, pdMS_TO_TICKS(OUTPUT_PERIOD_MS));
    }
}


// TODO 3: TASK SPORADIC, tratează apăsările butonului pentru pietoni.
//   Apăsările vin din două surse, în aceeași coadă (coada butonului):
//   - butonul BOOT de pe placă, printr-o rutină de întrerupere (ISR, mai jos)
//   - butonul virtual din dashboard: comanda "BUTTON", pusă în coadă de
//     command_received()
//   Taskul:
//   - așteaptă o apăsare din coada butonului
//   - dacă de la ultima apăsare acceptată au trecut mai puțin de
//     MIN_INTERARRIVAL_MS, o respinge (mesaj pe Serial). Așa se filtrează și
//     contactele imperfecte ale butonului (bounce), care produc mai multe
//     întreruperi la o singură apăsare.
//   - altfel înregistrează cererea pietonului în starea semaforului (cu mutexul)
//     și afișează pe Serial că a acceptat-o
//   Momentul sosirii se ia când apăsarea intră în coadă (în ISR sau în
//   command_received()), nu când o preia taskul, și se pune în elementul cozii.

void button_task(void* pv) {
    ButtonPress p;
    uint32_t lastAccepted = 0;
    bool hasAccepted = false;

    for (;;) {
        // stă blocat aici până sosește o apăsare
        xQueueReceive(buttonQueue, &p, portMAX_DELAY);

        // prea devreme față de ultima apăsare acceptată? o respingem
        if (hasAccepted && (p.atMs - lastAccepted) < MIN_INTERARRIVAL_MS) {
            Serial.println("[button] apasare respinsa (prea devreme)");
            continue;
        }

        lastAccepted = p.atMs;
        hasAccepted = true;

        // înregistrăm cererea pietonului în stare
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        state.pedRequest = true;
        xSemaphoreGive(stateMutex);

        Serial.println("[button] cerere acceptata");
    }
}

// TODO 3: RUTINA DE ÎNTRERUPERE a butonului BOOT, înregistrată în setup() cu
//   attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), button_isr, FALLING).
//   Scurtă: pune o apăsare în coada butonului cu xQueueSendFromISR() și atât.
//   Fără Serial, fără mqtt_publish(), fără mutex. Se declară cu IRAM_ATTR:
//
//   void IRAM_ATTR button_isr(void) { ... }

// Apelată de clientul MQTT pentru fiecare comandă primită (vezi connection.h).
void command_received(const char* text) {
    Serial.printf("[mqtt] comandă primită: %s\n", text);
    // TODO 3: "BUTTON" merge în coada butonului (o apăsare, la fel ca cea din
    //   ISR), fără să vă blocați (timeout 0). Celelalte comenzi se ignoră:
    //   în această temă placa nu are alte comenzi.

    if (strcmp(text, "BUTTON") == 0) {
        ButtonPress p;
        p.atMs = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (xQueueSend(buttonQueue, &p, 0) != pdPASS) {   // timeout 0: nu blocăm
            Serial.println("[mqtt] coada butonului plina");
        }
    }
}

void IRAM_ATTR button_isr(void) {
    ButtonPress press;
    press.atMs = xTaskGetTickCountFromISR() * portTICK_PERIOD_MS;   // momentul apăsării

    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(buttonQueue, &press, &woken);   // punem apăsarea în coadă
    portYIELD_FROM_ISR(woken);                        // dacă s-a trezit un task mai important, rulează imediat
}



void setup() {
    Serial.begin(115200);
    // Fără așteptare la scriere: cu placa la USB, dar fără Serial Monitor deschis,
    // fiecare Serial.printf() ar aștepta până la 2 s să fie citit și ar întârzia
    // taskul care scrie. Așa, textul pe care nu-l citește nimeni se pierde.
    Serial.setTxTimeoutMs(0);
    uint32_t t = millis();
    while (!Serial && millis() - t < 3000) delay(10);  // așteptăm Serial Monitor, cel mult 3 s
    delay(200);
    Serial.println();
    Serial.printf("=== tema 03: STUDENT %s, DEVICE_ID %s ===\n", STUDENT, DEVICE_ID);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);  // LED stins
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    wifi_connect();
    time_sync();

    // TODO 0 (a): creați mutexul stării (xSemaphoreCreateMutex) și coada
    //   butonului (xQueueCreate). Înainte de mqtt_start(): de acolo încolo pot
    //   sosi comenzi, iar command_received() pune "BUTTON" în coadă.
    //   Notați și momentul intrării în faza inițială (CAR_GREEN).

    stateMutex  = xSemaphoreCreateMutex();
    buttonQueue = xQueueCreate(8, sizeof(ButtonPress));

    state.phase = CAR_GREEN;
    state.phaseEnteredMs = millis();
    state.pedRequest = false;

    mqtt_start();

    // TODO 0 (b): porniți cele trei taskuri (xTaskCreate), apoi înregistrați
    //   întreruperea butonului (attachInterrupt). Alegeți prioritățile între
    //   2 și 19 (peste loopTask, sub taskurile sistemului); pentru cele două
    //   taskuri periodice, după regula Rate Monotonic. Stiva, în octeți: cel puțin 4096.
     xTaskCreate(control_task, "control", 4096, NULL, 6, NULL);
    xTaskCreate(output_task,  "output",  4096, NULL, 5, NULL);
    xTaskCreate(button_task,  "button",  4096, NULL, 4, NULL);
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), button_isr, FALLING);
}

void loop() {
    // Totul se face în taskuri: loop() nu are nimic de făcut.
    delay(1000);
}
