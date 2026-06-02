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

#include "caliptra_defines.h"
#include "caliptra_isr.h"
#include "riscv-csr.h"
#include <string.h>
#include <stdint.h>
#include "printf.h"
#include "riscv_hw_if.h"
#include "wdt.h"

volatile uint32_t* stdout           = (uint32_t *)STDOUT;
volatile uint32_t  intr_count = 0;
volatile uint32_t  rst_count __attribute__((section(".dccm.persistent"))) = 0;
volatile rv_exception_struct_s exc_flag __attribute__((section(".dccm.persistent")));

#ifdef CPT_VERBOSITY
    enum printf_verbosity verbosity_g = CPT_VERBOSITY;
#else
    enum printf_verbosity verbosity_g = LOW;
#endif

volatile uint32_t * wdt_timer1_en       = (uint32_t *) CLP_SOC_IFC_REG_CPTRA_WDT_TIMER1_EN;
volatile uint32_t * wdt_timer1_ctrl     = (uint32_t *) CLP_SOC_IFC_REG_CPTRA_WDT_TIMER1_CTRL;
volatile uint32_t * wdt_timer1_period_0 = (uint32_t *) CLP_SOC_IFC_REG_CPTRA_WDT_TIMER1_TIMEOUT_PERIOD_0;
volatile uint32_t * wdt_timer1_period_1 = (uint32_t *) CLP_SOC_IFC_REG_CPTRA_WDT_TIMER1_TIMEOUT_PERIOD_1;
volatile uint32_t * soc_intr_en         = (uint32_t *) CLP_SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTR_EN_R;
volatile uint32_t * soc_global_intr_en  = (uint32_t *) CLP_SOC_IFC_REG_INTR_BLOCK_RF_GLOBAL_INTR_EN_R;

volatile uint32_t * wdt_timer2_en       = (uint32_t *) CLP_SOC_IFC_REG_CPTRA_WDT_TIMER2_EN;
volatile uint32_t * wdt_timer2_ctrl     = (uint32_t *) CLP_SOC_IFC_REG_CPTRA_WDT_TIMER2_CTRL;
volatile uint32_t * wdt_timer2_period_0 = (uint32_t *) CLP_SOC_IFC_REG_CPTRA_WDT_TIMER2_TIMEOUT_PERIOD_0;
volatile uint32_t * wdt_timer2_period_1 = (uint32_t *) CLP_SOC_IFC_REG_CPTRA_WDT_TIMER2_TIMEOUT_PERIOD_1;

volatile uint32_t * hw_error_fatal      = (uint32_t *) CLP_SOC_IFC_REG_CPTRA_HW_ERROR_FATAL;

volatile caliptra_intr_received_s cptra_intr_rcv = {0};

void nmi_handler       (void);

void nmi_handler (void) {
    VPRINTF(LOW, "**** Entering NMI Handler ****\n");
    lsu_write_32(CLP_SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_R,
                 SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_R_ERROR_WDT_TIMER2_TIMEOUT_STS_MASK);
    if (lsu_read_32(CLP_SOC_IFC_REG_CPTRA_HW_ERROR_FATAL) & SOC_IFC_REG_CPTRA_HW_ERROR_FATAL_NMI_PIN_MASK) {
        SEND_STDOUT_CTRL(0xf5);
    }
    else {
        VPRINTF(ERROR, "Unexpected entry into NMI handler function!\n");
        SEND_STDOUT_CTRL(0x1);
    }

}

