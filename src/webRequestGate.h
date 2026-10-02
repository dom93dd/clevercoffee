/**
 * @file webRequestGate.h
 *
 * @brief Orione build (-D CC_ORIONE) only: lets just a few web responses run at once.
 *
 * Each response holds 10-30 KB of heap until the phone has acknowledged the data (lwIP keeps up to
 * 5.7 KB per connection, plus the response buffers). A browser opens up to six connections at
 * once; with WiFi, Bluetooth and the round display the heap of a WROOM-32 cannot take that, and
 * an allocation that fails inside the web server ends in abort(). Measured on 02.10.2026: 38 KB
 * free, page load with five files in parallel -> 12 KB free, largest block 2 KB, page hangs.
 *
 * So: one response at a time (a second one in parallel, if GateLimits allow it, only while enough
 * heap is free); further requests are paused (request continuation, pause()) and answered one
 * after another as responses finish. The decision itself: lib/Orione/src/OrioneWebGate.h.
 * Same idea as WLED's request queue (Will Miles) and the ESP32Async maintainers' advice
 * (ESPAsyncWebServer discussions #319 and #366). Everything here runs in the async_tcp task
 * (handlers and disconnect callbacks), so no lock is needed.
 *
 * Emergency brake (GateLimits): below brakeBlock / brakeFree a request is answered 503 right away,
 * below abortBlock the connection is reset without allocating anything. Measured on 02.10.2026: with
 * the heap broken into small pieces the ESP32 dropped out of the WiFi (no ARP replies while the
 * firmware still reported "connected") and only came back with a restart. The thresholds lie
 * below the lowest value of the load test (23 KB free), so normal page loads never hit them.
 */

#pragma once

#ifdef CC_ORIONE

#include <ESPAsyncWebServer.h>
#include <OrioneWebGate.h> // the decision, unit-tested
#include <esp_heap_caps.h>

#include <deque>
#include <functional>

namespace web_gate {

    constexpr size_t kFileBuffer = 512;               // stdio buffer per open file instead of 4 KB (LittleFS block size)

    using Handler = std::function<void(AsyncWebServerRequest*)>;

    struct Waiting {
            AsyncWebServerRequestPtr request;
            Handler handler;
    };

    inline int active = 0;
    inline std::deque<Waiting> waiting;
    inline uint32_t queuedTotal = 0;   // requests that had to wait (measurement build prints it)
    inline uint32_t rejectedTotal = 0; // requests answered with 503 because the queue was full
    inline uint32_t brakedTotal = 0;   // requests refused by the emergency brake

    inline orione::GateAction decide() {
        return orione::gateDecide(active, waiting.size(), heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    }

    /** Answers a refused request: 503, or a reset when the heap is almost gone (allocates nothing) */
    inline void refuse(AsyncWebServerRequest* request, const orione::GateAction action) {
        // a "busy" with a healthy heap means the queue was full; anything else is the emergency brake
        const bool queueFull = action == orione::GateAction::Busy &&
                               orione::gateDecide(0, 0, heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)) == orione::GateAction::Start;
        ++(queueFull ? rejectedTotal : brakedTotal);

        if (action == orione::GateAction::Reset) {
            request->abort();
            return;
        }

        AsyncWebServerResponse* response = request->beginResponse(503, "text/plain", "busy, retry");
        response->addHeader("Retry-After", "2");
        request->send(response);
    }

    inline void finished();

    inline void start(AsyncWebServerRequest* request, const Handler& handler) {
        ++active;
        request->onDisconnect([] { finished(); }); // every response closes its connection
        handler(request);
    }

    /** A response is done: let the next waiting requests in */
    inline void finished() {
        if (active > 0) {
            --active;
        }

        while (!waiting.empty()) {
            const orione::GateAction action = decide();

            if (action == orione::GateAction::Wait) {
                return;
            }

            Waiting next = std::move(waiting.front());
            waiting.pop_front();

            if (const auto request = next.request.lock()) { // gone if the browser gave up meanwhile
                if (action == orione::GateAction::Start) {
                    start(request.get(), next.handler);
                }
                else {
                    refuse(request.get(), action);
                }
            }
        }
    }

    inline void handle(AsyncWebServerRequest* request, Handler handler) {
        switch (const orione::GateAction action = decide(); action) {
            case orione::GateAction::Start:
                start(request, handler);
                return;
            case orione::GateAction::Wait:
                ++queuedTotal;
                waiting.push_back({request->pause(), std::move(handler)});
                return;
            default:
                refuse(request, action);
                return;
        }
    }

    /** Wraps a route handler: server.on("/timeseries", HTTP_GET, web_gate::gated([](AsyncWebServerRequest* r) {...})) */
    inline ArRequestHandlerFunction gated(ArRequestHandlerFunction handler) {
        return [handler](AsyncWebServerRequest* request) { handle(request, handler); };
    }

    /** Static files through the gate; the library's handler does the rest (gzip, ETag, 304, cache) */
    class GatedHandler : public AsyncWebHandler {
        public:
            explicit GatedHandler(AsyncWebHandler* inner) :
                inner_(inner) {
            }

            bool canHandle(AsyncWebServerRequest* request) const override {
                return inner_->canHandle(request);
            }

            bool isRequestHandlerTrivial() const override {
                return inner_->isRequestHandlerTrivial();
            }

            void handleRequest(AsyncWebServerRequest* request) override {
                if (request->_tempFile) {
                    request->_tempFile.setBufferSize(kFileBuffer); // before the first read
                }

                handle(request, [this](AsyncWebServerRequest* r) { inner_->handleRequest(r); });
            }

        private:
            AsyncWebHandler* inner_;
    };

    /** Like server.serveStatic(), but through the gate */
    inline AsyncStaticWebHandler& serveStatic(AsyncWebServer& server, const char* uri, fs::FS& fs, const char* path, const char* cacheControl) {
        auto* inner = new AsyncStaticWebHandler(uri, fs, path, cacheControl);
        server.addHandler(new GatedHandler(inner));
        return *inner;
    }

} // namespace web_gate

#define WEB_GATED(...) web_gate::gated(__VA_ARGS__) // variadic: the handler lambdas contain commas

#else

#define WEB_GATED(...) __VA_ARGS__

#endif
