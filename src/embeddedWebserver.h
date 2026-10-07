/**
 * @file embeddedWebserver.h
 *
 * @brief Embedded webserver
 *
 */

#pragma once

#include <Arduino.h>
#include <memory>

#include "FS.h"
#include <AsyncTCP.h>
#include <WiFi.h>

#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>

#include "LittleFS.h"
#include "webRequestGate.h"

inline AsyncWebServer server(80);
inline AsyncEventSource events("/events");

inline double curTemp = 0.0;
inline double tTemp = 0.0;
inline double hPower = 0.0;

#define HISTORY_LENGTH 600 // 20 mins of values (30 vals/min * 20 min) = 600 (3.6kb)

static int16_t tempHistory[3][HISTORY_LENGTH] = {};
inline int historyCurrentIndex = 0;
inline int historyValueCount = 0;

void serverSetup();

inline bool authenticate(AsyncWebServerRequest* request) {
    if (!config.get<bool>("system.auth.enabled")) {
        return true;
    }

    const auto clientIP = request->client()->remoteIP().toString();
    const auto requestedPath = request->url();
    const auto username = config.get<String>("system.auth.username");
    const auto password = config.get<String>("system.auth.password");

    if (request->authenticate(username.c_str(), password.c_str())) {
        LOGF(DEBUG, "Web auth OK: %s -> %s", clientIP.c_str(), requestedPath.c_str());

        return true;
    }

    if (request->hasHeader("Authorization")) {
        LOGF(WARNING, "Web auth FAIL: %s -> %s (wrong credentials)", clientIP.c_str(), requestedPath.c_str());
    }
    else {
        LOGF(DEBUG, "Web auth required: %s -> %s", clientIP.c_str(), requestedPath.c_str());
    }

    return false;
}

inline uint8_t flipUintValue(const uint8_t value) {
    return (value + 3) % 2;
}

inline String getTempString() {
    JsonDocument doc;

    doc["currentTemp"] = curTemp;
    doc["targetTemp"] = tTemp;
    doc["heaterPower"] = hPower;
#ifdef CC_ORIONE
    doc["state"] = static_cast<int>(machineState); // the Orione web page shows heating/ready/shot/standby/errors
    doc["brewTime"] = round(currBrewTime / 100.0) / 10.0;
#endif

    String jsonTemps;
    serializeJson(doc, jsonTemps);

    return jsonTemps;
}

#ifdef CC_ORIONE
/**
 * Live values go out from their own task on core 0, where the web server runs: writing to a client
 * waits for the TCP/IP task, measured up to 134 ms (02.10.2026), and loop() must not wait for it
 * (display, brew stop by time or weight, scale). loop() only leaves a copy and a notification.
 */
namespace live_events {

    struct Values {
            double temp;
            double target;
            double power;
            int state;
            double brewTime;
            int scale;     // 0 no scale, 1 not connected, 2 connected
            double weight; // brew weight while brewing, the scale's reading otherwise
            double flow;   // g/s during a shot, < 0 otherwise
            int battery;   // the scale's battery in percent, < 0 unknown
            int warmup;    // warm-up flush: orione::WarmupFlush::Phase
            int pulse;     // its pulse, 0 when not running
            double cup;    // while the drops after a shot are counted: in the cup since the start, < 0 otherwise
            bool held;     // the shot stopped by itself and the brew switch is still on
            bool sw;       // the brew switch is on (or not back off yet)
            int steam;     // steam from the original switch, seen from the temperature: 0 no, 1 steam, 2 cooling down after it
    };

    inline portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
    inline Values latest{};
    inline TaskHandle_t task = nullptr;

