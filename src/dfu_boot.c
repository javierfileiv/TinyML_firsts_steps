#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/uart.h>
#include <cmsis_core.h>

#include "dfu_boot.h"

/* STM32F4 system memory base, holds the DFU-capable ROM bootloader
 * (RM0383). Its USB device enumerates as 0483:DF11, the identity the
 * dfu-util runner of "west flash" expects
 */
#ifndef DFU_BOOT_SYSMEM_BASE
#define DFU_BOOT_SYSMEM_BASE 0x1FFF0000UL
#endif

/* The trigger sequence is read from the chosen console port */
#if DT_NODE_EXISTS(DT_CHOSEN(zephyr_console))
static const struct device *const dfu_console =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
#define DFU_BOOT_HAS_CONSOLE 1
#else
#define DFU_BOOT_HAS_CONSOLE 0
#endif

/* Hand the Cortex-M over to the ROM bootloader with a warm handover
 * from the running application: soft-disconnect USB, give the clock
 * tree back to its reset state, stop SysTick, disarm every NVIC
 * source, then remap the vector table, swap the stack pointer and
 * jump to the bootloader reset entry. Never returns
 */
static void dfu_boot_jump(void)
{
	/* Soft-disconnect USB first, so the host registers the detach
	 * before the ROM tries to re-attach
	 */
	USB_OTG_DeviceTypeDef *dev =
		(USB_OTG_DeviceTypeDef *)((uint32_t)USB_OTG_FS +
					  USB_OTG_DEVICE_BASE);

	dev->DCTL |= USB_OTG_DCTL_SDIS;
	k_msleep(300);

	/* Clocks back to HSI: the OTG peripheral loses its clock and
	 * releases D+ with it (a second detach the host also sees),
	 * and the ROM assumes HSI as SYSCLK (RM0383 clock rules)
	 */
	RCC->CR |= RCC_CR_HSION;
	while ((RCC->CR & RCC_CR_HSIRDY) == 0U) {
	}
	RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_HSI;
	while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI) {
	}
	RCC->CR &= ~(RCC_CR_PLLON | RCC_CR_PLLI2SON | RCC_CR_HSEON);

	/* Stop SysTick and clear system-exception pending bits: a tick
	 * firing in the middle of the bootloader's USB bring-up would
	 * hijack the CPU away from its interrupt-driven DFU stack
	 */
	SysTick->CTRL = 0U;
	SysTick->LOAD = 0U;
	SysTick->VAL = 0U;
	SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;

	/* Disable the MPU and clear the reset-cause flags: machine
	 * state a physical entry never carries
	 */
	MPU->CTRL = 0U;
	RCC->CSR |= RCC_CSR_RMVF;

	/* Disarm every NVIC source and clear pending flags (nothing is
	 * enabled right after a system reset, this is belt and braces)
	 */
	for (uint8_t i = 0; i < sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0]);
	     ++i) {
		NVIC->ICER[i] = 0xFFFFFFFFU;
		NVIC->ICPR[i] = 0xFFFFFFFFU;
	}

	/* Do NOT set PRIMASK: it survives the jump, the ROM never
	 * unmasks it and its DFU USB stack is interrupt-driven (it
	 * attaches but never answers). Reset CONTROL (clears the FPU
	 * context bit) and synchronize before handing over
	 */
	__set_CONTROL(0U);
	SCB->VTOR = DFU_BOOT_SYSMEM_BASE;
	__DSB();
	__ISB();
	__set_MSP(*(uint32_t *)DFU_BOOT_SYSMEM_BASE);
	((void (*)(void))(*(uint32_t *)(DFU_BOOT_SYSMEM_BASE + 4U)))();

	for (;;) {
	}
}

void dfu_boot_enter(void)
{
	printk("dfu: jumping into ROM bootloader, goodbye\n");

	/* Let the farewell line drain over USB before the detach */
	k_msleep(50);

	dfu_boot_jump();
}

int dfu_boot_poll(void)
{
#if DFU_BOOT_HAS_CONSOLE
	static const size_t trigger_len = sizeof(DFU_BOOT_TRIGGER) - 1;
	static size_t match;
	unsigned char c;

	if (uart_poll_in(dfu_console, &c) != 0) {
		return -1;
	}

#if DFU_BOOT_ECHO
	/* Echo so the operator sees the console is alive while typing */
	if (c == '\r') {
		printk("\r\n");
	} else {
		printk("%c", c);
	}
#endif

	if (c == DFU_BOOT_TRIGGER[match]) {
		match++;
		if (match == trigger_len) {
			dfu_boot_enter();
		}
	} else {
		/* A mismatch may still start a new sequence */
		match = (c == DFU_BOOT_TRIGGER[0]) ? 1 : 0;
	}

	return (int)c;
#else
	return -1;
#endif
}
