/*
 * Copyright (C) 2024 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SOC_RISCV_NORDIC_NRF_COMMON_VPR_SOC_ISR_STACKING_H_
#define SOC_RISCV_NORDIC_NRF_COMMON_VPR_SOC_ISR_STACKING_H_

#include <zephyr/arch/riscv/irq.h>

#if !defined(_ASMLANGUAGE)

#include <zephyr/devicetree.h>

#define VPR_CPU DT_INST(0, nordic_vpr)

#ifdef CONFIG_EXCEPTION_DEBUG
/*
 * Explicit padding is needed for VPRs, because they use hardware stacking on part of arch_esf and
 * ESF_SW_IRQ_SIZEOF needs to be calculated accordingly.
 */
#define ESF_CSF                                                                                    \
	_callee_saved_t *csf;                                                                      \
	unsigned long padding1;                                                                    \
	unsigned long padding2;                                                                    \
	unsigned long padding3;
#else
#define ESF_CSF
#endif /* CONFIG_EXCEPTION_DEBUG */

#if DT_PROP(VPR_CPU, nordic_bus_width) == 64

#define SOC_ISR_STACKING_ESF_DECLARE                                                               \
	struct arch_esf {                                                                          \
		unsigned long s0;                                                                  \
		unsigned long mstatus;                                                             \
		struct soc_esf soc_context;                                                        \
		ESF_CSF;                                                                           \
                                                                                                   \
		unsigned long t2;                                                                  \
		unsigned long ra;                                                                  \
		unsigned long t0;                                                                  \
		unsigned long t1;                                                                  \
		unsigned long a4;                                                                  \
		unsigned long a5;                                                                  \
		unsigned long a2;                                                                  \
		unsigned long a3;                                                                  \
		unsigned long a0;                                                                  \
		unsigned long a1;                                                                  \
		unsigned long mepc;                                                                \
		unsigned long _mcause;                                                             \
	} __aligned(16);

#else /* DT_PROP(VPR_CPU, nordic_bus_width) == 32 */

#define SOC_ISR_STACKING_ESF_DECLARE                                                               \
	struct arch_esf {                                                                          \
		unsigned long s0;                                                                  \
		unsigned long mstatus;                                                             \
		struct soc_esf soc_context;                                                        \
		ESF_CSF;                                                                           \
                                                                                                   \
		unsigned long ra;                                                                  \
		unsigned long t2;                                                                  \
		unsigned long t1;                                                                  \
		unsigned long t0;                                                                  \
		unsigned long a5;                                                                  \
		unsigned long a4;                                                                  \
		unsigned long a3;                                                                  \
		unsigned long a2;                                                                  \
		unsigned long a1;                                                                  \
		unsigned long a0;                                                                  \
		unsigned long _mcause;                                                             \
		unsigned long mepc;                                                                \
	} __aligned(16);

#endif /* DT_PROP(VPR_CPU, nordic_bus_width) == 64 */

/*
 * VPR stacked mcause needs to have proper value on initial stack.
 * Initial mret will restore this value.
 */
#define SOC_ISR_STACKING_ESR_INIT                                                                  \
	stack_init->_mcause = 0;

#else /* _ASMLANGUAGE */

/*
 * Size of the HW managed part of the ESF:
 *    sizeof(_mcause) + sizeof(_mepc)
 */
#define ESF_HW_SIZEOF	  (0x8)

/*
 * Size of the SW managed part of the ESF in case of exception
 */
#define ESF_SW_EXC_SIZEOF (__struct_arch_esf_SIZEOF - ESF_HW_SIZEOF)

/*
 * Size of the SW managed part of the ESF in case of interrupt
 * sizeof(s0) + sizeof(mstatus) + sizeof(soc_context) +...+ sizeof(ESF_CSF)
 */
#ifdef CONFIG_EXCEPTION_DEBUG
#define ESF_SW_IRQ_SIZEOF      (0x20)
#else
#define ESF_SW_IRQ_SIZEOF      (0x10)
#endif
/*
 * VPR needs aligned(8) SP when doing HW stacking, if this condition is not fulfilled it will move
 * SP by additional 4 bytes when HW stacking is done. This will be indicated by LSB bit in stacked
 * MEPC. This bit needs to be saved and then restored because zephyr is managing MEPC and doesn't
 * know anything about this additional offset.
 */
#define MEPC_SP_ALIGN_BIT_MASK (0x1UL)

#define STORE_SP_ALIGN_BIT_FROM_MEPC				\
	addi t1, sp, __struct_arch_esf_soc_context_OFFSET;	\
	lr t0, __struct_arch_esf_mepc_OFFSET(sp);		\
	andi t0, t0, MEPC_SP_ALIGN_BIT_MASK;			\
	sr t0, __soc_esf_t_sp_align_OFFSET(t1)

#define RESTORE_SP_ALIGN_BIT_TO_MEPC				\
	addi t1, sp, __struct_arch_esf_soc_context_OFFSET;	\
	lr t0, __soc_esf_t_sp_align_OFFSET(t1);			\
	lr t1, __struct_arch_esf_mepc_OFFSET(sp);		\
	or t2, t1, t0;						\
	sr t2, __struct_arch_esf_mepc_OFFSET(sp)

