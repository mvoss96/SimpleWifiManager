#pragma once

#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

class SimpleWifiManager
{
public:
    explicit SimpleWifiManager();
    ~SimpleWifiManager();

    // Start WiFi manager and captive portal if needed.
    void begin(const char *apName = "WiFi-Setup");
    // Customize Title of the captive portal page.
    void customize(const char *title);
    // Stop WiFi manager and release resources created in begin().
    void stop();
    // Configure how long to wait for STA connection before AP fallback.
    void setTimeout(uint32_t timeoutMs);
    // Configure how long to wait in offline state before starting AP mode.
    void setOfflineTimeout(uint32_t timeoutMs);
    // Configure how often to retry STA connection while in AP mode.
    void setRetryInterval(uint32_t intervalMs);
    // Enable or disable WiFi auto reconnect attempts while in AP mode.
    void setApAutoReconnect(bool enabled);
    // Enable or disable AP fallback when connection drops or times out.
    void setApFallback(bool enabled);
    // Clear stored WiFi credentials in persistent storage.
    void resetCredentials();

private:
    enum class State
    {
        Idle,
        Connecting,
        ApMode,
        Connected
    };

    void handlePortal();
    void handleScan();
    void handleSave();
    void sendGzipResponse(const uint8_t *data, size_t size, const char *contentType = "text/html");
    void handleRedirect();
    void loadCredentials();
    void saveCredentials();
    void handleCustom();
    void startApMode();
    void startStationConnect();
    void startWorkerTask();
    static void workerTask(void *parameter);

    const char *mPrefKey = "wifi-creds";

    WebServer mServer{80};
    DNSServer mDnsServer;
    Preferences mPrefs;

    String mSsid;
    String mPass;
    String mApName;
    String mCaptiveTitle = "WiFi Setup";

    const uint32_t mTaskInterval = 50;      // Delay between worker task iterations in milliseconds.
    uint32_t mConnectTimeout = 10 * 1000;   // Time to wait for STA connection before AP fallback in milliseconds.
    uint32_t mStaRetryInterval = 30 * 1000; // Time to wait between STA connection attempts while in AP mode in milliseconds.
    uint32_t mOfflineTimeout = 5 * 1000;    // Time to wait in offline state before starting AP mode in milliseconds.
    bool mApAutoReconnectEnabled = true;
    bool mApFallbackEnabled = true;

    State mState = State::Idle;
    uint32_t mConnectStartTime = 0;
    uint32_t mLastStaAttemptTime = 0;
    uint32_t mApStaStartTime = 0;
    uint32_t mOfflineTime = 0;
    volatile bool mStopRequested = false;
    TaskHandle_t mWorkerTaskHandle = nullptr;
};