    inline void run(void*) {
        char json[240];

        for (;;) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            portENTER_CRITICAL(&lock);
            const Values v = latest;
            portEXIT_CRITICAL(&lock);

            web_gate::sseClients = static_cast<uint8_t>(events.count());
            web_gate::sseWaiting = static_cast<uint8_t>(events.avgPacketsWaiting());

            if (events.count() > 0) {
                int n = snprintf(json, sizeof(json), R"({"currentTemp":%.2f,"targetTemp":%.2f,"heaterPower":%.1f,"state":%d,"brewTime":%.1f,"scale":%d)", v.temp, v.target, v.power, v.state, v.brewTime, v.scale);
                n += snprintf(json + n, sizeof(json) - n, v.scale == 2 && std::isfinite(v.weight) ? R"(,"weight":%.1f)" : R"(,"weight":null)", v.weight);
                n += snprintf(json + n, sizeof(json) - n, v.flow >= 0 ? R"(,"flow":%.2f)" : R"(,"flow":null)", v.flow);
                n += snprintf(json + n, sizeof(json) - n, v.scale == 2 && v.battery >= 0 ? R"(,"battery":%d)" : R"(,"battery":null)", v.battery);
                n += snprintf(json + n, sizeof(json) - n, R"(,"warmup":%d,"pulse":%d)", v.warmup, v.pulse);
                n += snprintf(json + n, sizeof(json) - n, v.cup >= 0 ? R"(,"cup":%.1f)" : R"(,"cup":null)", v.cup);
                snprintf(json + n, sizeof(json) - n, R"(,"held":%s,"sw":%s,"steam":%d})", v.held ? "true" : "false", v.sw ? "true" : "false", v.steam);
                events.send(json, "new_temps", millis());
            }
        }
    }

    inline void begin() {
        if (task == nullptr) {
            xTaskCreatePinnedToCore(run, "sse", 3072, nullptr, 1, &task, 0);
        }
    }

    inline void publish(const Values& v) {
        portENTER_CRITICAL(&lock);
        latest = v;
        portEXIT_CRITICAL(&lock);

        if (task != nullptr) {
            xTaskNotifyGive(task);
        }
    }

} // namespace live_events
#endif

// proper modulo function (% is remainder, so will return negatives)
inline int mod(const int a, const int b) {
    const int r = a % b;
    return r < 0 ? r + b : r;
}

// rounds a number to 2 decimal places
// example: round(3.14159) -> 3.14
// (less characters when serialized to json)
extern const char sysVersion[64];

inline double round2(const double value) {
    return std::round(value * 100.0) / 100.0;
}

inline void paramToJson(const String& name, const std::shared_ptr<Parameter>& param, JsonVariant doc) {
    doc["type"] = param->getType();
    doc["name"] = name;
    doc["displayName"] = param->getDisplayName();
    doc["section"] = param->getSection();
    doc["position"] = param->getPosition();
    doc["hasHelpText"] = param->hasHelpText();
    doc["show"] = param->shouldShow();
    doc["reboot"] = param->requiresReboot();

    // Set parameter value using the appropriate method based on type
    switch (param->getType()) {
        case kInteger:
            doc["value"] = static_cast<int>(param->getValue());
            break;

        case kUInt8:
            doc["value"] = static_cast<uint8_t>(param->getValue());
            break;

        case kDouble:
            doc["value"] = round2(param->getValue());
            break;

        case kFloat:
            doc["value"] = round2(static_cast<float>(param->getValue()));
            break;

        case kCString:
            doc["value"] = param->getStringValue();
            break;

        case kEnum:
            {
                doc["value"] = static_cast<int>(param->getValue());

                const JsonArray options = doc["options"].to<JsonArray>();
                const char* const* enumOptions = param->getEnumOptions();
                const size_t enumCount = param->getEnumCount();

                for (size_t i = 0; i < enumCount && enumOptions[i] != nullptr; i++) {
                    auto optionObj = options.add<JsonObject>();
                    optionObj["value"] = static_cast<int>(i);
                    optionObj["label"] = enumOptions[i];
                }

                break;
            }

        default:
            doc["value"] = param->getValue();
            break;
    }

    doc["min"] = param->getMinValue();
    doc["max"] = param->getMaxValue();
}

inline void serverSetup() {
#ifndef CC_ORIONE // no firmware steam mode / no HX711 calibration on the Orione
    server.on("/toggleSteam", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!authenticate(request)) {
            return request->requestAuthentication();
        }

        const bool steamMode = !steamON;
        setSteamMode(steamMode);

        LOGF(DEBUG, "Toggle steam mode: %s", steamON ? "on" : "off");

        request->redirect("/");
    });
