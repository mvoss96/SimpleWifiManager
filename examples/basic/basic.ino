#include "SimpleWifiManager.h"

SimpleWifiManager swm;

void setup()
{
    Serial.begin(115200);
    swm.begin("Setup-AP");
}

void loop()
{
    // The library manages WiFi connectivity asynchronously.
    Serial.println("Wifi status: " + String((swm.isConnected() ? "Connected" : "Not Connected")));
    delay(1000);
}
