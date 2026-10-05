/*
 * Copyright (c) 2024, Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/interrupt_controller/riscv_clic.h>
#include <hal/nrf_vpr_clic.h>

/*
 * TEMPORARY diagnostic instrumentation for nested-IRQ crash investigation.
 * Ring buffer of the last IRQ_TRACE_LEN interrupt entries, recorded from
 * __soc_handle_irq (see intc_nrfx_clic.S) with minimal overhead (no I/O,
 * just a few stores), so it can be dumped from the fatal error handler
 * without perturbing interrupt timing the way a live debugger probe does.
 * Entry stride is 16 bytes (4 words, last word unused/padding) to keep the
 * assembly indexing (shift by 4) simple.
 */
#define IRQ_TRACE_LEN 64

struct irq_trace_entry {
	uint32_t cause;
	uint32_t nested;
	uint32_t mepc;
	uint32_t _pad;
};

struct irq_trace_entry irq_trace_buf[IRQ_TRACE_LEN];
uint32_t irq_trace_idx;

void riscv_clic_irq_enable(uint32_t irq)
{
	nrf_vpr_clic_int_enable_set(NRF_VPRCLIC, irq, true);
}

void riscv_clic_irq_disable(uint32_t irq)
{
	nrf_vpr_clic_int_enable_set(NRF_VPRCLIC, irq, false);
}

int riscv_clic_irq_is_enabled(uint32_t irq)
{
	return nrf_vpr_clic_int_enable_check(NRF_VPRCLIC, irq);
}

void riscv_clic_irq_priority_set(uint32_t irq, uint32_t pri, uint32_t flags)
{
	nrf_vpr_clic_int_priority_set(NRF_VPRCLIC, irq, NRF_VPR_CLIC_INT_TO_PRIO(pri));
}

void riscv_clic_irq_set_pending(uint32_t irq)
{
	nrf_vpr_clic_int_pending_set(NRF_VPRCLIC, irq);
}
