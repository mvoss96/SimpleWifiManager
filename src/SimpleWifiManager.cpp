#include "SimpleWifiManager.h"

#include <esp_log.h>
#include <algorithm>
#include <vector>
#include "CaptivePage.h"

//static const char *kLogTag = "SimpleWifiManager";

SimpleWifiManager::SimpleWifiManager()
{
    // Reserve memory for WiFi credentials to avoid fragmentation later
    mSsid.reserve(32);
    mPass.reserve(64);
    mApName.reserve(32);
}

SimpleWifiManager::~SimpleWifiManager()
{
    stop();
}

void SimpleWifiManager::begin(const char *apName)
{
    mApName = apName;

    mPrefs.begin(mPrefKey, false);
    loadCredentials();



    if (mSsid.length() > 0)
    {
        log_i("SMW: SSID %s saved", mSsid.c_str());
        startStationConnect();
    }
    else
    {
        log_i("SMW: No SSID saved");
        startApMode();
    }
}

void SimpleWifiManager::customize(const char *title)
{
    mCaptiveTitle = title;
}

void SimpleWifiManager::stop()
{
    log_i("SMW: Stopping");

    mState = State::Idle;
    mStopRequested = true;

    mDnsServer.stop();
    mServer.stop();

    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    WiFi.setAutoReconnect(false);

    mConnectStartTime = 0;
    mLastStaAttemptTime = 0;
    mApStaStartTime = 0;
    mOfflineTime = 0;

    mPrefs.end();
}

void SimpleWifiManager::setTimeout(uint32_t timeoutMs)
{
    mConnectTimeout = timeoutMs;
}

void SimpleWifiManager::setOfflineTimeout(uint32_t timeoutMs)
{
    mOfflineTimeout = timeoutMs;
}

void SimpleWifiManager::setRetryInterval(uint32_t intervalMs)
{
    mStaRetryInterval = intervalMs;
}

void SimpleWifiManager::setApAutoReconnect(bool enabled)
{
    mApAutoReconnectEnabled = enabled;
    WiFi.setAutoReconnect(enabled);
}

void SimpleWifiManager::setApFallback(bool enabled)
{
    mApFallbackEnabled = enabled;
}

void SimpleWifiManager::handlePortal()
{
#if !defined(SWM_DISABLE_GZIP)
    sendGzipResponse(HtmlPages::captiveSiteHtmlGzip, HtmlPages::captiveSiteHtmlGzipSize, "text/html; charset=utf-8");
#else
    mServer.send_P(200, "text/html; charset=utf-8", HtmlPages::captiveSiteHtml);
#endif
}

void SimpleWifiManager::handleScan()
{
    log_i("SMW: Scan request");
    const int count = WiFi.scanNetworks();
    String payload;
    payload.reserve(256);
    payload += "[";
    if (count > 0)
    {
        struct ApInfo {
            String ssid;
            int enc;
            int rssi;
            int idx;
        };
        std::vector<ApInfo> aps;
        for (int i = 0; i < count; ++i) {
            ApInfo ap{WiFi.SSID(i), WiFi.encryptionType(i), WiFi.RSSI(i), i};
            // Check for duplicates (same SSID and encryption) and skip them, keeping only the one with the strongest signal
            bool duplicate = false;
            for (const auto& existing : aps) {
                if (existing.ssid == ap.ssid && existing.enc == ap.enc) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                aps.push_back(ap);
            }
        }
        // Sort by RSSI descending
        std::sort(aps.begin(), aps.end(), [](const ApInfo& a, const ApInfo& b) {
            return a.rssi > b.rssi;
        });

        // Helper to escape characters in SSID for safe JSON strings
        auto escapeJson = [](const String &s) {
            String out;
            out.reserve(s.length() * 2);
            auto hex = [](uint8_t v) -> char { return v < 10 ? static_cast<char>('0' + v) : static_cast<char>('A' + (v - 10)); };
            for (size_t i = 0; i < s.length(); ++i) {
                uint8_t c = static_cast<uint8_t>(s[i]);
                switch (c) {
                    case '"': out += '\\'; out += '"'; break;
                    case '\\': out += '\\'; out += '\\'; break;
                    case '\b': out += '\\'; out += 'b'; break;
                    case '\f': out += '\\'; out += 'f'; break;
                    case '\n': out += '\\'; out += 'n'; break;
                    case '\r': out += '\\'; out += 'r'; break;
                    case '\t': out += '\\'; out += 't'; break;
                    default:
                        if (c < 0x20) {
                            out += '\\'; out += 'u'; out += '0'; out += '0';
                            out += hex((c >> 4) & 0xF);
                            out += hex(c & 0xF);
                        } else {
                            out += static_cast<char>(c);
                        }
                        break;
                }
            }
            return out;
        };

        for (size_t pos = 0; pos < aps.size(); ++pos) {
            const auto& ap = aps[pos];
            if (pos > 0) {
                payload += ',';
            }
            // Output as compact array: ["ssid",rssi,enc]
            payload += '[';
            payload += '"';
            payload += escapeJson(ap.ssid);
            payload += '"';
            payload += ',';
            payload += ap.rssi;
            payload += ',';
            payload += ap.enc;
            payload += ']';
        }
    }
    payload += "]";
    WiFi.scanDelete();
    mServer.send(200, "application/json", payload);
}

