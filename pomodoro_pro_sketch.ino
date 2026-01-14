#include <WiFi.h>
#include <ThingSpeak.h>
#include "arduino_secrets.h"

/*
 * ================= CONFIGURATION =================
 * Hardware configuration, global state variables,
 * and timing parameters used across the system.
 */

// RGB LED configuration
// Set to true if the RGB LED uses a common anode
const bool RGB_COMMON_ANODE = true; 



// ---------------- ThingSpeak / Networking ----------------
// WiFi client used by the ThingSpeak library
WiFiClient tsClient;



// Cumulative session-level metrics (milliseconds unless stated otherwise)
unsigned long totalSessionTimeMs = 0;     // Total time since session start
unsigned long totalFocusTimeMs   = 0;     // Total time spent in WORK state
unsigned long totalRestTimeMs    = 0;     // Total time spent in BREAK state



// Total number of completed pomodoros in the current session
int totalPomodoroCount = 0;



// ---------------- RGB LED (state indicator) ----------------
// RGB LED pins used to represent the current state
const int RGB_R = 11;
const int RGB_G = 10; // Green channel unused in this design
const int RGB_B = 9;



// ---------------- Progress LEDs ----------------
// LEDs used to visualise pomodoro progress (1–4)
const int P1 = 2;
const int P2 = 3;
const int P3 = 4;
const int P4 = 5;



// ---------------- User interaction ----------------
// Buzzer for audio feedback
const int BUZZ = 6;

// Push button used to start / resume cycles
const int BTN  = 7;

// ---------------- Timing configuration ----------------
const unsigned long WORK_TIME  = 10UL * 1000UL;   // Work phase duration (ms)
const unsigned long BREAK_TIME = 10UL * 1000UL;   // Break phase duration (ms)



// ================= FINITE STATE MACHINE =================
// The system operates as a simple FSM with three states:
// IDLE  -> waiting for user interaction
// WORK  -> focused work period
// BREAK -> rest period between work sessions
enum State { IDLE, WORK, BREAK };
State currentState = IDLE;

// Timestamp marking when the current phase started
unsigned long phaseStartTime = 0;

// Duration assigned to the current phase

unsigned long phaseDuration  = 0;

// Pomodoro counter used for visual progress (resets every 4)
int pomodoroCount = 0;

// Indicates that the BREAK minimum duration has finished
// and the system is waiting for the user to resume WORK
bool breakAwaitingButton = false;



// ---------------- Break waiting animation ----------------
// Used to blink the RGB LED while waiting for user input
const unsigned long WAIT_BLINK_MS = 400;
unsigned long waitBlinkLast = 0;
bool waitBlinkOn = false;


// ================= DEBUG / SERIAL LOGGING =================
// Converts FSM states to human-readable strings
const char* stateName(State s) {
  switch (s) {
    case IDLE:  return "IDLE";
    case WORK:  return "WORK";
    case BREAK: return "BREAK";
    default:    return "???";
  }
}

// Logs state transitions and key variables to the serial monitor
// This is useful for debugging and validating system behaviour
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



// ================= UTILITIES =================
/*
 * Helper functions used across the system.
 * This section includes:
 *  - time conversion helpers
 *  - RGB LED control
 *  - buzzer feedback
 *  - button debouncing
 *  - progress LEDs
 *  - ThingSpeak metric preparation
 */

// ---------------- Time conversion helpers ----------------
// Converts milliseconds to seconds (used for data transmission)
float msToSeconds(unsigned long ms) {
  return ms / 1000.0;
}

float msToMinutes(unsigned long ms) {
  return ms / 60000.0;
}


// ---------------- RGB LED control ----------------
// Sets the RGB LED colour based on the current system state.
// Logic is inverted automatically if a common anode LED is used.
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



// ---------------- Buzzer feedback ----------------
// Emits a short audible signal to indicate phase transitions.
// A blocking delay is acceptable here due to the short duration.
void beep(int ms = 200) {
  digitalWrite(BUZZ, HIGH);
  delay(ms);
  digitalWrite(BUZZ, LOW);
}



