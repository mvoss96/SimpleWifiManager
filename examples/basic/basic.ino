#include "SimpleWifiManager.h"

SimpleWifiManager swm;

void setup()
{
    swm.begin("Setup-AP");
}

void loop()
{
    // The library manages WiFi connectivity asynchronously.
}
