#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <U8g2lib.h>
#include <Wire.h>

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R2);

int tempLeft = -999;
int tempTopRight = -999;
bool pumpRunning = false;
int spinnerFrame = 0;

unsigned long lastBasementUpdate = 0;
unsigned long lastRoofUpdate = 0;

const int buttonPin = 14;
int lastButtonState = HIGH;
int currentButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

bool screenOn = false;
unsigned long displayTimeout = 0;
const unsigned long displayDuration = 10000;

typedef struct __attribute__((packed)) {
    float temperature;
    bool pumpStatus;
} TempMessage;

void onReceive(const esp_now_recv_info *info, const uint8_t *data, int len) {
    if (len == sizeof(TempMessage)) {
        TempMessage msg;
        memcpy(&msg, data, len);
        tempLeft = (int) round(msg.temperature);
        pumpRunning = msg.pumpStatus;
        lastBasementUpdate = millis();
    } else if (len == sizeof(float)) {
        float temp;
        memcpy(&temp, data, len);
        tempTopRight = (int) round(temp);
        lastRoofUpdate = millis();
    }
}

void setup() {
    u8g2.begin();
    u8g2.enableUTF8Print();
    u8g2.setBusClock(400000UL);

    WiFi.mode(WIFI_STA);
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR);

    if (esp_now_init() != ESP_OK) {
        return;
    }
    esp_now_register_recv_cb(onReceive);

    pinMode(buttonPin, INPUT_PULLUP);
}

void loop() {
    handleButton();

    if (screenOn && millis() > displayTimeout) {
        screenOn = false;
        u8g2.setPowerSave(1);
    }

    if (screenOn) {
        drawScreen();
    }

    delay(50);
}

void handleButton() {
    int reading = digitalRead(buttonPin);

    if (reading != lastButtonState) {
        lastDebounceTime = millis();
    }

    if ((millis() - lastDebounceTime) > debounceDelay) {
        if (reading != currentButtonState) {
            currentButtonState = reading;

            if (currentButtonState == LOW) {
                screenOn = true;
                displayTimeout = millis() + displayDuration;
                u8g2.setPowerSave(0);
            }
        }
    }

    lastButtonState = reading;
}

void drawScreen() {
    u8g2.clearBuffer();
    u8g2.drawLine(72, 0, 72, 64);

    bool validBasement = (millis() - lastBasementUpdate < 60000) && tempLeft > -50 && tempLeft < 100;

    if (validBasement) {
        String numStr = String(tempLeft);
        u8g2.setFont(u8g2_font_fur25_tf);
        int numWidth = u8g2.getUTF8Width(numStr.c_str());
        int xNum = (72 - (numWidth + 10)) / 2;
        int yNum = 44;
        u8g2.drawUTF8(xNum, yNum, numStr.c_str());
        u8g2.setFont(u8g2_font_fur11_tf);
        u8g2.drawUTF8(xNum + numWidth + 1, yNum - 10, "°C");
    } else {
        u8g2.setFont(u8g2_font_fur25_tf);
        int errWidth = u8g2.getUTF8Width("Err");
        int xErr = (72 - errWidth) / 2;
        u8g2.drawUTF8(xErr, 44, "Err");
    }

    u8g2.drawLine(72, 32, 128, 32);
    u8g2.setFont(u8g2_font_fur14_tf);

    bool validRoof = (millis() - lastRoofUpdate < 60000) && tempTopRight > -50 && tempTopRight < 100;

    if (validRoof) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d°C", tempTopRight);
        int w = u8g2.getUTF8Width(buf);
        u8g2.drawUTF8(72 + (56 - w) / 2, 22, buf);
    } else {
        int errWidth = u8g2.getUTF8Width("Err");
        u8g2.drawUTF8(72 + (56 - errWidth) / 2, 22, "Err");
    }

    if (pumpRunning) {
        drawSpinner(100, 48, 10, spinnerFrame);
        spinnerFrame = (spinnerFrame + 1) % 12;
    }

    u8g2.sendBuffer();
}

void drawSpinner(int cx, int cy, int radius, int frame) {
    const int dotCount = 12;
    for (int i = 0; i < dotCount; i++) {
        float angle = (2 * PI / dotCount) * i;
        int x = cx + cos(angle) * radius;
        int y = cy + sin(angle) * radius;
        if (i == frame)
            u8g2.drawDisc(x, y, 2);
        else
            u8g2.drawDisc(x, y, 1);
    }
}