#endif

    server.on("/togglePid", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!authenticate(request)) {
            return request->requestAuthentication();
        }

        LOGF(DEBUG, "/togglePid requested, method: %d", request->method());

        const auto pidParam = ParameterRegistry::getInstance().getParameterById("pid.enabled");
        const bool newPidState = !pidParam->getValueAs<bool>();
        ParameterRegistry::getInstance().setParameterValue("pid.enabled", newPidState);

        pidON = newPidState;

        LOGF(DEBUG, "Toggle PID state: %d\n", newPidState);

        request->redirect("/");
    });

    server.on("/toggleBackflush", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!authenticate(request)) {
            return request->requestAuthentication();
        }

        backflushOn = !backflushOn;
        LOGF(DEBUG, "Toggle backflush mode: %s", backflushOn ? "on" : "off");

        request->redirect("/");
    });

    if (config.get<bool>("hardware.sensors.scale.enabled")) {
        server.on("/toggleTareScale", HTTP_POST, [](AsyncWebServerRequest* request) {
            if (!authenticate(request)) {
                return request->requestAuthentication();
            }

            scaleTareOn = !scaleTareOn;

            LOGF(DEBUG, "Toggle scale tare mode: %s", scaleTareOn ? "on" : "off");

            request->redirect("/");
        });

#ifndef CC_ORIONE // no firmware steam mode / no HX711 calibration on the Orione
        server.on("/toggleScaleCalibration", HTTP_POST, [](AsyncWebServerRequest* request) {
            if (!authenticate(request)) {
                return request->requestAuthentication();
            }

            scaleCalibrationOn = !scaleCalibrationOn;

            LOGF(DEBUG, "Toggle scale calibration mode: %s", scaleCalibrationOn ? "on" : "off");

            request->redirect("/");
        });
#endif
    }

    server.on("/parameters", WEB_GATED([](AsyncWebServerRequest* request) {
        if (!request->client() || !request->client()->connected()) {
            return;
        }

        if (!authenticate(request)) {
            return request->requestAuthentication();
        }

        if (request->method() == 1) { // HTTP_GET
            const auto& registry = ParameterRegistry::getInstance();
            const auto& parameters = registry.getParameters();

            // Check for filter parameter
            String filterType = "";
            if (request->hasParam("filter")) {
                filterType = request->getParam("filter")->value();
            }

            // A view can name the parameters it shows instead of asking for a whole
            // section range and picking from the result. Wrapped in commas so a plain
            // indexOf matches whole names only.
            String names = "";
            if (request->hasParam("names")) {
                names = "," + request->getParam("names")->value() + ",";
            }

            // Defaults
            int offset = 0;
            int limit = 5;

            if (request->hasParam("offset")) {
                offset = request->getParam("offset")->value().toInt();
            }

            if (request->hasParam("limit")) {
                limit = request->getParam("limit")->value().toInt();
            }

            // Clamp what a caller can ask for. The response is written into a cbuf that
            // grows by resizeAdd(), one value at a time -- an unbounded limit means
            // thousands of reallocations of a steadily growing block, which fragments
            // the heap until an allocation fails, and operator new throws here where
            // nothing catches it. Asking for more than the registry holds cannot return
            // more anyway. The web UI pages with limit=5 and is unaffected.
            if (limit > static_cast<int>(parameters.size())) {
                limit = static_cast<int>(parameters.size());
            }

            if (limit < 0) {
                limit = 0;
            }

            AsyncResponseStream* response = request->beginResponseStream("application/json");
            response->print("{\"parameters\":[");

            bool first = true;
            int filteredParameterCount = 0;
            int sent = 0;

            // Get parameters based on filter
            for (const auto& param : parameters) {
                if (!param->shouldShow()) {
                    continue;
                }

                bool includeParam = false;

                if (names.length() > 0) {
                    includeParam = names.indexOf("," + String(param->getId()) + ",") >= 0;
                }
                else if (filterType == "hardware") {
                    includeParam = param->getSection() >= 11 && param->getSection() <= 15;
                }
                else if (filterType == "behavior") {
                    includeParam = param->getSection() >= 0 && param->getSection() <= 9;
                }
                else if (filterType == "other") {
                    includeParam = param->getSection() == 10;
                }
                else if (filterType == "all") {
                    includeParam = true;
                }
                else {
                    includeParam = param->getSection() == 0 || param->getSection() == 1 || param->getSection() == 10;
                }

                if (includeParam) {
                    if (filteredParameterCount++ < offset) {
                        continue;
                    }

                    if (sent >= limit) {
                        break;
                    }

                    if (!first) {
                        response->print(",");
                    }

                    first = false;

                    JsonDocument doc;
                    paramToJson(param->getId(), param, doc.to<JsonVariant>());
                    serializeJson(doc, *response);

                    sent++;
                    LOGF(DEBUG, "[Heap] Free: %u  MaxAlloc: %u, Param: %d", ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT), filteredParameterCount);
                }
            }

            response->printf(R"(],"offset":%d,"limit":%d,"returned":%d})", offset, limit, sent);
            request->send(response);
        }
        else if (request->method() == 2) { // HTTP_POST
            auto& registry = ParameterRegistry::getInstance();

            String responseMessage = "OK";
            bool hasErrors = false;

            const auto requestParams = request->params();

#ifdef CC_ORIONE
            // grind, grinder and beans may be emptied; everything else (host name, passwords, numbers) not
            const auto mayBeEmpty = [](const String& name) { return name == "brew.grind" || name == "brew.grinder" || name == "brew.beans"; };
#else
            const auto mayBeEmpty = [](const String&) { return false; };
#endif

            for (auto i = 0u; i < requestParams; ++i) {
                if (auto* p = request->getParam(i); p && p->name().length() > 0 && (p->value().length() > 0 || mayBeEmpty(p->name()))) {
                    const String& varName = p->name();
                    const String& value = p->value();

                    try {
                        std::shared_ptr<Parameter> paramPtr = registry.getParameterById(varName.c_str());

                        if (paramPtr == nullptr || !paramPtr->shouldShow()) {
                            continue;
                        }

                        if (paramPtr->getType() == kCString) {
                            registry.setParameterValue(varName.c_str(), value);
                        }
                        else {
                            double newVal = std::stod(value.c_str());
                            registry.setParameterValue(varName.c_str(), newVal);
                        }
                    } catch (const std::exception& e) {
                        LOGF(INFO, "Parameter %s processing failed: %s", varName.c_str(), e.what());
                        hasErrors = true;
                    }
                }
            }

            registry.forceSave();
            writeSysParamsToMQTT(true);

            AsyncWebServerResponse* response = request->beginResponse(200, "text/plain", hasErrors ? "Partial Success" : "OK");
            response->addHeader("Connection", "close");
            request->send(response);
        }
        else {
            LOGF(ERROR, "Unsupported HTTP method %d for /parameters", request->method());
            AsyncWebServerResponse* response = request->beginResponse(405, "text/plain", "Method Not Allowed");
            response->addHeader("Connection", "close");
            request->send(response);
        }
    }));

    server.on("/parameterHelp", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;
        auto* p = request->getParam(0);

        if (p == nullptr) {
            request->send(422, "text/plain", "parameter is missing");
            return;
        }

        const String& varValue = p->value();

        const std::shared_ptr<Parameter> param = ParameterRegistry::getInstance().getParameterById(varValue.c_str());

        if (param == nullptr) {
            request->send(404, "application/json", "parameter not found");
            return;
        }

        doc["name"] = varValue;
        doc["helpText"] = param->getHelpText();

        String helpJson;
        serializeJson(doc, helpJson);
        request->send(200, "application/json", helpJson);
    });

    // Replaces the %VAR_SHOW_VERSION% template placeholder. With it gone, the pages
    // need no template processor and can be served static and pre-compressed.
    server.on("/version", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", sysVersion);
    });

