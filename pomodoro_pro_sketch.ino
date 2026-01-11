// ================= CONFIG =================
const bool RGB_COMMON_ANODE = true; // true si tu RGB es ánodo común

// RGB (estado)
const int RGB_R = 11;
const int RGB_G = 10; // no lo usamos (siempre LOW)
const int RGB_B = 9;

// Progreso
const int P1 = 5;
const int P2 = 4;
const int P3 = 3;
const int P4 = 2;

// Buzzer y botón
const int BUZZ = 6;
const int BTN  = 7;

// === TIEMPOS DE PRUEBA ===
const unsigned long WORK_TIME  = 5UL * 1000UL;   // 5 s
const unsigned long BREAK_TIME = 3UL * 1000UL;   // 3 s

// ================= FSM =================
enum State { IDLE, WORK, BREAK };
State currentState = IDLE;

unsigned long phaseStartTime = 0;
unsigned long phaseDuration  = 0;

int pomodoroCount = 0;

// cuando el break termina, esperamos botón para volver a WORK
bool breakAwaitingButton = false;

// === Animación "esperando botón" ===
const unsigned long WAIT_BLINK_MS = 400;
unsigned long waitBlinkLast = 0;
bool waitBlinkOn = false;


// ================= DEBUG SERIAL =================
const char* stateName(State s) {
  switch (s) {
    case IDLE:  return "IDLE";
    case WORK:  return "WORK";
    case BREAK: return "BREAK";
    default:    return "???";
  }
}

void logState(const char* reason) {
  Serial.print("[");
  Serial.print(millis());
  Serial.print(" ms] ");
  Serial.print(reason);
  Serial.print(" -> state=");
  Serial.print(stateName(currentState));
  Serial.print(" | pomodoros=");
  Serial.print(pomodoroCount);
  Serial.print(" | awaitingBtn=");
  Serial.println(breakAwaitingButton ? "true" : "false");
}

// ================= UTILIDADES =================
void setRGB(bool r, bool g, bool b) {
  if (RGB_COMMON_ANODE) {
    digitalWrite(RGB_R, r ? LOW : HIGH);
    digitalWrite(RGB_G, g ? LOW : HIGH);
    digitalWrite(RGB_B, b ? LOW : HIGH);
  } else {
    digitalWrite(RGB_R, r ? HIGH : LOW);
    digitalWrite(RGB_G, g ? HIGH : LOW);
    digitalWrite(RGB_B, b ? HIGH : LOW);
  }
}

void beep(int ms = 200) {
  digitalWrite(BUZZ, HIGH);
  delay(ms);
  digitalWrite(BUZZ, LOW);
}

// bool buttonPressed() {
//   static bool last = HIGH;
//   bool current = digitalRead(BTN);
//   bool pressed = (last == HIGH && current == LOW);
//   last = current;
//   return pressed;
// }

bool buttonPressed() {
  const unsigned long DEBOUNCE_MS = 30;
  const unsigned long COOLDOWN_MS = 250; // evita doble evento pegado

  static int stableState = HIGH;      // estado estable aceptado
  static int lastReading = HIGH;      // lectura cruda
  static unsigned long lastChange = 0;
  static unsigned long lastEvent  = 0;

  int reading = digitalRead(BTN);

  if (reading != lastReading) {
    lastReading = reading;
    lastChange = millis();
  }

  // aceptar cambio si se mantuvo estable DEBOUNCE_MS
  if ((millis() - lastChange) >= DEBOUNCE_MS && reading != stableState) {
    stableState = reading;

    // evento: transición estable a pulsado (LOW)
    if (stableState == LOW) {
      unsigned long now = millis();
      if (now - lastEvent >= COOLDOWN_MS) {
        lastEvent = now;
        return true;
      }
    }
  }

  return false;
}


void updateProgressLEDs() {
  digitalWrite(P1, pomodoroCount >= 1 ? HIGH : LOW);
  digitalWrite(P2, pomodoroCount >= 2 ? HIGH : LOW);
  digitalWrite(P3, pomodoroCount >= 3 ? HIGH : LOW);
  digitalWrite(P4, pomodoroCount >= 4 ? HIGH : LOW);
}

// ================= ENTRADAS DE ESTADO =================
void enterIdle() {
  currentState = IDLE;
  setRGB(false, false, false);
  breakAwaitingButton = false;
  logState("ENTER");
}

void enterWork() {
  currentState = WORK;
  phaseStartTime = millis();
  phaseDuration  = WORK_TIME;
  setRGB(true, false, false); // ROJO
  breakAwaitingButton = false;
  logState("ENTER");
}

void enterBreak() {
  currentState = BREAK;
  phaseStartTime = millis();
  phaseDuration  = BREAK_TIME;
  setRGB(false, false, true); // AZUL
  breakAwaitingButton = false;

 // reset animación
  waitBlinkLast = millis();
  waitBlinkOn = true;
  
  logState("ENTER");
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(RGB_R, OUTPUT);
  pinMode(RGB_G, OUTPUT);
  pinMode(RGB_B, OUTPUT);

  pinMode(P1, OUTPUT);
  pinMode(P2, OUTPUT);
  pinMode(P3, OUTPUT);
  pinMode(P4, OUTPUT);

  pinMode(BUZZ, OUTPUT);
  pinMode(BTN, INPUT_PULLUP);

  pomodoroCount = 0;
  updateProgressLEDs();
  enterIdle();

  logState("BOOT");
}

// ================= LOOP =================
void loop() {
  unsigned long now = millis();

  // Botón
  if (buttonPressed()) {
    Serial.print("[");
    Serial.print(millis());
    Serial.print(" ms] BTN event in ");
    Serial.println(stateName(currentState));

    if (currentState == IDLE) {
      pomodoroCount = 0;
      updateProgressLEDs();
      enterWork();
    }
    else if (currentState == BREAK && breakAwaitingButton) {
      if (pomodoroCount >= 4) {
        pomodoroCount = 0;
        updateProgressLEDs();
      }
      enterWork();
    }
    else {
      enterIdle();
    }

    return; // ✅ CLAVE: no ejecutes la lógica de tiempos en la misma vuelta
  }

  // Lógica por estado
  if (currentState == WORK) {
    if (millis() - phaseStartTime >= phaseDuration) {   // (puedes usar millis() directamente)
      beep();
      pomodoroCount++;
      if (pomodoroCount > 4) pomodoroCount = 4;
      updateProgressLEDs();
      enterBreak();
    }
  }
  else if (currentState == BREAK) {
    if (!breakAwaitingButton && (millis() - phaseStartTime >= phaseDuration)) {
      beep();
      breakAwaitingButton = true;
      logState("BREAK finished (waiting for button)");
    }
if (breakAwaitingButton) {
    if (now - waitBlinkLast >= WAIT_BLINK_MS) {
      waitBlinkLast = now;
      waitBlinkOn = !waitBlinkOn;

      if (waitBlinkOn) setRGB(false, false, true);   // azul
      else             setRGB(false, false, false);  // apagado
    }
  }
}
    
  }
