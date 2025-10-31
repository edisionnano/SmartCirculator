#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Preferences.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 128
#define OLED_RESET -1
Adafruit_SH1107 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define ONE_WIRE_BUS 4
#define RELAY_PIN 5
#define ENCODER_SW 27
#define ENCODER_S1 25
#define ENCODER_S2 26

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

uint8_t box2Address[] = {0x78, 0x1C, 0x3C, 0xA8, 0xE2, 0x40};

Preferences prefs;

float basementTemp = NAN;
float roofTemp = NAN;
unsigned long lastSend = 0;
unsigned long lastRoofUpdate = 0;
unsigned long lastBasementUpdate = 0;

bool pumpRunning = false;
unsigned long pumpStartTime = 0;
bool pumpError = false;

struct Config {
    float tempDeltaOn;
    float tempDeltaOff;
    float minSolarTemp;
    float maxBufferTemp;
    unsigned long maxPumpRunTime;
    unsigned long screenTimeout;
} config = {
    5.0,
    3.0,
    30.0,
    55.0,
    3600000,
    300000
};

int spinnerFrame = 0;
unsigned long lastSpinnerUpdate = 0;
const unsigned long spinnerDelay = 120;
const unsigned long refreshMs = 33;
unsigned long lastRefresh = 0;

bool screenOn = false;
unsigned long screenOnTime = 0;

bool inMenu = false;
int menuIndex = 0;
bool editingValue = false;
const int menuItems = 6;
String menuLabels[] = {
    "Delta ON",
    "Delta OFF",
    "Min Solar",
    "Max Buffer",
    "Max Run Time",
    "Screen Timeout"
};

volatile int encoderPos = 0;
int lastEncoderPos = 0;

typedef struct __attribute__((packed)) {
    float temp;
    bool pumpStatus;
} TempMessage;

void IRAM_ATTR encoderISR() {
    static int lastEncodedValue = 0;

    int MSB = digitalRead(ENCODER_S1);
    int LSB = digitalRead(ENCODER_S2);
    int encoded = (MSB << 1) | LSB;

    int sum = (lastEncodedValue << 2) | encoded;
    if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) encoderPos++;
    if (sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000) encoderPos--;

    lastEncodedValue = encoded;
}

void loadConfig() {
    prefs.begin("pump", false);
    config.tempDeltaOn = prefs.getFloat("deltaOn", 5.0);
    config.tempDeltaOff = prefs.getFloat("deltaOff", 3.0);
    config.minSolarTemp = prefs.getFloat("minSolar", 35.0);
    config.maxBufferTemp = prefs.getFloat("maxBuffer", 55.0);
    config.maxPumpRunTime = prefs.getULong("maxRunTime", 3600000);
    config.screenTimeout = prefs.getULong("scrTimeout", 300000);
    prefs.end();
}

void saveConfig() {
    prefs.begin("pump", false);
    prefs.putFloat("deltaOn", config.tempDeltaOn);
    prefs.putFloat("deltaOff", config.tempDeltaOff);
    prefs.putFloat("minSolar", config.minSolarTemp);
    prefs.putFloat("maxBuffer", config.maxBufferTemp);
    prefs.putULong("maxRunTime", config.maxPumpRunTime);
    prefs.putULong("scrTimeout", config.screenTimeout);
    prefs.end();
}

float getConfigValue(int index) {
    switch (index) {
        case 0: return config.tempDeltaOn;
        case 1: return config.tempDeltaOff;
        case 2: return config.minSolarTemp;
        case 3: return config.maxBufferTemp;
        case 4: return config.maxPumpRunTime / 60000.0;
        case 5: return config.screenTimeout / 60000.0;
        default: return 0;
    }
}

void setConfigValue(int index, float value) {
    switch (index) {
        case 0:
            config.tempDeltaOn = constrain(value, 1.0, 10.0);
            break;
        case 1:
            config.tempDeltaOff = constrain(value, 1.0, 10.0);
            break;
        case 2:
            config.minSolarTemp = constrain(value, 20.0, 40.0);
            break;
        case 3:
            config.maxBufferTemp = constrain(value, 40.0, 60.0);
            break;
        case 4:
            config.maxPumpRunTime = constrain((unsigned long) (value * 60000), 60000, 3600000);
            break;
        case 5:
            config.screenTimeout = constrain((unsigned long) (value * 60000), 60000, 600000);
            break;
    }
    saveConfig();
}