/*
 * NOTE: this used to stash t0 in the mscratch CSR while mcause was read to
 * determine interrupt-vs-exception (csrw mscratch, t0 / csrr t0, mcause /
 * ... / csrrw t0, mscratch, zero). That is unsafe on this VPR/CLIC core:
 * CLIC interrupts can preempt at almost any instruction boundary
 * (independent of mstatus.MIE, gated only by MINTSTATUS.MIL vs
 * MINTTHRESH), so a higher-priority interrupt can land in the 1-3
 * instruction window where mscratch temporarily holds a "borrowed" raw
 * t0 value instead of its normal role (VPR's datasheet documents
 * mscratch as dedicated to holding a hart-local context pointer, which
 * the hardware trap trampoline itself relies on). That collision
 * corrupts state (observed as e.g. _current_cpu / s0 reading back as
 * 0xFFFFFFFF, cascading into every register derived from it) and was
 * the root cause of sporadic crashes under back-to-back interrupts of
 * different priority.
 *
 * Fix: never touch a shared CSR for this. Unconditionally reserve the
 * worst-case (exception-sized) stack space *first* via a plain sp
 * adjustment, then stash t0 in real memory just above the new sp. Any
 * nested trap that preempts us here will compute its own entry sp
 * starting from our *already-decremented* sp and stack strictly below
 * it, so it can never touch the word we just wrote - no race is
 * possible. If it turns out to be an interrupt (which needs less
 * space), we simply give back the unused difference; the stashed word
 * is never read back in that case since VPR hardware auto-stacks the
 * caller-saved GPRs for interrupts already.
 */
#define SOC_ISR_SW_STACKING			\
	addi sp, sp, -ESF_SW_EXC_SIZEOF;	\
	sw t0, 0(sp);				\
						\
	csrr t0, mcause;			\
	srli t0, t0, RISCV_MCAUSE_IRQ_POS;	\
	bnez t0, stacking_is_interrupt;		\
						\
	lw t0, 0(sp);				\
	DO_CALLER_SAVED(sr);			\
	j stacking_keep_going;			\
						\
stacking_is_interrupt:				\
	addi sp, sp, (ESF_SW_EXC_SIZEOF - ESF_SW_IRQ_SIZEOF); \
						\
stacking_keep_going:				\
	STORE_SP_ALIGN_BIT_FROM_MEPC

/*
 * VPR/CLIC interrupt entry does NOT clear MSTATUS.MIE the way exception
 * entry does (confirmed via Nordic's VPR datasheet: the Exceptions page
 * explicitly states "MIE in MSTATUS is set to 0 upon exception entry to
 * prevent processing interrupts", with no equivalent statement for
 * interrupts - deliberately, since CLIC's whole point is to allow a
 * higher-priority interrupt to preempt a lower-priority one that's
 * still executing). This means preemption during interrupt handling is
 * gated *only* by MINTTHRESH/priority-level comparison, never by MIE.
 *
 * Zephyr's generic isr.S was written assuming (as is true on classic
 * RISC-V + PLIC cores) that nothing can preempt it from the moment a
 * trap is taken until its own MRET executes. That's false here. In
 * particular, the tail end of interrupt unstacking (this macro) runs
 * with interrupts still fully preemptable: a new, unrelated interrupt
 * can land literally anywhere in here - including after
 * _current_cpu->nested has already been decremented back to 0 in
 * irq_done (see the SOC_ISR_MASK_CRITICAL_SECTION uses in isr.S) - and
 * misinterpret this in-flight, not-yet-returned-to context as a fresh,
 * non-nested interrupt, corrupting it. This was observed empirically
 * via ring-buffer tracing: a new interrupt fired with mepc pointing at
 * the "j unstacking_keep_going" instruction just before this macro's own
 * MRET, reading nested==0 and taking over the still in-flight sp.
 *
 * Fix: mask all further CLIC preemption (MINTTHRESH = max) for the
 * whole irq_done -> MRET tail (raised in isr.S's irq_done, and
 * re-raised here since __soc_restore_context() already restored the
 * real/original value earlier in no_reschedule), and only write back
 * the real value in the very last instant before MRET, once we're
 * certain nothing else can preempt us before that MRET actually
 * executes.
 */
#define SOC_ISR_MASK_CRITICAL_SECTION		\
	li t0, 0xFF;				\
	csrw 0x347, t0

#define SOC_ISR_SW_UNSTACKING			\
	RESTORE_SP_ALIGN_BIT_TO_MEPC;		\
	csrr t0, mcause;			\
	srli t0, t0, RISCV_MCAUSE_IRQ_POS;	\
	bnez t0, unstacking_is_interrupt;	\
						\
	lw t1, __struct_arch_esf_soc_context_OFFSET(sp); \
	csrw 0x347, t1;				\
	DO_CALLER_SAVED(lr);			\
	addi sp, sp, ESF_SW_EXC_SIZEOF;		\
	j unstacking_keep_going;		\
						\
unstacking_is_interrupt:			\
	lw t1, __struct_arch_esf_soc_context_OFFSET(sp); \
	csrw 0x347, t1;				\
	addi sp, sp, ESF_SW_IRQ_SIZEOF;		\
						\
unstacking_keep_going:

#endif /* _ASMLANGUAGE */

#endif /* SOC_RISCV_NORDIC_NRF_COMMON_VPR_SOC_ISR_STACKING_H_ */
