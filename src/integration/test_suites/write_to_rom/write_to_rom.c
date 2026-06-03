// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
#include "veer-csr.h"
#include "riscv_hw_if.h"
#include "printf.h"
#include "caliptra_isr.h"

volatile char* stdout = (char *)STDOUT;

#ifdef CPT_VERBOSITY
    enum printf_verbosity verbosity_g = CPT_VERBOSITY;
#else
    enum printf_verbosity verbosity_g = LOW;
#endif

volatile caliptra_intr_received_s cptra_intr_rcv = {0};
volatile uint32_t intr_count       = 0;

static inline uint_xlen_t csr_read_mdseac(void) {
    uint_xlen_t value;
    __asm__ volatile ("csrr    %0, 0xFC0"
                      : "=r" (value)  /* output : register */
                      : /* input : none */
                      : /* clobbers: none */);
    return value;
}

void main(void);

void nmi_handler (void) {
    if (csr_read_mcause() == 0xF0000000U &&                 // mcause for store bus error
        csr_read_mdseac() == (((uint32_t) main) & ~3)) {    // mdseac holds address of first failing store
        VPRINTF(LOW, "Received Imprecise Bus Error NMI\n");
        SEND_STDOUT_CTRL(0xff);
    }
    else {
        VPRINTF(ERROR, "Unexpected entry into NMI handler function!\n");
        SEND_STDOUT_CTRL(0x1);
    }
}

// This code is located in ICCM because doing it from ROM triggers assert_ahb_error_protocol.
__attribute__((section(".data_iccm0"))) __attribute__ ((noinline))
void write_to_rom(uint32_t addr) {
    *(uint32_t *)addr = 0;

    // Since error is imprecise, wait for a while before jumping back to ROM
    for (uint8_t i = 0; i < 25; i++) {
        __asm__ volatile ("nop"); // Sleep loop as "nop"
    }
}

// ICCM can only be accessed in aligned 32b words. Linker script ensures
// that source data in DCCM is sufficiently aligned.
extern uint32_t iccm_code0_start[];
extern uint32_t iccm_code0_end[];

void main(void) {
    VPRINTF(LOW, "-----------------------\nAttempt write to ROM !!\n-----------------------\n");

    init_interrupts();

    // Copy ICCM from LMA to VMA, crt0 doesn't do this
    volatile uint32_t *iccm = (uint32_t *)0x40000000;
    for (int i=0; i<(iccm_code0_end - iccm_code0_start); i++) {
        *iccm++ = iccm_code0_start[i];
    }

    // Setup the NMI Handler
    lsu_write_32((uintptr_t) (CLP_SOC_IFC_REG_INTERNAL_NMI_VECTOR), (uint32_t) (nmi_handler));

    // Try to overwrite this function in ROM, it should cause Imprecise Bus Error Non-Maskable Interrupt
    write_to_rom(((uint32_t) main) & ~3);

    VPRINTF(ERROR, "NMI didn't happen!\n");
    SEND_STDOUT_CTRL(0x1);
}