void SimpleWifiManager::handleSave()
{
    log_i("SMW: Save request");
    String ssid = mServer.arg("ssid");
    String pass = mServer.arg("password");

    if (ssid.length() == 0)
    {
        mServer.send(400, "text/plain", "SSID required");
        return;
    }

    mSsid = ssid;
    mPass = pass;
    saveCredentials();
    log_i("SMW: SSID %s saved", mSsid.c_str());
    mServer.send(200, "text/plain", "Credentials saved");
    mDnsServer.stop();
    mServer.stop();
    WiFi.softAPdisconnect(true);
    startStationConnect();
}

void SimpleWifiManager::sendGzipResponse(const uint8_t* data, size_t size, const char* contentType)
{
    log_i("SMW: Sending gzip response");
    mServer.sendHeader("Content-Encoding", "gzip");
    mServer.send_P(
        200,
        contentType,
        (const char*)data,
        size
    );
}

void SimpleWifiManager::handleRedirect()
{
    mServer.sendHeader("Location", "http://" + WiFi.softAPIP().toString(), true);
    mServer.send(302, "text/plain", "redirect to captive portal");
}

void SimpleWifiManager::handleCustom()
{
    String payload = "{\"title\":\"" + mCaptiveTitle + "\"}";
    mServer.send(200, "application/json", payload);
}

void SimpleWifiManager::startApMode()
{
    mState = State::ApMode;
    log_i("SMW: AP mode: %s", mApName.c_str());
    mApStaStartTime = 0;
    if (WiFi.getMode() == WIFI_STA || WiFi.getMode() == WIFI_AP_STA)
    {
        // Ensure STA is stopped before we reconfigure in AP/STA mode.
        WiFi.disconnect(true, true);
        delay(50);
    }
    WiFi.mode(WIFI_AP);

    // Start AP with captive portal enabled
    WiFi.AP.begin();
    WiFi.AP.create(mApName.c_str());
    WiFi.AP.enableDhcpCaptivePortal();
    mDnsServer.start();

    // Setup routes for captive portal
    mServer.on("/", HTTP_GET, [this]()
               { handlePortal(); });
    mServer.on("/portal", HTTP_GET, [this]()
               { handlePortal(); });
    mServer.on("/scan", HTTP_GET, [this]()
               { handleScan(); });
    mServer.on("/custom", HTTP_GET, [this]()
               { handleCustom(); });
    mServer.on("/", HTTP_POST, [this]()
               { handleSave(); });
    mServer.on("/generate_204", HTTP_GET, [this]()
               { handleRedirect(); });
    mServer.on("/connecttest.txt", HTTP_GET, [this]()
               { handleRedirect(); });
    mServer.on("/hotspot-detect.html", HTTP_GET, [this]()
               { handleRedirect(); });
    mServer.on("/fwlink", HTTP_GET, [this]()
               { handleRedirect(); });
    mServer.onNotFound([this]()
                       { handleRedirect(); });
    mServer.begin();

    startWorkerTask();
}