// ---------------- Button handling ----------------
// Debounced button press detection.
// Returns true only on a stable HIGH -> LOW transition.
// Includes a cooldown period to avoid double triggering.
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

  // Track raw state changes
  if (reading != lastReading) {
    lastReading = reading;
    lastChange = millis();
  }

  // Accept new state if stable for DEBOUNCE_MS
  if ((millis() - lastChange) >= DEBOUNCE_MS && reading != stableState) {
    stableState = reading;

     // Trigger event on button press (LOW)
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



// ---------------- Progress LEDs ----------------
// Updates the progress LEDs to reflect the number of pomodoros
// completed within the current block (1–4).
void updateProgressLEDs() {
  digitalWrite(P1, pomodoroCount >= 1 ? HIGH : LOW);
  digitalWrite(P2, pomodoroCount >= 2 ? HIGH : LOW);
  digitalWrite(P3, pomodoroCount >= 3 ? HIGH : LOW);
  digitalWrite(P4, pomodoroCount >= 4 ? HIGH : LOW);
}



// ---------------- ThingSpeak metric preparation ----------------
// These functions only set the corresponding fields.
// Data is transmitted explicitly via sendAllMetrics().

// Total number of completed pomodoros
void setTotalPomodoroCount(int totalPomodoros) {
  ThingSpeak.setField(1, totalPomodoros);
}

// Total session time (seconds since system start)
void setTotalWorkingTime(unsigned long totalTimeMs) {
  ThingSpeak.setField(2, msToSeconds(totalTimeMs));
}

// Total focused work time (cumulative)
void setTotalFocusTime(unsigned long focusTimeMs) {
  ThingSpeak.setField(3, msToSeconds(focusTimeMs));
}

// Total resting time (cumulative)
void setTotalRestingTime(unsigned long restingTimeMs) {
  ThingSpeak.setField(4, msToSeconds(restingTimeMs));
}


// ---------------- Data transmission ----------------
// Sends a complete snapshot of all prepared metrics to ThingSpeak.
// This function must be called only at meaningful state transitions.
void sendAllMetrics() {
  ThingSpeak.writeFields(channelID, writeAPIKey);
}




// ================= STATE ENTRY FUNCTIONS =================
/*
 * State transition handlers for the finite state machine.
 * Each function is responsible for:
 *  - updating the current state
 *  - initialising timing information
 *  - configuring visual feedback (RGB LED)
 *  - resetting state-specific flags
 */

// ---------------- IDLE ----------------
// Entered when the system is waiting for user interaction.
// All outputs are disabled and internal flags are reset.
void enterIdle() {
  currentState = IDLE;
  setRGB(false, false, false);      // RGB LED off
  breakAwaitingButton = false;      // No pending break continuation
  logState("ENTER");
}


// ---------------- WORK ----------------
// Entered when a focused work session starts.
// Initialises the work timer and updates the RGB LED accordingly.
void enterWork() {
  currentState = WORK;
  phaseStartTime = millis();        // Mark start of WORK phase
  phaseDuration  = WORK_TIME;       // Asign work duration
  pomodoroCount++;
  if (pomodoroCount > 4) pomodoroCount = 4;
  updateProgressLEDs();
  setRGB(true, false, false);       // Red indicates focus/work
  breakAwaitingButton = false;      // Reset break-related flag
  logState("ENTER");
}


// ---------------- BREAK ----------------
// Entered after a work session completes.
// Starts the break timer and prepares the system to wait for user input
// once the minimum break duration has elapsed.
void enterBreak() {
  currentState = BREAK;
  phaseStartTime = millis();    // Mark start of BREAK phase
  phaseDuration  = BREAK_TIME;  // Assign break duration
  setRGB(false, false, true);   // Blue indicates rest/break
  breakAwaitingButton = false;  // Will be set true after break timer


 // Initialise waiting animation for user input
  waitBlinkLast = millis();
  waitBlinkOn = true;
  
  logState("ENTER");
}

// ================= SETUP =================
/*
 * System initialisation.
 * This function is executed once at boot and is responsible for:
 *  - establishing WiFi connectivity
 *  - initialising the ThingSpeak client
 *  - configuring I/O pins
 *  - resetting system state
 */
void setup() {

  // ---------------- Network initialisation ----------------
  // Connect to WiFi using credentials stored in arduino_secrets.h
  WiFi.begin(SECRET_SSID, SECRET_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);                       // Wait until connection is established
  }

  // Initialise ThingSpeak with the configured WiFi client
  ThingSpeak.begin(tsClient);


  // ---------------- Serial debugging ----------------
  // Serial output used for debugging and behaviour verification
  Serial.begin(115200);
  delay(300);


  // ---------------- Hardware configuration ----------------
  // RGB LED pins
  pinMode(RGB_R, OUTPUT);
  pinMode(RGB_G, OUTPUT);
  pinMode(RGB_B, OUTPUT);

  // Progress LEDs
  pinMode(P1, OUTPUT);
  pinMode(P2, OUTPUT);
  pinMode(P3, OUTPUT);
  pinMode(P4, OUTPUT);

  // Buzzer and user button
  pinMode(BUZZ, OUTPUT);
  pinMode(BTN, INPUT_PULLUP);


  // ---------------- Initial system state ----------------
  // Reset pomodoro progress and visual indicators
  pomodoroCount = 0;
  updateProgressLEDs();


  // Start the system in IDLE state
  enterIdle();

  // Log system boot event
  logState("BOOT");
}

