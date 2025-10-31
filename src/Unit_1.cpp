#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define ONE_WIRE_BUS 4
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

uint8_t receivers[][6] = {
    {0x78, 0x1C, 0x3C, 0xA8, 0xE2, 0x40},
    {0x78, 0x1C, 0x3C, 0xA7, 0xC9, 0x94}
};
const int numReceivers = sizeof(receivers) / 6;

typedef struct __attribute__((packed)) {
    float temperature;
} TempMessage;

void setup() {
    sensors.begin();

    WiFi.mode(WIFI_STA);
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR);
    if (esp_now_init() != ESP_OK) {
        return;
    }

    for (int i = 0; i < numReceivers; i++) {
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, receivers[i], 6);
        peerInfo.channel = 0;
        peerInfo.encrypt = false;
    }
}

void loop() {
    sensors.requestTemperatures();
    float tempC = sensors.getTempCByIndex(0);

    TempMessage msg;
    msg.temperature = tempC;

    for (int i = 0; i < numReceivers; i++) {
        esp_err_t result = esp_now_send(receivers[i], (uint8_t *) &msg, sizeof(msg));
    }

    delay(1000);
}