void controlPump() {
    if (pumpError) return;

    bool validBasement = !isnan(basementTemp) && basementTemp > -10 && basementTemp < 90
                         && (millis() - lastBasementUpdate < 60000);
    bool validRoof = !isnan(roofTemp) && roofTemp > -10 && roofTemp < 90
                     && (millis() - lastRoofUpdate < 60000);

    if (!validBasement || !validRoof) {
        if (pumpRunning) {
            pumpRunning = false;
            digitalWrite(RELAY_PIN, HIGH);
        }
        return;
    }

    float tempDelta = roofTemp - basementTemp;

    if (pumpRunning && (millis() - pumpStartTime > config.maxPumpRunTime)) {
        pumpError = true;
        pumpRunning = false;
        digitalWrite(RELAY_PIN, HIGH);
        return;
    }

    if (pumpRunning) {
        if (tempDelta < config.tempDeltaOff ||
            basementTemp >= config.maxBufferTemp ||
            roofTemp < config.minSolarTemp) {
            pumpRunning = false;
            digitalWrite(RELAY_PIN, HIGH);
        }
    } else {
        if (tempDelta >= config.tempDeltaOn &&
            basementTemp < config.maxBufferTemp &&
            roofTemp >= config.minSolarTemp) {
            pumpRunning = true;
            pumpStartTime = millis();
            digitalWrite(RELAY_PIN, LOW);
        }
    }
}

void onSend(const wifi_tx_info_t *info, esp_now_send_status_t status) {
}

void onReceive(const esp_now_recv_info *info, const uint8_t *data, int len) {
    if (len == sizeof(float)) {
        float temp;
        memcpy(&temp, data, sizeof(float));
        roofTemp = temp;
        lastRoofUpdate = millis();
    } else if (len == sizeof(TempMessage)) {
        TempMessage msg;
        memcpy(&msg, data, sizeof(msg));
        roofTemp = msg.temp;
        lastRoofUpdate = millis();
    }
}

void drawDegreeSymbol(int x, int y, bool large) {
    int r = large ? 3 : 2;
    display.drawCircle(x, y, r, SH110X_WHITE);
    if (large) display.setTextSize(2);
    else display.setTextSize(1);
    display.setCursor(x + r * 2 + 1, y - (large ? 5 : 2));
    display.print("C");
}

void drawSpinner(int cx, int cy, int radius) {
    const int dots = 12;
    for (int i = 0; i < dots; ++i) {
        float a = (2 * PI / dots) * i;
        int x = cx + (int) (cos(a) * radius);
        int y = cy + (int) (sin(a) * radius);
        int r = (i == spinnerFrame) ? 3 : 1;
        display.fillCircle(x, y, r, SH110X_WHITE);
    }
    if (millis() - lastSpinnerUpdate >= spinnerDelay) {
        spinnerFrame = (spinnerFrame + 1) % dots;
        lastSpinnerUpdate = millis();
    }
}

void drawMainScreen() {
    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);
    display.setTextWrap(false);

    if (pumpError) {
        display.setTextSize(2);
        display.setCursor(10, 30);
        display.print("PUMP");
        display.setCursor(10, 50);
        display.print("ERROR");
        display.setTextSize(1);
        display.setCursor(5, 80);
        display.print("Power cycle");
        display.setCursor(5, 95);
        display.print("to reset");
        display.display();
        return;
    }

    display.setTextSize(5);
    display.setCursor(12, 25);
    bool invalidBasement = isnan(basementTemp) || basementTemp < -10 || basementTemp > 90
                           || (millis() - lastBasementUpdate > 60000);
    if (invalidBasement) {
        display.print("Err");
    } else {
        display.printf("%2d", (int) round(basementTemp));
        drawDegreeSymbol(92, 35, true);
    }

    display.drawFastHLine(0, 80, SCREEN_WIDTH, SH110X_WHITE);
    display.drawFastVLine(86, 80, 48, SH110X_WHITE);

    display.setTextSize(3);
    display.setCursor(10, 98);
    bool invalidRoof = isnan(roofTemp) || roofTemp < -10 || roofTemp > 90
                       || (millis() - lastRoofUpdate > 60000);
    if (invalidRoof) {
        display.print("Err");
    } else {
        display.printf("%2d", (int) round(roofTemp));
        drawDegreeSymbol(56, 100, false);
    }

    if (pumpRunning) {
        drawSpinner(108, 104, 10);
    }

    display.display();
}

void drawMenu() {
    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);
    display.setTextSize(1);

    display.setCursor(0, 0);
    display.print("Hold 1s to exit");
    display.drawFastHLine(0, 9, SCREEN_WIDTH, SH110X_WHITE);

    int startItem = max(0, min(menuIndex - 2, menuItems - 6));
    int y = 14;

    for (int i = startItem; i < min(menuItems, startItem + 6); i++) {
        if (i == menuIndex) {
            display.fillTriangle(2, y + 2, 2, y + 6, 5, y + 4, SH110X_WHITE);
        }

        display.setCursor(8, y);
        display.print(menuLabels[i]);

        float val = getConfigValue(i);

        char valBuf[16];
        if (i == 4 || i == 5) {
            snprintf(valBuf, sizeof(valBuf), "%.0fm", val);
        } else {
            snprintf(valBuf, sizeof(valBuf), "%.0f", val);
        }

        int valWidth = strlen(valBuf) * 6;
        int valX = 120 - valWidth;

        if (editingValue && i == menuIndex) {
            display.setCursor(valX - 6, y);
            display.print("[");
            display.setCursor(valX, y);
            display.print(valBuf);
            display.setCursor(valX + valWidth, y);
            display.print("]");
        } else {
            display.setCursor(valX, y);
            display.print(valBuf);
        }

        y += 11;
        if (i < menuItems - 1 && y < 120) {
            display.drawFastHLine(0, y, SCREEN_WIDTH, SH110X_WHITE);
            y += 2;
        }
    }

    display.display();
}

