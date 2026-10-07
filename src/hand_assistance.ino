/*
 * ==============================================================================
 * Project: ESP8266 Servo-Based Hand Assistance System (Wi-Fi AP & Web Server)
 * Description:
 *   - Creates a local Wi-Fi Hotspot (SoftAP: "ESP8266-Hand-Assistance").
 *   - Hosts an embedded responsive Web Server dashboard on http://192.168.4.1
 *   - Provides web-based START & STOP button controls with live status telemetry.
 *   - Supports a physical hardware push-button on NodeMCU pin D3 (GPIO0).
 *   - Non-blocking state machine for smooth, repeatable 0° <-> 90° hand exercise.
 * ==============================================================================
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Servo.h>

// --------------------------- Pin Definitions ---------------------------
#define SERVO_PIN   D4   // GPIO2 - Servo PWM Control Signal
#define BUTTON_PIN  D3   // GPIO0 - Physical Start/Stop Push Button (Active LOW)

// --------------------------- Wi-Fi AP Credentials ----------------------
const char *AP_SSID = "ESP8266-Hand-Assistance";
const char *AP_PASS = "12345678"; // Min 8 characters for WPA2-PSK

// --------------------------- Web Server Instance -----------------------
ESP8266WebServer server(80);

// --------------------------- Servo & Motion Config ---------------------
Servo handServo;

const int MIN_ANGLE = 0;          // Rest position (0°)
const int MAX_ANGLE = 90;         // Target exercise position (90°)
const int STEP_DELAY_MS = 15;     // Speed: Delay between each degree step
const unsigned long HOLD_TIME_MS = 3000; // Hold duration at 0° and 90° (3 sec)

// --------------------------- State Machine Enum ------------------------
enum MotionState {
  STATE_STOPPED,           // Idle at MIN_ANGLE (0°)
  STATE_MOVING_TO_MAX,     // Smoothly moving 0° -> 90°
  STATE_HOLD_MAX,          // Holding at 90° for 3 seconds
  STATE_MOVING_TO_MIN,     // Smoothly moving 90° -> 0°
  STATE_HOLD_MIN,          // Holding at 0° for 3 seconds
  STATE_RETURNING_TO_REST  // Safely returning to 0° after user Stop command
};

// --------------------------- System Variables --------------------------
MotionState currentState = STATE_STOPPED;
bool isRunning = false;
int currentAngle = 0;
unsigned long lastStepTime = 0;
unsigned long holdStartTime = 0;
unsigned long cycleCount = 0;

// Button Debounce Variables
int lastButtonState = HIGH;
int buttonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

// --------------------------- Embedded Web UI ---------------------------
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>ESP8266 Hand Assistance Controller</title>
  <style>
    :root {
      --bg-gradient: linear-gradient(135deg, #0f172a 0%, #1e293b 100%);
      --card-bg: rgba(30, 41, 59, 0.85);
      --card-border: rgba(255, 255, 255, 0.08);
      --primary: #38bdf8;
      --primary-glow: rgba(56, 189, 248, 0.35);
      --success: #10b981;
      --success-glow: rgba(16, 185, 129, 0.4);
      --danger: #ef4444;
      --danger-glow: rgba(239, 68, 68, 0.4);
      --text-main: #f8fafc;
      --text-sub: #94a3b8;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; }
    body {
      background: var(--bg-gradient);
      color: var(--text-main);
      min-height: 100vh;
      display: flex;
      flex-direction: column;
      align-items: center;
      padding: 20px 14px;
    }
    .container {
      width: 100%;
      max-width: 480px;
      display: flex;
      flex-direction: column;
      gap: 18px;
    }
    header {
      text-align: center;
      padding: 10px 0;
    }
    .brand {
      font-size: 0.8rem;
      letter-spacing: 2px;
      text-transform: uppercase;
      color: var(--primary);
      font-weight: 700;
      margin-bottom: 4px;
    }
    h1 {
      font-size: 1.55rem;
      font-weight: 700;
      color: #fff;
    }
    .subtitle {
      font-size: 0.85rem;
      color: var(--text-sub);
      margin-top: 4px;
    }
    .card {
      background: var(--card-bg);
      backdrop-filter: blur(12px);
      -webkit-backdrop-filter: blur(12px);
      border: 1px solid var(--card-border);
      border-radius: 20px;
      padding: 22px;
      box-shadow: 0 10px 30px rgba(0,0,0,0.3);
    }
    .status-badge-container {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 16px;
    }
    .status-title {
      font-size: 0.9rem;
      color: var(--text-sub);
      text-transform: uppercase;
      letter-spacing: 1px;
      font-weight: 600;
    }
    .badge {
      display: inline-flex;
      align-items: center;
      gap: 6px;
      padding: 6px 14px;
      border-radius: 9999px;
      font-size: 0.82rem;
      font-weight: 700;
      text-transform: uppercase;
      letter-spacing: 0.5px;
      transition: all 0.3s ease;
    }
    .badge-stopped {
      background: rgba(239, 68, 68, 0.15);
      color: #f87171;
      border: 1px solid rgba(239, 68, 68, 0.3);
    }
    .badge-running {
      background: rgba(16, 185, 129, 0.15);
      color: #34d399;
      border: 1px solid rgba(16, 185, 129, 0.4);
      box-shadow: 0 0 15px var(--success-glow);
    }
    .indicator-dot {
      width: 8px;
      height: 8px;
      border-radius: 50%;
      background: currentColor;
    }
    .badge-running .indicator-dot {
      animation: pulse 1.5s infinite;
    }
    @keyframes pulse {
      0%, 100% { opacity: 1; transform: scale(1); }
      50% { opacity: 0.4; transform: scale(0.8); }
    }
    .gauge-wrapper {
      text-align: center;
      margin: 10px 0 18px 0;
    }
    .angle-value {
      font-size: 3.2rem;
      font-weight: 800;
      color: #fff;
      line-height: 1;
      font-variant-numeric: tabular-nums;
    }
    .angle-unit {
      font-size: 1.8rem;
      color: var(--primary);
      font-weight: 600;
    }
    .motion-state {
      font-size: 0.95rem;
      font-weight: 600;
      color: var(--primary);
      margin-top: 8px;
      min-height: 22px;
    }
    .bar-bg {
      width: 100%;
      height: 12px;
      background: rgba(255, 255, 255, 0.08);
      border-radius: 9999px;
      overflow: hidden;
      margin-top: 14px;
      position: relative;
    }
    .bar-fill {
      height: 100%;
      width: 0%;
      background: linear-gradient(90deg, #38bdf8, #818cf8);
      border-radius: 9999px;
      transition: width 0.15s ease-out;
      box-shadow: 0 0 10px rgba(56, 189, 248, 0.5);
    }
    .bar-labels {
      display: flex;
      justify-content: space-between;
      font-size: 0.75rem;
      color: var(--text-sub);
      margin-top: 6px;
    }
    .btn-group {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 14px;
      margin-top: 8px;
    }
    .btn {
      border: none;
      outline: none;
      cursor: pointer;
      padding: 16px 20px;
      border-radius: 16px;
      font-size: 1.05rem;
      font-weight: 700;
      letter-spacing: 0.5px;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      gap: 4px;
      transition: all 0.2s cubic-bezier(0.4, 0, 0.2, 1);
      user-select: none;
      -webkit-tap-highlight-color: transparent;
    }
    .btn:active {
      transform: scale(0.96);
    }
    .btn-start {
      background: linear-gradient(135deg, #10b981 0%, #059669 100%);
      color: #ffffff;
      box-shadow: 0 8px 20px var(--success-glow);
    }
    .btn-start:hover {
      box-shadow: 0 10px 25px rgba(16, 185, 129, 0.6);
    }
    .btn-stop {
      background: linear-gradient(135deg, #ef4444 0%, #dc2626 100%);
      color: #ffffff;
      box-shadow: 0 8px 20px var(--danger-glow);
    }
    .btn-stop:hover {
      box-shadow: 0 10px 25px rgba(239, 68, 68, 0.6);
    }
    .btn-subtext {
      font-size: 0.7rem;
      opacity: 0.85;
      font-weight: 500;
    }
    .stats-grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 12px;
    }
    .stat-box {
      background: rgba(15, 23, 42, 0.6);
      border: 1px solid rgba(255, 255, 255, 0.05);
      padding: 14px;
      border-radius: 14px;
      text-align: center;
    }
    .stat-label {
      font-size: 0.72rem;
      text-transform: uppercase;
      letter-spacing: 0.5px;
      color: var(--text-sub);
    }
    .stat-value {
      font-size: 1.25rem;
      font-weight: 700;
      color: #fff;
      margin-top: 4px;
    }
    footer {
      text-align: center;
      font-size: 0.75rem;
      color: var(--text-sub);
      margin-top: 8px;
    }
    .connection-pill {
      display: inline-flex;
      align-items: center;
      gap: 6px;
      font-size: 0.75rem;
      color: #38bdf8;
      background: rgba(56, 189, 248, 0.1);
      padding: 4px 10px;
      border-radius: 20px;
      margin-top: 6px;
    }
  </style>
</head>
<body>
  <div class="container">
    <header>
      <div class="brand">ESP8266 Hotspot Controller</div>
      <h1>Hand Assistance System</h1>
      <p class="subtitle">Controlled Repetitive Rehabilitation Prototype</p>
      <div class="connection-pill">
        <span>Wi-Fi: <b>ESP8266-Hand-Assistance</b></span>
      </div>
    </header>

    <div class="card">
      <div class="status-badge-container">
        <span class="status-title">System Status</span>
        <div id="statusBadge" class="badge badge-stopped">
          <span class="indicator-dot"></span>
          <span id="statusText">STOPPED</span>
        </div>
      </div>

      <div class="gauge-wrapper">
        <div class="angle-value">
          <span id="angleValue">0</span><span class="angle-unit">°</span>
        </div>
        <div id="motionState" class="motion-state">System Idle (Rest 0°)</div>
        
        <div class="bar-bg">
          <div id="barFill" class="bar-fill"></div>
        </div>
        <div class="bar-labels">
          <span>0° (Rest)</span>
          <span>45°</span>
          <span>90° (Flexion)</span>
        </div>
      </div>

      <div class="btn-group">
        <button id="btnStart" class="btn btn-start" onclick="sendCommand('start')">
          <span>START</span>
          <span class="btn-subtext">Begin Repetitions</span>
        </button>
        <button id="btnStop" class="btn btn-stop" onclick="sendCommand('stop')">
          <span>STOP</span>
          <span class="btn-subtext">Safe Return to 0°</span>
        </button>
      </div>
    </div>

    <div class="stats-grid">
      <div class="stat-box">
        <div class="stat-label">Completed Cycles</div>
        <div id="cycleCount" class="stat-value">0</div>
      </div>
      <div class="stat-box">
        <div class="stat-label">Hold Duration</div>
        <div class="stat-value">3.0 s</div>
      </div>
      <div class="stat-box">
        <div class="stat-label">Motion Range</div>
        <div class="stat-value">0° - 90°</div>
      </div>
      <div class="stat-box">
        <div class="stat-label">Hardware Button</div>
        <div class="stat-value">NodeMCU D3</div>
      </div>
    </div>

    <footer>
      ESP8266 Hand Assistance Rehabilitation System
    </footer>
  </div>

  <script>
    let isRequesting = false;

    function sendCommand(action) {
      fetch('/' + action, { method: 'POST' })
        .then(response => response.json())
        .then(data => {
          updateUI(data);
        })
        .catch(err => {
          // Fallback to GET if POST failed
          fetch('/' + action)
            .then(res => res.json())
            .then(data => updateUI(data))
            .catch(e => console.error('Command Error:', e));
        });
    }

    function pollStatus() {
      if (isRequesting) return;
      isRequesting = true;
      fetch('/status')
        .then(response => response.json())
        .then(data => {
          updateUI(data);
          isRequesting = false;
        })
        .catch(err => {
          isRequesting = false;
        });
    }

    function updateUI(data) {
      if (!data) return;
      
      const badge = document.getElementById('statusBadge');
      const statusText = document.getElementById('statusText');
      const angleVal = document.getElementById('angleValue');
      const barFill = document.getElementById('barFill');
      const motionState = document.getElementById('motionState');
      const cycleCount = document.getElementById('cycleCount');

      if (data.running) {
        badge.className = 'badge badge-running';
        statusText.innerText = 'RUNNING';
      } else {
        badge.className = 'badge badge-stopped';
        statusText.innerText = 'STOPPED';
      }

      angleVal.innerText = data.angle;
      const pct = Math.max(0, Math.min(100, (data.angle / 90.0) * 100));
      barFill.style.width = pct + '%';

      if (data.state) {
        motionState.innerText = data.state;
      }
      if (data.cycles !== undefined) {
        cycleCount.innerText = data.cycles;
      }
    }

    // High frequency telemetry polling (every 180ms)
    setInterval(pollStatus, 180);
    pollStatus();
  </script>
</body>
</html>
)rawliteral";

// --------------------------- Helper Functions --------------------------

String getMotionStateDescription() {
  switch (currentState) {
    case STATE_STOPPED:
      return "System Idle (Rest 0°)";
    case STATE_MOVING_TO_MAX:
      return "Flexion: Moving (0° → 90°)";
    case STATE_HOLD_MAX:
      return "Holding at 90° (Hold 3s)";
    case STATE_MOVING_TO_MIN:
      return "Extension: Moving (90° → 0°)";
    case STATE_HOLD_MIN:
      return "Holding at 0° (Hold 3s)";
    case STATE_RETURNING_TO_REST:
      return "Stopping: Returning to 0° Safe Rest";
    default:
      return "Ready";
  }
}

String getStatusJson() {
  String json = "{";
  json += "\"running\":" + String(isRunning ? "true" : "false") + ",";
  json += "\"angle\":" + String(currentAngle) + ",";
  json += "\"state\":\"" + getMotionStateDescription() + "\",";
  json += "\"cycles\":" + String(cycleCount);
  json += "}";
  return json;
}

void startOperation() {
  if (!isRunning) {
    isRunning = true;
    if (currentState == STATE_STOPPED || currentState == STATE_RETURNING_TO_REST) {
      currentState = STATE_MOVING_TO_MAX;
      lastStepTime = millis();
    }
    Serial.println(F("[SYSTEM] Operation STARTED via Control"));
  }
}

void stopOperation() {
  if (isRunning) {
    isRunning = false;
    if (currentAngle > MIN_ANGLE) {
      currentState = STATE_RETURNING_TO_REST;
      lastStepTime = millis();
    } else {
      currentState = STATE_STOPPED;
    }
    Serial.println(F("[SYSTEM] Operation STOPPED via Control"));
  }
}

void toggleOperation() {
  if (isRunning) {
    stopOperation();
  } else {
    startOperation();
  }
}

// --------------------------- Web Server Handlers -----------------------

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleStart() {
  startOperation();
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", getStatusJson());
}

void handleStop() {
  stopOperation();
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", getStatusJson());
}

void handleStatus() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", getStatusJson());
}

void handleNotFound() {
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "Redirecting to Dashboard...");
}

// --------------------------- Physical Button Handler -------------------

void checkPhysicalButton() {
  int reading = digitalRead(BUTTON_PIN);

  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;
      // Button is active LOW with INPUT_PULLUP
      if (buttonState == LOW) {
        Serial.println(F("[BUTTON] Physical button pressed!"));
        toggleOperation();
      }
    }
  }

  lastButtonState = reading;
}

// --------------------------- Non-blocking Motion Engine -----------------

void updateMotionEngine() {
  unsigned long now = millis();

  switch (currentState) {
    case STATE_STOPPED:
      // Inactive, resting safely at 0°
      break;

    case STATE_MOVING_TO_MAX:
      if (now - lastStepTime >= STEP_DELAY_MS) {
        lastStepTime = now;
        currentAngle++;
        handServo.write(currentAngle);

        if (currentAngle >= MAX_ANGLE) {
          currentAngle = MAX_ANGLE;
          currentState = STATE_HOLD_MAX;
          holdStartTime = now;
        }
      }
      break;

    case STATE_HOLD_MAX:
      if (now - holdStartTime >= HOLD_TIME_MS) {
        currentState = STATE_MOVING_TO_MIN;
        lastStepTime = now;
      }
      break;

    case STATE_MOVING_TO_MIN:
      if (now - lastStepTime >= STEP_DELAY_MS) {
        lastStepTime = now;
        currentAngle--;
        handServo.write(currentAngle);

        if (currentAngle <= MIN_ANGLE) {
          currentAngle = MIN_ANGLE;
          currentState = STATE_HOLD_MIN;
          holdStartTime = now;
        }
      }
      break;

    case STATE_HOLD_MIN:
      if (now - holdStartTime >= HOLD_TIME_MS) {
        cycleCount++;
        Serial.print(F("[CYCLE] Completed Cycle: "));
        Serial.println(cycleCount);

        if (isRunning) {
          currentState = STATE_MOVING_TO_MAX;
          lastStepTime = now;
        } else {
          currentState = STATE_STOPPED;
        }
      }
      break;

    case STATE_RETURNING_TO_REST:
      // Gracefully step back to 0°
      if (now - lastStepTime >= STEP_DELAY_MS) {
        lastStepTime = now;
        if (currentAngle > MIN_ANGLE) {
          currentAngle--;
          handServo.write(currentAngle);
        } else {
          currentAngle = MIN_ANGLE;
          currentState = STATE_STOPPED;
          Serial.println(F("[SYSTEM] Servo safely positioned at 0° Rest"));
        }
      }
      break;
  }
}

// --------------------------- Arduino Setup -----------------------------

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println(F("\n=============================================="));
  Serial.println(F(" ESP8266 Hand Assistance Web Control System"));
  Serial.println(F("=============================================="));

  // Initialize Physical Button with Internal Pull-Up
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Initialize Servo
  handServo.attach(SERVO_PIN);
  currentAngle = MIN_ANGLE;
  handServo.write(currentAngle);
  delay(500);

  // Configure Wi-Fi SoftAP Mode
  WiFi.mode(WIFI_AP);
  bool apSuccess = WiFi.softAP(AP_SSID, AP_PASS);

  if (apSuccess) {
    Serial.print(F("[WIFI] Access Point Created: "));
    Serial.println(AP_SSID);
    Serial.print(F("[WIFI] AP IP Address: "));
    Serial.println(WiFi.softAPIP());
    Serial.println(F("[WIFI] Connect your phone/PC to this Wi-Fi and open http://192.168.4.1"));
  } else {
    Serial.println(F("[WIFI] Failed to create Access Point!"));
  }

  // Configure Web Server Routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/start", HTTP_ANY, handleStart);
  server.on("/stop", HTTP_ANY, handleStop);
  server.on("/status", HTTP_GET, handleStatus);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println(F("[HTTP] Web Server Started successfully!"));
  Serial.println(F("[STATUS] System is IDLE. Use Web UI or D3 Button to START.\n"));
}

// --------------------------- Arduino Main Loop -------------------------

void loop() {
  // 1. Handle incoming HTTP client requests
  server.handleClient();

  // 2. Check hardware pushbutton input (D3)
  checkPhysicalButton();

  // 3. Update non-blocking servo motion engine
  updateMotionEngine();
}