#ifdef CC_ORIONE
    // why the ESP32 last started, for how long it has been running (orioneMachine.h), and its memory: the heap now,
    // its lowest point since the start and largest block, and for each task the stack it has never used (bytes)
    server.on("/boot", HTTP_GET, [](AsyncWebServerRequest* request) {
        const auto unused = [](const char* name) -> long {
            TaskHandle_t t = xTaskGetHandle(name);
            return t != nullptr ? static_cast<long>(uxTaskGetStackHighWaterMark(t)) : -1L;
        };
        char json[300];
        snprintf(json, sizeof(json),
                 R"({"reason":"%s","uptime":%lu,"heap":%u,"heapMin":%u,"block":%u,"stackUnused":{"loop":%ld,"tcp":%ld,"ble":%ld,"scale":%ld,"sse":%ld,"guard":%ld}})",
                 orione_machine::startReason, static_cast<unsigned long>(millis() / 1000), static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
                 static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)), static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
                 unused("loopTask"), unused("async_tcp"), unused("nimble_host"), unused("scale"), unused("sse"), unused("loopGuard"));
        request->send(200, "application/json", json);
    });
#endif

    server.on("/temperatures", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        response->print('{');
        response->print("\"currentTemp\":");
        response->print(curTemp, 2);
        response->print(",\"targetTemp\":");
        response->print(tTemp, 2);
        response->print(",\"heaterPower\":");
        response->print(hPower, 2);
        response->print('}');
        request->send(response);
    });