void SimpleWifiManager::startStationConnect()
{
    mState = State::Connecting;
    log_i("SMW: STA connect start ssid=%s", mSsid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.STA.connect(mSsid.c_str(), mPass.c_str());
    mConnectStartTime = millis();
    mLastStaAttemptTime = mConnectStartTime;
    mApStaStartTime = 0;

    startWorkerTask();
}

void SimpleWifiManager::startWorkerTask()
{
    // Avoid starting multiple worker tasks
    if (mWorkerTaskHandle != nullptr)
    {
        return;
    }

    mStopRequested = false;

    xTaskCreate(SimpleWifiManager::workerTask,
                "wifi_mgr",
                4096,
                this,
                1,
                &mWorkerTaskHandle);
}

void SimpleWifiManager::workerTask(void *parameter)
{
    SimpleWifiManager *self = static_cast<SimpleWifiManager *>(parameter);

    while (!self->mStopRequested)
    {
        const uint32_t now = millis();
        auto wifiStatus = WiFi.status();

        if (wifiStatus == WL_CONNECTED && self->mState != State::Connected)
        {
            log_i("SMW: STA got IP %s", WiFi.localIP().toString().c_str());
            if (self->mState == State::ApMode)
            {
                self->mDnsServer.stop();
                self->mServer.stop();
                WiFi.softAPdisconnect(true);
                WiFi.mode(WIFI_STA);
            }
            self->mState = State::Connected;
            self->mOfflineTime = 0;
        }

        switch (self->mState)
        {
        case State::Connecting:
            if (wifiStatus == WL_CONNECTED)
            {
                // State already updated above.
            }
            else if (now - self->mConnectStartTime >= self->mConnectTimeout)
            {
                if (self->mApFallbackEnabled)
                {
                    log_i("SMW: STA connect timeout, falling back to AP");
                    self->startApMode();
                }
            }
            break;
        case State::Connected:
            if (wifiStatus != WL_CONNECTED)
            {
                if (self->mOfflineTime == 0)
                {
                    self->mOfflineTime = now;
                }
                else if (now - self->mOfflineTime >= self->mOfflineTimeout)
                {
                    if (self->mApFallbackEnabled)
                    {
                        log_i("SMW: STA offline, falling back to AP");
                        self->startApMode();
                    }
                }
            }
            else
            {
                self->mOfflineTime = 0;
            }
            break;
        case State::ApMode:
            self->mDnsServer.processNextRequest();
            self->mServer.handleClient();
            if (self->mApAutoReconnectEnabled && self->mSsid.length() > 0 && now - self->mLastStaAttemptTime >= self->mStaRetryInterval)
            {
                // Retry STA connection while keeping the AP/captive portal alive.
                WiFi.mode(WIFI_AP_STA);
                WiFi.STA.connect(self->mSsid.c_str(), self->mPass.c_str());
                self->mLastStaAttemptTime = now;
                self->mApStaStartTime = now;
            }
            if (self->mApStaStartTime != 0 && now - self->mApStaStartTime >= self->mConnectTimeout && WiFi.status() != WL_CONNECTED)
            {
                // Fall back to AP-only to reduce power and avoid AP_STA linger.
                WiFi.mode(WIFI_AP);
                self->mApStaStartTime = 0;
            }
            break;
        default:
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(self->mTaskInterval));
    }

    self->mWorkerTaskHandle = nullptr;
    vTaskDelete(nullptr);
}

void SimpleWifiManager::loadCredentials()
{
    mSsid = mPrefs.getString("ssid", "");
    mPass = mPrefs.getString("pass", "");
}

void SimpleWifiManager::saveCredentials()
{
    mPrefs.putString("ssid", mSsid.c_str());
    mPrefs.putString("pass", mPass.c_str());
}

void SimpleWifiManager::resetCredentials()
{
    log_i("SMW: Resetting credentials");
    mPrefs.clear();
    delay(100);
    ESP.restart();
}

bool SimpleWifiManager::isConnected() const
{
    return mState == State::Connected;
}