void main() {
    VPRINTF(LOW, "---------------------------\n");
    VPRINTF(LOW, " WDT Smoke Test with reset !!\n");
    VPRINTF(LOW, "---------------------------\n");

    //Enable SOC error interrupt
    *soc_global_intr_en = SOC_IFC_REG_INTR_BLOCK_RF_GLOBAL_INTR_EN_R_ERROR_EN_MASK;
    *soc_intr_en = SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTR_EN_R_ERROR_WDT_TIMER1_TIMEOUT_EN_MASK | SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTR_EN_R_ERROR_WDT_TIMER2_TIMEOUT_EN_MASK;
    
    //Call interrupt init
    // init_interrupts();

    rst_count++;

    // Setup the NMI Handler
    lsu_write_32((uintptr_t) (CLP_SOC_IFC_REG_INTERNAL_NMI_VECTOR), (uint32_t) (nmi_handler));
    
    if(rst_count == 1) {
        VPRINTF(LOW, "Cascaded mode\n");
        configure_wdt_cascade(0x200, 0x00, 0xffffffff, 0xffffffff);
        
        VPRINTF(LOW, "Stall until timer1 times out\n");
        service_t1_intr();
        
        SEND_STDOUT_CTRL(0xf6);
    }
    else if (rst_count == 2) {
        //Release forced timer periods from tb so test can set them
        SEND_STDOUT_CTRL(0xf1);
        configure_wdt_cascade(0x200, 0x00, 0xffffffff, 0xffffffff);

        *wdt_timer1_ctrl = SOC_IFC_REG_CPTRA_WDT_TIMER1_CTRL_TIMER1_RESTART_MASK;

        service_t1_intr();
        SEND_STDOUT_CTRL(0xf5);
    }
    else if (rst_count == 3) {
        //Release forced timer periods from tb so test can set them
        SEND_STDOUT_CTRL(0xf1);
    
        VPRINTF(LOW, "Independent mode - both timers enabled - warm rst\n");
        configure_wdt_independent(BOTH_TIMERS_EN, 0x200, 0x00000000, 0x200, 0x00000000);
        
        VPRINTF(LOW, "Stall until timer1 times out\n");
        service_t1_intr();
        //reset t1
        *wdt_timer1_en = 0;
        
        service_t2_intr();
        //reset t2
        *wdt_timer2_en = 0;
        
        SEND_STDOUT_CTRL(0xf6);
    }
    else if (rst_count == 4) {
        //Release forced timer periods from tb so test can set them
        SEND_STDOUT_CTRL(0xf1);

        VPRINTF(LOW, "Independent mode - both timers enabled - cold rst\n");
        configure_wdt_independent(BOTH_TIMERS_EN, 0x200, 0x00000000, 0x200, 0x00000000);

        VPRINTF(LOW, "Stall until timer1 times out\n");
        service_t1_intr();
        *wdt_timer1_en = 0;

        VPRINTF(LOW, "Stall until timer2 times out\n");
        service_t2_intr();
        *wdt_timer2_en = 0;
        
        SEND_STDOUT_CTRL(0xf5);
    }
    else if (rst_count == 5) {
        //Release forced timer periods from tb so test can set them
        SEND_STDOUT_CTRL(0xf1);
        VPRINTF(LOW, "Cascaded mode with timer2 timeout - NMI - cold rst\n");
        configure_wdt_cascade(0x200, 0x00, 0x200, 0x00);
        
        VPRINTF(LOW, "Stall until timer1 times out\n");
        VPRINTF(LOW, "Stall until timer2 times out\n");

        while(!(lsu_read_32(CLP_SOC_IFC_REG_CPTRA_HW_ERROR_FATAL) & SOC_IFC_REG_CPTRA_HW_ERROR_FATAL_NMI_PIN_MASK));

        // NMI handler should have reset the platform by now
        VPRINTF(ERROR, "NMI handler returned!\n");
        SEND_STDOUT_CTRL(0x1);
    }
    else if (rst_count == 6) {
        //Release forced timer periods from tb so test can set them
        SEND_STDOUT_CTRL(0xf1);
        //Write 1 to clear HW fatal error register
        if ((*hw_error_fatal && SOC_IFC_REG_CPTRA_HW_ERROR_FATAL_NMI_PIN_MASK) == 1) {
            VPRINTF(ERROR, "Cold rst should have reset hw_fatal_error nmi_pin!\n");
            SEND_STDOUT_CTRL(0x1);
        }

        VPRINTF(LOW, "Independent mode - timer2 enabled, timer1 disabled - warm rst\n");
        *wdt_timer2_en = SOC_IFC_REG_CPTRA_WDT_TIMER2_EN_TIMER2_EN_MASK;
        set_t2_period(0x00000200, 0x00000000);
        
        VPRINTF(LOW, "Stall until timer2 times out\n");
        service_t2_intr();
        *wdt_timer2_en = 0;
        
        SEND_STDOUT_CTRL(0xf6);

    }
    else if (rst_count == 7) {
        //Release forced timer periods from tb so test can set them
        SEND_STDOUT_CTRL(0xf1);

        VPRINTF(LOW, "Independent mode - timer2 enabled, timer1 disabled - cold rst\n");
        configure_wdt_independent(T1_DIS_T2_EN, 0x200, 0x00000000, 0x200, 0x00000000);
        
        VPRINTF(LOW, "Stall until timer2 times out\n");
        service_t2_intr();
        *wdt_timer2_en = 0;
        
        SEND_STDOUT_CTRL(0xf5);
    }
    else if (rst_count == 8) {
        //Issue warm reset during WDT operation
        //WDT cascade mode
        configure_wdt_cascade(0x37, 0x00, 0xffffffff, 0xffffffff);
        SEND_STDOUT_CTRL(0xf6);
    }
    else if (rst_count == 9) {
        //Issue cold reset during WDT operation
        configure_wdt_cascade(0x37, 0x00, 0xffffffff, 0xffffffff);
        SEND_STDOUT_CTRL(0xf5);
    }
    else if (rst_count == 10) {
        //Issue warm reset during WDT operation
        //WDT cascade mode
        configure_wdt_independent(BOTH_TIMERS_EN, 0x200, 0x00000000, 0x34, 0x00000000);
        SEND_STDOUT_CTRL(0xf6);
    }
    else if (rst_count == 11) {
        //Issue cold reset during WDT operation
        //WDT cascade mode
        configure_wdt_independent(BOTH_TIMERS_EN, 0x200, 0x00000000, 0x34, 0x00000000);
        SEND_STDOUT_CTRL(0xf5);
    }
    else if (rst_count == 12) {
        //Issue warm reset during WDT operation
        //WDT cascade mode
        configure_wdt_independent(T1_DIS_T2_EN, 0x200, 0x00000000, 0x200, 0x00000000);
        SEND_STDOUT_CTRL(0xf6);
    }
    else if (rst_count == 13) {
        //Issue cold reset during WDT operation
        //WDT cascade mode
        configure_wdt_independent(T1_DIS_T2_EN, 0x200, 0x00000000, 0x200, 0x00000000);
        SEND_STDOUT_CTRL(0xf5);
    }
    else if (rst_count == 14) {
        VPRINTF(LOW, "Timer 2 forced service doesn't clear timer\n");
        *soc_intr_en = 0;
        init_interrupts();
        // Clear timer2
        lsu_write_32(CLP_SOC_IFC_REG_CPTRA_WDT_TIMER2_EN, SOC_IFC_REG_CPTRA_WDT_TIMER2_EN_TIMER2_EN_MASK);
        lsu_write_32(CLP_SOC_IFC_REG_CPTRA_WDT_TIMER1_EN, SOC_IFC_REG_CPTRA_WDT_TIMER1_EN_TIMER1_EN_MASK);
        lsu_write_32(CLP_SOC_IFC_REG_CPTRA_WDT_TIMER1_TIMEOUT_PERIOD_0, 0);
        lsu_write_32(CLP_SOC_IFC_REG_CPTRA_WDT_TIMER1_TIMEOUT_PERIOD_1, 0);
        lsu_write_32(CLP_SOC_IFC_REG_CPTRA_WDT_TIMER2_TIMEOUT_PERIOD_0, 0x1800);
        lsu_write_32(CLP_SOC_IFC_REG_CPTRA_WDT_TIMER2_TIMEOUT_PERIOD_1, 0);
        lsu_write_32(CLP_SOC_IFC_REG_CPTRA_WDT_TIMER1_CTRL, SOC_IFC_REG_CPTRA_WDT_TIMER1_CTRL_TIMER1_RESTART_MASK);
        uint32_t mitb0 = 0x00001000;
        uint32_t mie_timer0_en = 0x20000000;
        //Set internal timer0 counter value to 0
        __asm__ volatile ("csrwi    %0, %1" \
                      : /* output: none */        \
                      : "i" (0x7d2), "i" (0x00)  /* input : immediate  */ \
                      : /* clobbers: none */);
        //Set internal timer0 upper bound
        __asm__ volatile ("csrw    %0, %1" \
                      : /* output: none */        \
                      : "i" (0x7d3), "r" (mitb0)   /* input : immediate  */ \
                      : /* clobbers: none */);
        //Set machine intr enable reg (mie) - enable internal timer0 intr
        __asm__ volatile ("csrw    %0, %1" \
                      : /* output: none */        \
                      : "i" (0x304), "r" (mie_timer0_en)  /* input : immediate  */ \
                      : /* clobbers: none */);
        //Set mstatus reg - enable mie
        __asm__ volatile ("csrwi    %0, %1" \
                      : /* output: none */        \
                      : "i" (0x300), "i" (0x08)  /* input : immediate  */ \
                      : /* clobbers: none */);
        // Set timer2 interrupt
        lsu_write_32(CLP_SOC_IFC_REG_CPTRA_WDT_TIMER2_CTRL, SOC_IFC_REG_CPTRA_WDT_TIMER2_CTRL_TIMER2_RESTART_MASK);
        VPRINTF(LOW, "t2s\n");
        //Set internal timer0 counter value to 0
        __asm__ volatile ("csrwi    %0, %1" \
                      : /* output: none */        \
                      : "i" (0x7d2), "i" (0x00)  /* input : immediate  */ \
                      : /* clobbers: none */);
        //Set internal timer0 control (halt_en = 1, enable = 1)
        __asm__ volatile ("csrwi    %0, %1" \
                      : /* output: none */        \
                      : "i" (0x7d4), "i" (0x03)  /* input : immediate  */ \
                      : /* clobbers: none */);
        //Halt the core
        __asm__ volatile ("csrwi    %0, %1;" \
                          "fence.i"
                      : /* output: none */        \
                      : "i" (0x7c6), "i" (0x03)  /* input : immediate  */ \
                      : /* clobbers: none */);
        // Servicing existing intr shouldn't clear counter
        lsu_write_32(CLP_SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_R,
                     SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_R_ERROR_WDT_TIMER2_TIMEOUT_STS_MASK);
        // Servicing timer1 intr shouldn't clear timer2 counter when in independent mode
        lsu_write_32(CLP_SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_R,
                     SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_R_ERROR_WDT_TIMER1_TIMEOUT_STS_MASK);
        if (
            (lsu_read_32(CLP_SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_R) &
             SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_R_ERROR_WDT_TIMER2_TIMEOUT_STS_MASK) == 1
        ) {
            SEND_STDOUT_CTRL(0x1);
            while(1);
        }
        VPRINTF(LOW, "t2r\n");
        //Set internal timer0 upper bound
        __asm__ volatile ("csrw    %0, %1" \
                      : /* output: none */        \
                      : "i" (0x7d3), "r" (mitb0)   /* input : immediate  */ \
                      : /* clobbers: none */);
        //Set internal timer0 control (halt_en = 1, enable = 1)
        __asm__ volatile ("csrwi    %0, %1" \
                      : /* output: none */        \
                      : "i" (0x7d4), "i" (0x03)  /* input : immediate  */ \
                      : /* clobbers: none */);
        //Halt the core
        __asm__ volatile ("csrwi    %0, %1;" \
                          "fence.i"
                      : /* output: none */        \
                      : "i" (0x7c6), "i" (0x03)  /* input : immediate  */ \
                      : /* clobbers: none */);
        if (
            (lsu_read_32(CLP_SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_R) &
             SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_R_ERROR_WDT_TIMER2_TIMEOUT_STS_MASK) == 0
        ) {
            SEND_STDOUT_CTRL(0x1);
            while(1);
        }
        SEND_STDOUT_CTRL(0xf5);
        while(1);
    }
    else {
        VPRINTF(LOW, "End of test\n");
    }
}
