/**
 * @file heapDiag.h
 *
 * @brief Orione build (CC_ORIONE): where the heap goes. The free heap fell to 1.3-2.7 KB several times (08.10.2026,
 *        GET /boot "heapMin") while the WiFi in the assembled machine was poor. Suspected: data the TCP stack still
 *        holds for connections nobody reads any more (a phone locked or out of reach; a closed connection keeps its
 *        unacknowledged data until the peer answers or lwIP gives up after 12 retransmissions, minutes later).
 *        This counts the TCP connections and the bytes queued in them, records failed allocations, and writes a log
 *        line with this context whenever the heap reaches a new low.
 */

#pragma once

#include <esp_heap_caps.h>
#include <lwip/priv/tcp_priv.h>
#include <lwip/priv/tcpip_priv.h>

namespace heap_diag {

    struct TcpUse {
            uint16_t open = 0;    // established
            uint16_t closing = 0; // closed by one side, still in the stack (incl. TIME-WAIT)
            uint32_t held = 0;    // bytes in their send queues (unsent and unacknowledged), with headers
    };

    struct TcpCall {
            ::tcpip_api_call_data call;
            TcpUse use;
    };

    /** Runs in the TCP/IP task: its lists must not be walked from anywhere else */
    inline err_t countTcp(::tcpip_api_call_data* c) {
        TcpUse& u = reinterpret_cast<TcpCall*>(c)->use;

        for (const tcp_pcb* p = tcp_active_pcbs; p != nullptr; p = p->next) {
            ++(p->state == ESTABLISHED ? u.open : u.closing);

            for (const tcp_seg* s = p->unsent; s != nullptr; s = s->next) {
                u.held += s->p != nullptr ? s->p->tot_len : s->len;
            }

            for (const tcp_seg* s = p->unacked; s != nullptr; s = s->next) {
                u.held += s->p != nullptr ? s->p->tot_len : s->len;
            }
        }

        for (const tcp_pcb* p = tcp_tw_pcbs; p != nullptr; p = p->next) {
            ++u.closing;
        }

        return ERR_OK;
    }

    inline TcpUse tcpUse() {
        TcpCall call{};
        tcpip_api_call(countTcp, &call.call);
        return call.use;
    }

    // Allocations that failed (any task); the hook runs where it failed, so it only counts
    inline volatile uint32_t allocFails = 0;
    inline volatile uint32_t allocFailMax = 0;
    inline const char* volatile allocFailTask = "";

    inline void onAllocFailed(const size_t size, uint32_t, const char*) {
        allocFails = allocFails + 1;

        if (size > allocFailMax) {
            allocFailMax = size;
            allocFailTask = pcTaskGetName(nullptr);
        }
    }

    /** Call once, early in setup() */
    inline void begin() {
        heap_caps_register_failed_alloc_callback(onAllocFailed);
    }

    /**
     * Call from loop(): a log line when the heap reached a new low under 16 KB (in steps of 1 KB), and when
     * allocations failed, with what was going on
     * @param sse live-value clients, @param scale 0 off, 1 not connected, 2 connected
     */
    inline void loop(const int sse, const int scale) {
        static uint32_t lastCheck = 0;
        static size_t loggedLow = 16 * 1024;
        static uint32_t loggedFails = 0;

        if (millis() - lastCheck < 250) {
            return;
        }

        lastCheck = millis();
        const size_t low = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
        const uint32_t fails = allocFails;

        if (low + 1024 > loggedLow && fails == loggedFails) {
            return;
        }

        const TcpUse t = tcpUse();
        const size_t heapFree = heap_caps_get_free_size(MALLOC_CAP_8BIT), block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

        if (low + 1024 <= loggedLow) {
            loggedLow = low;
            LOGF(WARNING, "Heap low %u B (now %u, block %u); TCP %u open, %u closing, %u B queued; SSE %d; scale %d; RSSI %d", static_cast<unsigned>(low),
                 static_cast<unsigned>(heapFree), static_cast<unsigned>(block), t.open, t.closing, static_cast<unsigned>(t.held), sse, scale, static_cast<int>(WiFi.RSSI()));
        }

        if (fails != loggedFails) {
            LOGF(ERROR, "Allocation failed %u times so far (largest %u B, in %s); heap %u, block %u; TCP %u open, %u closing, %u B queued", static_cast<unsigned>(fails),
                 static_cast<unsigned>(allocFailMax), allocFailTask, static_cast<unsigned>(heapFree), static_cast<unsigned>(block), t.open, t.closing, static_cast<unsigned>(t.held));
            loggedFails = fails;
        }
    }

} // namespace heap_diag