#ifdef CC_FAKE_TEMP_SENSOR
    // bench build only: virtual brew switch (src/benchSwitch.h)
    server.on("/bench/brew", HTTP_POST, [](AsyncWebServerRequest* request) {
        const long s = request->hasParam("s") ? request->getParam("s")->value().toInt() : 25;
        bench::holdBrewSwitch(static_cast<uint32_t>(constrain(s, 0L, 120L)));
        request->send(200, "text/plain", s > 0 ? "brew switch on" : "brew switch off");
    });

    server.on("/bench/tank", HTTP_POST, [](AsyncWebServerRequest* request) {
        bench::tankEmpty = request->hasParam("empty") && request->getParam("empty")->value() == "1";
        request->send(200, "text/plain", bench::tankEmpty ? "tank empty" : "tank full");
    });

#ifdef CC_FAKE_SCALE
    server.on("/bench/scale", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (request->hasParam("off")) {
            bench::scaleOff = request->getParam("off")->value() == "1";
        }

        if (request->hasParam("miss")) {
            bench::scaleMissed = request->getParam("miss")->value() == "1";
        }

        if (request->hasParam("cup")) {
            bench::scaleAddGrams = bench::scaleAddGrams + request->getParam("cup")->value().toFloat();
        }

        request->send(200, "text/plain", "OK");
    });
#endif

    server.on("/bench/wifi-outage", HTTP_POST, [](AsyncWebServerRequest* request) {
        const long s = request->hasParam("s") ? request->getParam("s")->value().toInt() : 180;
        bench::wifiOutageUntilMs = millis() + static_cast<uint32_t>(constrain(s, 10L, 1800L)) * 1000;
        bench::wifiOutageStart = true;
        request->send(200, "text/plain", "router off");
    });

#endif
#ifdef CC_ORIONE
    // Bluetooth scale chosen on the web page (Einstellungen → Waage). state: 0 off, 1 chosen but not
    // connected, 2 connected, 3 none chosen. Names come from any scale around: ArduinoJson escapes them.
    server.on("/scale", HTTP_GET, [](AsyncWebServerRequest* request) {
        auto* ble = isBluetoothScale && scale != nullptr ? static_cast<BluetoothScale*>(scale) : nullptr;
        JsonDocument doc;
        doc["state"] = !ble || !config.get<bool>("hardware.sensors.scale.enabled") ? 0 : ble->isConnected() ? 2 : ble->hasTarget() ? 1 : 3;
        const auto setting = [](const char* key) { // never saved yet: Config answers "null"
            const String v = config.get<String>(key);
            return v == "null" ? String() : v;
        };
        doc["address"] = setting("hardware.sensors.scale.address");
        doc["name"] = setting("hardware.sensors.scale.name");
        const int battery = ble ? ble->getBattery() : -1;
        if (battery >= 0) {
            doc["battery"] = battery;
        }
        doc["searching"] = ble != nullptr && ble->discovering();
        JsonArray list = doc["found"].to<JsonArray>();
        orione::FoundScale found[orione::ScaleList::kMax];
        for (int i = 0, n = ble ? ble->found(found, orione::ScaleList::kMax) : 0; i < n; ++i) {
            JsonObject f = list.add<JsonObject>();
            f["name"] = found[i].name;
            f["address"] = found[i].address;
            f["rssi"] = found[i].rssi;
        }
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });

    server.on("/scale/discover", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!isBluetoothScale || scale == nullptr) {
            return request->send(409, "text/plain", "scale off");
        }
        static_cast<BluetoothScale*>(scale)->requestDiscover();
        request->send(202, "text/plain", "ok");
    });

    // ?address=aa:bb:cc:dd:ee:ff&name=...: remembered (setting) and connected from now on
    server.on("/scale/select", HTTP_POST, [](AsyncWebServerRequest* request) {
        char address[18];
        orione::ScaleList::normalize(request->hasParam("address") ? request->getParam("address")->value().c_str() : "", address);
        if (!isBluetoothScale || scale == nullptr || address[0] == '\0') {
            return request->send(400, "text/plain", "no such scale");
        }
        String name;
        for (const char c : request->hasParam("name") ? request->getParam("name")->value() : String()) {
            if (c >= 0x20 && c < 0x7f && name.length() < 23) {
                name += c; // printable ASCII, as scales advertise themselves
            }
        }
        auto& registry = ParameterRegistry::getInstance();
        registry.setParameterValue<String>("hardware.sensors.scale.address", String(address));
        registry.setParameterValue<String>("hardware.sensors.scale.name", name);
        static_cast<BluetoothScale*>(scale)->requestTarget(address);
        request->send(202, "text/plain", "ok");
    });

    server.on("/scale/forget", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!isBluetoothScale || scale == nullptr) {
            return request->send(409, "text/plain", "scale off");
        }
        auto& registry = ParameterRegistry::getInstance();
        registry.setParameterValue<String>("hardware.sensors.scale.address", String());
        registry.setParameterValue<String>("hardware.sensors.scale.name", String());
        static_cast<BluetoothScale*>(scale)->requestTarget("");
        request->send(202, "text/plain", "ok");
    });

    // warm-up flush by hand (Wartung): ?start=1 or ?stop=1, applied by loop(); only with a water level sensor
    server.on("/flush", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!warmup_flush::sensorEnabled()) {
            return request->send(409, "text/plain", "no water level sensor");
        }

        warmup_flush::requestFromWeb(!request->hasParam("stop"));
        request->send(202, "text/plain", "ok");
    });

    server.on("/shots", HTTP_GET, WEB_GATED([](AsyncWebServerRequest* request) {
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        shot_history::writeJson(*response);
        request->send(response);
    }));

    server.on("/shot/rate", HTTP_POST, [](AsyncWebServerRequest* request) {
        const long i = request->hasParam("i") ? request->getParam("i")->value().toInt() : -1;
        const long t = request->hasParam("t") ? request->getParam("t")->value().toInt() : -1;

        if (i < 0 || i >= shot_history::shotLog.count() || t < 0 || t > orione::kBitter) {
            request->send(400, "text/plain", "bad rating");
            return;
        }

        shot_history::requestRating(static_cast<int>(i), static_cast<uint8_t>(t));
        request->send(200, "text/plain", "OK");
    });

    // delete shot i (a test at the bench, a flush that counted); at and s say which one the page means
    server.on("/shot/delete", HTTP_POST, [](AsyncWebServerRequest* request) {
        const long i = request->hasParam("i") ? request->getParam("i")->value().toInt() : -1;
        const uint32_t at = request->hasParam("at") ? static_cast<uint32_t>(request->getParam("at")->value().toInt()) : 0;
        const float s = request->hasParam("s") ? request->getParam("s")->value().toFloat() : -1.0f;

        if (!shot_history::isShot(static_cast<int>(i), at, s)) {
            request->send(409, "text/plain", "not that shot");
            return;
        }

        shot_history::requestDelete(static_cast<int>(i), at, s);
        request->send(200, "text/plain", "OK");
    });

    server.on("/shot", HTTP_GET, WEB_GATED([](AsyncWebServerRequest* request) {
        const int i = request->hasParam("i") ? request->getParam("i")->value().toInt() : 0;
        AsyncResponseStream* response = request->beginResponseStream("application/json");

        if (!shot_history::writeCurveJson(i, *response)) {
            delete response;
            request->send(404, "text/plain", "no curve");
            return;
        }

        request->send(response);
    }));

