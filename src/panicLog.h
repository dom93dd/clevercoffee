/**
 * @file panicLog.h
 *
 * @brief Orione build (CC_ORIONE): a crash leaves its trace in the log that survives the restart
 *        (src/orioneLog.h, GET /log): reason, task and the return addresses of the crashing code, so a
 *        crash in the machine can be found without the USB cable (the backtrace otherwise only goes to
 *        the serial port). The linker sends esp_panic_handler() here first (-Wl,--wrap in platformio.ini);
 *        orione_deploy.py --decode turns the addresses into functions and lines with the firmware's ELF.
 *        Runs in the panic handler: interrupts off, the other core halted; no locks, no heap, no printf.
 */

#pragma once

#include "orioneLog.h"
#include <esp_debug_helpers.h>
#include <esp_private/panic_internal.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <soc/cpu.h>
#include <xtensa/xtensa_context.h>

namespace orione_panic {

    inline void hex(char* out, uint32_t v) {
        static constexpr char kDigits[] = "0123456789abcdef";
        out[0] = '0';
        out[1] = 'x';

        for (int i = 9; i >= 2; --i, v >>= 4) {
            out[i] = kDigits[v & 0xF];
        }

        out[10] = '\0';
    }

    inline void note(const panic_info_t* info) {
        auto& r = orione_log::ring; // no lock: the panic may have hit inside it, and nothing else runs now
        char h[11];
        r.append("!!! PANIC ");
        r.append(info->reason ? info->reason : "?");

        if (info->description) {
            r.append(" | ");
            r.append(info->description);
        }

        r.append(" | core ");
        h[0] = static_cast<char>('0' + (info->core & 1));
        r.append(h, 1);
        const TaskHandle_t task = xTaskGetCurrentTaskHandleForCPU(info->core);

        if (task != nullptr) {
            r.append(" | task ");
            r.append(pcTaskGetName(task));
        }

        if (info->frame != nullptr) {
            const auto* xf = static_cast<const XtExcFrame*>(info->frame);
            esp_backtrace_frame_t f = {static_cast<uint32_t>(xf->pc), static_cast<uint32_t>(xf->a1), static_cast<uint32_t>(xf->a0), xf};
            r.append("\nBacktrace:");

            for (int i = 0; i < 12; ++i) {
                hex(h, esp_cpu_process_stack_pc(f.pc));
                r.append(" ");
                r.append(h);

                if (f.next_pc == 0 || !esp_backtrace_get_next_frame(&f)) {
                    break;
                }
            }
        }

        r.append("\n");
    }

} // namespace orione_panic

extern "C" void __real_esp_panic_handler(panic_info_t* info);

extern "C" void __wrap_esp_panic_handler(panic_info_t* info) {
    orione_panic::note(info);
    __real_esp_panic_handler(info);
}
