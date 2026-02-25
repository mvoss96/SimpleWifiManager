#include "SimpleWifiManager.h"

SimpleWifiManager swm;

constexpr uint8_t kResetPin = 0; // Boot button on many ESP32 boards

void setup()
{
    Serial.begin(115200);
    pinMode(kResetPin, INPUT_PULLUP);

    swm.customize("Device WiFi Setup"); // Custom title for the captive portal page.
    swm.setTimeout(10000);              // Wait for 10 seconds for WiFi connection before starting AP mode.
    swm.setOfflineTimeout(8000);        // If no WiFi connection is established, start AP mode after 8 seconds.
    swm.setRetryInterval(15000);        // While in AP mode, retry connecting to WiFi every 15 seconds.
    swm.begin("Device-Setup");          // Name of the AP when in setup mode.
}

void loop()
{
    // Hold the reset pin low to clear stored credentials.
    if (digitalRead(kResetPin) == LOW)
    {
        swm.resetCredentials();
        delay(500);
    }
    Serial.println("Wifi status: " + String((swm.isConnected() ? "Connected" : "Not Connected")));
    delay(1000);
}