#endif
    server.on("/timeseries", HTTP_GET, WEB_GATED([](AsyncWebServerRequest* request) {
        // Chunked, so a response never needs more memory than the server's send buffer
        struct TsState {
                int start = 0;
                int count = 0;
                int next = 0; // next token to send
        };

        auto st = std::make_shared<TsState>();
        // count before index, sendTempEvent() updates the index first
        st->count = historyValueCount;
        st->start = mod(historyCurrentIndex - st->count, HISTORY_LENGTH);

        static constexpr size_t tokenCap = 24;
        static_assert(sizeof("],\"heaterPowers\":[") <= tokenCap, "token buffer too small");

        AsyncWebServerResponse* response = request->beginChunkedResponse("application/json", [st](uint8_t* buffer, size_t maxLen, size_t) -> size_t {
            static constexpr const char* headers[] = {"{\"currentTemps\":[", "],\"targetTemps\":[", "],\"heaterPowers\":["};

            // Tokens: per array a header and count values, then "]}"
            const int perArray = st->count + 1;
            const int last = 3 * perArray;
            size_t written = 0;

            // Whole tokens only. maxLen is at least 710 bytes, a token at most 18.
            while (st->next <= last) {
                const int k = st->next;
                char tok[tokenCap];
                size_t n;

                if (k == last) {
                    n = snprintf(tok, sizeof(tok), "]}");
                }
                else if (k % perArray == 0) {
                    n = snprintf(tok, sizeof(tok), "%s", headers[k / perArray]);
                }
                else {
                    const int i = k % perArray - 1;
                    n = snprintf(tok, sizeof(tok), "%s%.2f", i > 0 ? "," : "", tempHistory[k / perArray][mod(st->start + i, HISTORY_LENGTH)] * 0.01f);
                }

                if (written + n > maxLen) {
                    break;
                }

                memcpy(buffer + written, tok, n);
                written += n;
                st->next++;
            }

            return written; // 0 ends the response
        });

        response->addHeader("Connection", "close");
        request->send(response);
    }));

    server.on("/wifireset", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!authenticate(request)) {
            return request->requestAuthentication();
        }

        request->send(200, "text/plain", "WiFi settings are being reset. Rebooting...");

        if (u8g2 != nullptr) {
            u8g2->setPowerSave(1);
        }
        // Defer slightly so the response gets sent before reboot
        delay(1000);

        wiFiReset();
    });

    server.on("/download/config", HTTP_GET, WEB_GATED([](AsyncWebServerRequest* request) {
        if (!authenticate(request)) {
            return request->requestAuthentication();
        }

        if (!LittleFS.exists("/config.json")) {
            request->send(404, "text/plain", "Config file not found");
            return;
        }

        File configFile = LittleFS.open("/config.json", "r");

        if (!configFile) {
            request->send(500, "text/plain", "Failed to open config file");
            return;
        }

        JsonDocument doc;
        const DeserializationError error = deserializeJson(doc, configFile);
        configFile.close();

        if (error) {
            request->send(500, "text/plain", "Failed to parse config file");
            return;
        }

        // Serialize as pretty JSON
        String prettifiedJson;
        serializeJsonPretty(doc, prettifiedJson);

        // Send the prettified JSON
        AsyncWebServerResponse* response = request->beginResponse(200, "application/json", prettifiedJson);
        response->addHeader("Content-Disposition", "attachment; filename=\"config.json\"");
        request->send(response);
    }));

    server.on(
        "/upload/config", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            // This response will be set by the upload handler
        },
        [](AsyncWebServerRequest* request, const String& filename, const size_t index, const uint8_t* data, const size_t len, const bool final) {
            if (!authenticate(request)) {
                return request->requestAuthentication();
            }

            static String uploadBuffer;
            static size_t totalSize = 0;

            if (index == 0) {
                uploadBuffer = "";
                uploadBuffer.reserve(8192);
                totalSize = 0;
                LOGF(INFO, "Config upload started: %s", filename.c_str());
            }

            for (size_t i = 0; i < len; i++) {
                uploadBuffer += static_cast<char>(data[i]);
            }

            totalSize += len;

            if (final) {
                LOGF(INFO, "Config upload finished: %s, total size: %u bytes", filename.c_str(), totalSize);

                if (config.validateAndApplyFromJson(uploadBuffer)) {
                    LOG(INFO, "Configuration validated and applied successfully");

                    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", R"({"success": true, "message": "Configuration validated and applied successfully.", "restart": true})");

                    response->addHeader("Connection", "close");
                    request->send(response);
                }
                else {
                    LOG(ERROR, "Configuration validation failed - invalid data or out of range values");

                    AsyncWebServerResponse* response =
                        request->beginResponse(400, "application/json", R"({"success": false, "message": "Configuration validation failed. Please check that all parameter values are within valid ranges.", "restart": true})");

                    response->addHeader("Connection", "close");
                    request->send(response);
                }
            }
        });

    server.on("/restart", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!authenticate(request)) {
            return request->requestAuthentication();
        }

        request->send(200, "text/plain", "Restarting...");

        if (u8g2 != nullptr) {
            u8g2->setPowerSave(1);
        }

        delay(100);
        ESP.restart();
    });

    server.on("/factoryreset", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!authenticate(request)) {
            return request->requestAuthentication();
        }

        const bool removed = LittleFS.remove("/config.json");

        request->send(200, "text/plain", removed ? "Factory reset. Restarting..." : "Could not delete config.json. Restarting...");

        if (u8g2 != nullptr) {
            u8g2->setPowerSave(1);
        }

        delay(100);
        ESP.restart();
    });

    server.onNotFound([](AsyncWebServerRequest* request) { request->send(404, "text/plain", "Not found"); });

    // set up event handler for temperature messages
    events.onConnect([](AsyncEventSourceClient* client) {
        if (client->lastId()) {
            LOGF(DEBUG, "Reconnected, last message ID was: %u", client->lastId());
        }

        client->send("hello", nullptr, millis(), 10000);
#ifdef CC_ORIONE
        // A phone that keeps the connection but stops reading (locked, tab in the background) would
        // tie up to 16 KB here; heap ran down to an 11 KB block with the brake on (02.10.2026)
        client->set_max_inflight_bytes(SSE_MIN_INFLIGH);
#endif
    });

    server.addHandler(&events);