void handleButton() {
    static unsigned long lastPress = 0;
    static bool lastButtonState = HIGH;
    bool currentButtonState = digitalRead(ENCODER_SW);

    if (currentButtonState == LOW && lastButtonState == HIGH && (millis() - lastPress > 200)) {
        lastPress = millis();

        if (!screenOn) {
            screenOn = true;
            screenOnTime = millis();
        } else if (!inMenu) {
            inMenu = true;
            menuIndex = 0;
            encoderPos = 0;
            lastEncoderPos = 0;
        } else if (inMenu && !editingValue) {
            editingValue = true;
            encoderPos = 0;
            lastEncoderPos = 0;
        } else if (editingValue) {
            editingValue = false;
            encoderPos = menuIndex;
        }
    }

    lastButtonState = currentButtonState;

    static unsigned long pressStart = 0;
    if (currentButtonState == LOW) {
        if (pressStart == 0) pressStart = millis();
        if (inMenu && millis() - pressStart > 1000) {
            inMenu = false;
            editingValue = false;
            pressStart = 0;
        }
    } else {
        pressStart = 0;
    }
}

void handleEncoder() {
    static int lastA = HIGH;
    static unsigned long lastValidChange = 0;

    int currentA = digitalRead(ENCODER_S1);

    if (lastA == HIGH && currentA == LOW) {
        unsigned long now = millis();
        if (now - lastValidChange > 20) {
            int delta = 1;

            encoderPos += delta;
            lastValidChange = now;

            if (inMenu) {
                if (editingValue) {
                    float currentVal = getConfigValue(menuIndex);
                    float step;
                    if (menuIndex == 4) step = 5.0;
                    else if (menuIndex == 5) step = 1.0;
                    else step = 1.0;
                    float newVal = currentVal + (delta * step);

                    switch (menuIndex) {
                        case 0: if (newVal > 10.0) newVal = 1.0;
                            break;
                        case 1: if (newVal > 10.0) newVal = 1.0;
                            break;
                        case 2: if (newVal > 40.0) newVal = 20.0;
                            break;
                        case 3: if (newVal > 60.0) newVal = 40.0;
                            break;
                        case 4: if (newVal > 60.0) newVal = 1.0;
                            break;
                        case 5: if (newVal > 10.0) newVal = 1.0;
                            break;
                    }
                    setConfigValue(menuIndex, newVal);
                } else {
                    menuIndex += delta;
                    if (menuIndex >= menuItems) menuIndex = 0;
                }
                screenOnTime = millis();
            }

            lastEncoderPos = encoderPos;
        }
    }

    lastA = currentA;
}

void setup() {
    Wire.begin();

    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, HIGH);

    pinMode(ENCODER_SW, INPUT_PULLUP);
    pinMode(ENCODER_S1, INPUT_PULLUP);
    pinMode(ENCODER_S2, INPUT_PULLUP);

    attachInterrupt(digitalPinToInterrupt(ENCODER_S1), encoderISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENCODER_S2), encoderISR, CHANGE);

    if (!display.begin(0x3C, true)) {
        while (1) delay(1000);
    }
    display.setRotation(1);
    display.clearDisplay();
    display.display();

    sensors.begin();

    sensors.requestTemperatures();
    float t = sensors.getTempCByIndex(0);
    if (t != DEVICE_DISCONNECTED_C && t > -55 && t < 125) {
        basementTemp = t;
        lastBasementUpdate = millis();
    }

    loadConfig();

    WiFi.mode(WIFI_STA);
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR);
    if (esp_now_init() == ESP_OK) {
        esp_now_register_send_cb(onSend);
        esp_now_register_recv_cb(onReceive);
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, box2Address, 6);
        peer.channel = 0;
        peer.encrypt = false;
        esp_now_add_peer(&peer);
    }
}

void loop() {
    unsigned long now = millis();

    handleButton();
    handleEncoder();

    if (screenOn && !inMenu && (now - screenOnTime > config.screenTimeout)) {
        screenOn = false;
        display.clearDisplay();
        display.display();
    }

    static unsigned long lastRead = 0;
    if (now - lastRead > 5000) {
        sensors.requestTemperatures();
        float t = sensors.getTempCByIndex(0);

        if (t != DEVICE_DISCONNECTED_C && t > -55 && t < 125) {
            basementTemp = t;
            lastBasementUpdate = now;
        }
        lastRead = now;
    }

    controlPump();

    if (screenOn && (now - lastRefresh >= refreshMs)) {
        if (inMenu) {
            drawMenu();
        } else {
            drawMainScreen();
        }
        lastRefresh = now;
    }

    if (!isnan(basementTemp) && now - lastSend >= 5000) {
        TempMessage msg;
        msg.temp = basementTemp;
        msg.pumpStatus = pumpRunning;
        esp_now_send(box2Address, (uint8_t *) &msg, sizeof(msg));
        lastSend = now;
    }

    delay(1);
}