// ================= LOOP =================
void loop() {
  unsigned long now = millis();
  
  // ---------------- User interaction ----------------
  // Button press events are handled with priority over time-based logic.
  // Depending on the current state, the button can:
  //  - start a new WORK session from IDLE
  //  - resume WORK after a BREAK
  //  - reset the system back to IDLE
  if (buttonPressed()) {
    Serial.print("[");
    Serial.print(millis());
    Serial.print(" ms] BTN event in ");
    Serial.println(stateName(currentState));

    // ----- IDLE -> WORK -----
    // A button press in IDLE starts a new work session
    if (currentState == IDLE) {
      pomodoroCount = 0;
      updateProgressLEDs();
      enterWork();
    }


    // ----- BREAK -> WORK -----
    // If the break has finished and the system is waiting for the user,
    // the button resumes work and closes the break period
    else if (currentState == BREAK && breakAwaitingButton) {

        unsigned long breakDurationMs = millis() - phaseStartTime;
        totalRestTimeMs += breakDurationMs;

        
      if (pomodoroCount >= 4) {
        pomodoroCount = 0;
        updateProgressLEDs();
      }      
      enterWork();
    }
     // ----- Any other case -> IDLE -----
     // A button press in any unexpected state resets the system
     else {
      enterIdle();
     }

     // Important: exit loop to avoid executing time-based logic
     // in the same iteration as a user event
     return; // ✅ CLAVE: no ejecutes la lógica de tiempos en la misma vuelta
  }



  // --------------- WORK STATE LOGIC ---------------
  // Handles the completion of a focused work session.
  // Metrics are calculated and transmitted only when a full WORK cycle ends.
  if (currentState == WORK) {
    if (millis() - phaseStartTime >= phaseDuration) {   

      // Audible feedback to signal the end of the WORK phase
      beep(); 

      // ---------------- Metric calculation ----------------
      // A pomodoro is counted only after a full WORK cycle completes
      totalPomodoroCount++;

      // Measure actual focused work duration
      unsigned long workDurationMs = millis() - phaseStartTime;
      totalFocusTimeMs += workDurationMs;

      // ---------------- Metric preparation ----------------
      // Prepare a complete snapshot of the current session state
      setTotalPomodoroCount(totalPomodoroCount);
      setTotalFocusTime(totalFocusTimeMs);
      setTotalRestingTime(totalRestTimeMs);
      setTotalWorkingTime(totalSessionTimeMs);

      // ---------------- Data transmission ----------------
      // Send all prepared metrics as a single update to ThingSpeak
      sendAllMetrics();

      // Limit visual pomodoro counter to the available LEDs (1–4)
      if (pomodoroCount > 4) pomodoroCount = 4;
        updateProgressLEDs();

        // Transition to BREAK state
        enterBreak();
      }
  }

    // --------------- BREAK STATE LOGIC ---------------
    // Handles behaviour during the break period.
    // The break has two distinct phases:
    //  1) Minimum break duration (timer-based)
    //  2) Waiting for user input to resume work
    else if (currentState == BREAK) {

      // ----- Minimum break duration completed -----
      // Once the minimum break time has elapsed, the system notifies the user
      // and enters a waiting state until the button is pressed.
      if (!breakAwaitingButton && (millis() - phaseStartTime >= phaseDuration)) {


        // Audible feedback to signal end of minimum break
        beep();

        // Update total session time (uptime-based)
        totalSessionTimeMs = millis();

        // ---------------- Metric preparation ----------------
        // Send a snapshot of the current session state.
        // Note: real break duration is measured later, when the user resumes work.
        setTotalPomodoroCount(totalPomodoroCount);
        setTotalFocusTime(totalFocusTimeMs);
        setTotalRestingTime(totalRestTimeMs);
        setTotalWorkingTime(totalSessionTimeMs);

        // ---------------- Data transmission ----------------
        sendAllMetrics();

        // System is now waiting for explicit user interaction
        breakAwaitingButton = true;
        logState("BREAK finished (waiting for button)");
      }
  // ----- Waiting for user input -----
  // While waiting, the RGB LED blinks to indicate that the system
  // is ready to resume work.    
  if (breakAwaitingButton) {
      if (now - waitBlinkLast >= WAIT_BLINK_MS) {
        waitBlinkLast = now;
        waitBlinkOn = !waitBlinkOn;

        if (waitBlinkOn) setRGB(false, false, true);   // Blue ON
        else             setRGB(false, false, false);  // LED OFF
      }
    }
  }
}