#ifdef CC_ORIONE
    live_events::begin();
#endif

    // The four pages became tabs of one page, addressed by hash. Redirect the old URLs
    // so existing bookmarks still land on the right tab.
    server.on("/parameters.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        const AsyncWebParameter* filter = request->getParam("filter");
        request->redirect(filter && filter->value() == "hardware" ? "/#hardware" : "/#settings");
    });

    server.on("/system.html", HTTP_GET, [](AsyncWebServerRequest* request) { request->redirect("/#system"); });
    server.on("/about.html", HTTP_GET, [](AsyncWebServerRequest* request) { request->redirect("/#about"); });

    // serve static files
    LittleFS.begin();
#ifdef CC_ORIONE
    // The Orione page is one gzipped file (frontend-orione/, built by orione_frontend.py). no-cache:
    // the browser asks every time but gets a 304 by ETag as long as the file did not change.
    web_gate::serveStatic(server, "/fonts/", LittleFS, "/html/fonts/", "max-age=31536000, immutable"); // a new font gets a new name
    web_gate::serveStatic(server, "/", LittleFS, "/html/", "no-cache").setDefaultFile("index.html");
#else
    server.serveStatic("/css", LittleFS, "/css/", "max-age=604800"); // cache for one week
    server.serveStatic("/js", LittleFS, "/js/", "max-age=604800");
    server.serveStatic("/img", LittleFS, "/img/", "max-age=604800"); // cache for one week
    server.serveStatic("/manifest.json", LittleFS, "/manifest.json", "max-age=604800");
    server.serveStatic("/", LittleFS, "/html/", "max-age=604800").setDefaultFile("index.html");
