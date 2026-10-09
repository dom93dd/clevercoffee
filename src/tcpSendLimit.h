/**
 * @file tcpSendLimit.h
 *
 * @brief Orione build (CC_ORIONE): at most two segments of an answer in the TCP stack at a time. lwIP lets each
 *        connection queue TCP_SND_BUF (5760 bytes, four segments) of copies in the heap until they are acknowledged.
 *        With the free heap at ~20 KB that left the WiFi driver without buffers whenever the page went out over a
 *        weak link: 6030 B queued, heap down to 1.5-2.7 KB, the driver failed to allocate (up to 2308 B) and dropped
 *        packets, so the data stayed queued longer; the machine was hardly reachable until switched off and on
 *        (GET /log, 09.10.2026). A smaller send buffer for each connection halves that (Dominik: "ja, mach beides").
 *        lwIP adds back to snd_buf only what was acknowledged, so a lower start value holds for the connection.
 */

#pragma once

#include <AsyncTCP.h>
#include <lwip/priv/tcp_priv.h>
#include <lwip/priv/tcpip_priv.h>

namespace tcp_send_limit {

    constexpr tcpwnd_size_t kBytes = 2 * CONFIG_LWIP_TCP_MSS; // 2872: the web server's send buffer (ASYNC_RESPONCE_BUFF_SIZE)

    struct Call {
            ::tcpip_api_call_data call;
            tcp_pcb* pcb;
    };

    /** Runs in the TCP/IP task: the connection may have gone meanwhile, so only one still in its list */
    inline err_t apply(::tcpip_api_call_data* c) {
        tcp_pcb* const pcb = reinterpret_cast<Call*>(c)->pcb;

        for (tcp_pcb* p = tcp_active_pcbs; p != nullptr; p = p->next) {
            if (p == pcb) {
                if (p->snd_buf > kBytes && p->unsent == nullptr && p->unacked == nullptr) {
                    p->snd_buf = kBytes; // nothing sent yet: the accounting starts from here
                }

                break;
            }
        }

        return ERR_OK;
    }

    /** Call before the first byte of the answer (the web server's middleware) */
    inline void limit(AsyncClient* client) {
        if (client == nullptr || client->pcb() == nullptr) {
            return;
        }

        Call call{};
        call.pcb = client->pcb();
        tcpip_api_call(apply, &call.call);
    }

} // namespace tcp_send_limit
