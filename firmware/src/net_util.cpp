#include "net_util.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

bool https_get(const char* url, String& body, uint32_t timeout_ms) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setConnectTimeout(timeout_ms);
    http.setTimeout(timeout_ms);
    if (!http.begin(client, url)) {
        Serial.println("net: HTTP begin failed");
        return false;
    }
    http.addHeader("User-Agent", "clawdmeter/1.0");
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("net: GET failed code=%d\n", code);
        http.end();
        return false;
    }
    body = http.getString();
    http.end();
    return true;
}