#endif

    server.begin();

    if (offlineMode) {
        LOG(INFO, ("Server started at " + WiFi.softAPIP().toString()).c_str());
    }
    else {
        LOG(INFO, ("Server started at " + WiFi.localIP().toString()).c_str());
    }
}

// skip counter so we don't keep a value every second
inline int skippedValues = 0;
#define SECONDS_TO_SKIP 2

inline void sendTempEvent(const double currentTemp, const double targetTemp, const double heaterPower) {
    curTemp = currentTemp;
    tTemp = targetTemp;
    hPower = heaterPower;

    // save all values in memory to show history
    if (skippedValues > 0 && skippedValues % SECONDS_TO_SKIP == 0) {
        // use array and int value for start index (round robin)
        // one record (3 int values == 6 bytes) every two seconds, for twenty
        // minutes -> 3.6kB of static memory
        tempHistory[0][historyCurrentIndex] = static_cast<int16_t>(currentTemp * 100);
        tempHistory[1][historyCurrentIndex] = static_cast<int16_t>(targetTemp * 100);
        tempHistory[2][historyCurrentIndex] = static_cast<int16_t>(heaterPower * 100);
        historyCurrentIndex = (historyCurrentIndex + 1) % HISTORY_LENGTH;
        historyValueCount = min(HISTORY_LENGTH - 1, historyValueCount + 1);
        skippedValues = 0;
    }
    else {
        skippedValues++;
    }

#ifdef CC_ORIONE
    // the values themselves keep the connection alive, no extra "ping"
    // 0 off, 1 not connected, 2 connected, 3 no scale chosen (GET /scale)
    const int scaleState = scale == nullptr || !config.get<bool>("hardware.sensors.scale.enabled") ? 0
                         : scale->isConnected()                                                ? 2
                         : isBluetoothScale && !static_cast<BluetoothScale*>(scale)->hasTarget() ? 3
                                                                                                 : 1;
    live_events::publish({currentTemp, targetTemp, heaterPower, static_cast<int>(machineState), round(currBrewTime / 100.0) / 10.0, scaleState,
                          checkBrewActive() ? currBrewWeight : currReadingWeight, shot_history::liveFlow(scaleState == 2), scaleBatteryPercent(),
                          warmup_flush::livePhase(), warmup_flush::flush.pulse(),
                          scaleState == 2 && shot_history::shotLog.settling() ? std::max(0.0, static_cast<double>(currReadingWeight - preBrewWeight)) : -1.0,
                          brewSwitchHeldAfterBrew(), currBrewSwitchState != kBrewSwitchIdle, static_cast<int>(orione_machine::steam.phase())});
#else
    if (events.count() > 0) {
        events.send("ping", nullptr, millis());
        events.send(getTempString().c_str(), "new_temps", millis());
    }
#endif
}
