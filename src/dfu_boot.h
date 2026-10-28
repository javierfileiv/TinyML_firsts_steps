/*
 * Reusable "jump to ROM DFU bootloader" helper for STM32F4 targets
 *
 * Add dfu_boot.c to the build and call dfu_boot_poll() from a
 * periodic loop. When the trigger sequence is typed on the chosen
 * console port (tio works well for that), the module hands the
 * Cortex-M over to the ROM bootloader with a warm handover: USB is
 * soft-disconnected, the clock tree is restored to its reset state,
 * SysTick is stopped and every NVIC source is disarmed, then the
 * vector table, stack pointer and control register are swapped over
 * to the system memory. The ROM re-enumerates as VID 0483 / PID
 * DF11 and the board can be reflashed with "west flash" (dfu-util
 * runner) without any button press
 *
 * Integration:
 *   #include "dfu_boot.h"
 *   while (1) {
 *       (void)dfu_boot_poll();
 *       ...
 *   }
 */
#ifndef DFU_BOOT_H
#define DFU_BOOT_H

/* Console sequence that triggers the jump */
#ifndef DFU_BOOT_TRIGGER
#define DFU_BOOT_TRIGGER "dfu"
#endif

/* Echo each received character back on the console (debug aid) */
#ifndef DFU_BOOT_ECHO
#define DFU_BOOT_ECHO 1
#endif

/*
 * Poll the chosen console for the trigger sequence, non-blocking,
 * call from a periodic loop. Enters the ROM bootloader (never
 * returns) as soon as the sequence completes. Returns the consumed
 * character so the application can implement its own console
 * commands, or -1 when no character was pending
 */
int dfu_boot_poll(void);

/* Hand the chip over to the ROM system bootloader, never returns */
void dfu_boot_enter(void);

#endif /* DFU_BOOT_H */
