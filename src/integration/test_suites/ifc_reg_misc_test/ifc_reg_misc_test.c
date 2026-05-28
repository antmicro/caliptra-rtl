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

#include "soc_access.h"
#include "xorshift.h"
#include "caliptra_defines.h"
#include "caliptra_isr.h"
#include "printf.h"
#include "riscv-csr.h"
#include "riscv_hw_if.h"
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

volatile char* stdout = (char *)STDOUT;
volatile uint32_t  intr_count = 0;
volatile int rst_count __attribute__((section(".dccm.persistent"))) = 0;
volatile int error_count __attribute__((section(".dccm.persistent"))) = 0;
volatile rv_exception_struct_s exc_flag __attribute__((section(".dccm.persistent")));

#ifdef CPT_VERBOSITY
    enum printf_verbosity verbosity_g = CPT_VERBOSITY;
#else
    enum printf_verbosity verbosity_g = LOW;
#endif

volatile caliptra_intr_received_s cptra_intr_rcv = {0};

uint32_t trng_data_addr_regs [12] = {
    CLP_SOC_IFC_REG_CPTRA_TRNG_DATA_0,
    CLP_SOC_IFC_REG_CPTRA_TRNG_DATA_1,
    CLP_SOC_IFC_REG_CPTRA_TRNG_DATA_2,
    CLP_SOC_IFC_REG_CPTRA_TRNG_DATA_3,
    CLP_SOC_IFC_REG_CPTRA_TRNG_DATA_4,
    CLP_SOC_IFC_REG_CPTRA_TRNG_DATA_5,
    CLP_SOC_IFC_REG_CPTRA_TRNG_DATA_6,
    CLP_SOC_IFC_REG_CPTRA_TRNG_DATA_7,
    CLP_SOC_IFC_REG_CPTRA_TRNG_DATA_8,
    CLP_SOC_IFC_REG_CPTRA_TRNG_DATA_9,
    CLP_SOC_IFC_REG_CPTRA_TRNG_DATA_10,
    CLP_SOC_IFC_REG_CPTRA_TRNG_DATA_11
};

#define TB_CMD_COLD_RESET 0xF5
#define TB_CMD_TEST_PASS 0xFF
#define TB_CMD_TEST_FAIL 0x01

void test_masking(uint32_t addr, uint32_t value, uint32_t byte_mask, char* name) {
    uint32_t reg = lsu_read_32(addr);
    // Write value but mask all bytes
    soc_masked_write_32(addr, value, ~byte_mask);
    if ((lsu_read_32(addr) & value) == value) {
        VPRINTF(LOW, "%s was incorrectly updated, expected: %x, got: %x !\n", name, reg, lsu_read_32(addr));
        error_count += 1;
    }
    // Write value
    soc_masked_write_32(addr, value, byte_mask);
    if ((lsu_read_32(addr) & value) != value) {
        VPRINTF(LOW, "Write to %s was incorrectly masked, expected: %x, got: %x!\n", name, value, lsu_read_32(addr) & value);
        error_count += 1;
    }
    // Try clearing
    soc_masked_write_32(addr, 0, ~byte_mask);
    if ((lsu_read_32(addr) & value) != value) {
        VPRINTF(LOW, "%s was incorrectly cleared, expected: %x, got: %x!\n", name, value, lsu_read_32(addr) & value);
        error_count += 1;
    }
    // Clear
    soc_masked_write_32(addr, 0, byte_mask);
    if ((lsu_read_32(addr) & value) == value) {
        VPRINTF(LOW, "%s was not cleared!\n", name);
        error_count += 1;
    }
}

void test_lock_masking(uint32_t lock_addr, char* name) {
    // Write 1 but mask all bytes
    soc_masked_write_32(lock_addr, 1, 0);
    if (lsu_read_32(lock_addr) == 1) {
        VPRINTF(LOW, "%s was incorrectly locked!\n", name);
        error_count += 1;
    }
    // Write 1
    soc_masked_write_32(lock_addr, 1, 1);
    if (lsu_read_32(lock_addr) != 1) {
        VPRINTF(LOW, "Write to %s was incorrectly masked!\n", name);
        error_count += 1;
    }
    // Try clearing
    soc_masked_write_32(lock_addr, 0, 0);
    if (lsu_read_32(lock_addr) != 1) {
        VPRINTF(LOW, "%s was incorrectly cleared!\n", name);
        error_count += 1;
    }
    // Clear
    soc_masked_write_32(lock_addr, 0, 1);
    if (lsu_read_32(lock_addr) != 1) {
        VPRINTF(LOW, "0 write cleared %s!\n", name);
        error_count += 1;
    }
}

void test_w1c_masking(uint32_t status_addr, uint32_t value, uint32_t byte_mask, char* name) {
    // Write 0 but mask all bytes
    soc_masked_write_32(status_addr, ~value, 0xf);
    if ((lsu_read_32(status_addr) & value) != value) {
        VPRINTF(LOW, "%s was cleared by writing 0!\n", name);
        error_count += 1;
    }
    // Write 1 but mask all bytes
    soc_masked_write_32(status_addr, value, ~byte_mask);
    if ((lsu_read_32(status_addr) & value) != value) {
        VPRINTF(LOW, "%s was incorrectly cleared!\n", name);
        error_count += 1;
    }
    // Write 1 to clear
    soc_masked_write_32(status_addr, value, byte_mask);
    if ((lsu_read_32(status_addr) & value) == value) {
        VPRINTF(LOW, "Write to %s was incorrectly masked!\n", name);
        error_count += 1;
    }
}

extern uintptr_t iccm_code0_start, iccm_code0_end;
void execute_from_iccm (void) __attribute__ ((aligned(4),section (".data_iccm0")));
void execute_from_iccm (void) {
    // Do a bunch of random tasks just to populate the code space.
    uint32_t tmp_reg, tmp_reg2;
    VPRINTF(LOW, "Exec from ICCM\n");
    tmp_reg = lsu_read_32(CLP_SOC_IFC_REG_CPTRA_HW_CONFIG);
    VPRINTF(LOW, "HW_CFG: 0x%x\n", tmp_reg);
    tmp_reg  = lsu_read_32(CLP_SOC_IFC_REG_INTERNAL_RV_MTIMECMP_L);
    tmp_reg2 = lsu_read_32(CLP_SOC_IFC_REG_INTERNAL_RV_MTIMECMP_H);
    VPRINTF(LOW, "MTIMECMP: 0x%x %x\n", tmp_reg2, tmp_reg);
    tmp_reg  = lsu_read_32(CLP_SOC_IFC_REG_INTERNAL_RV_MTIME_L);
    tmp_reg2 = lsu_read_32(CLP_SOC_IFC_REG_INTERNAL_RV_MTIME_H);
    VPRINTF(LOW, "MTIME: 0x%x %x\n", tmp_reg2, tmp_reg);
    if (tmp_reg2) {
        lsu_write_32(CLP_SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_COUNT_INCR_R, tmp_reg2 ^ tmp_reg);
    } else {
        lsu_write_32(CLP_SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_COUNT_INCR_R, (tmp_reg2 - 1) ^ tmp_reg);
    }
    tmp_reg = csr_read_mepc();
    tmp_reg2 = csr_read_mtval();
    tmp_reg ^= csr_read_mtvec();
    tmp_reg2 ^= csr_read_mie();
    tmp_reg ^= csr_read_mip();
    tmp_reg2 ^= csr_read_mvendorid();
    tmp_reg ^= tmp_reg2;
    VPRINTF(LOW, "Result: 0x%x\n", tmp_reg);
    while(1) {
        __asm__ volatile ("wfi");
        for (uint16_t slp = 0; slp < 1000; slp++) {
            __asm__ volatile ("nop");
        }
        putchar('.');
    }
}

void trigger_mbox_prot_err(void) {
    lsu_write_32(CLP_MBOX_CSR_MBOX_UNLOCK, MBOX_CSR_MBOX_UNLOCK_UNLOCK_MASK);
    lsu_write_32(STDOUT, 0xe5);
    lsu_write_32(CLP_MBOX_CSR_MBOX_UNLOCK, MBOX_CSR_MBOX_UNLOCK_UNLOCK_MASK);
}

void trigger_mbox_ooo_err(void) {
    lsu_write_32(STDOUT, 0xe6);
    // Wait some time after injecting error
    for (uint16_t slp = 0; slp < 100; slp++) {
        __asm__ volatile ("nop");
    }
    lsu_write_32(CLP_MBOX_CSR_MBOX_UNLOCK, MBOX_CSR_MBOX_UNLOCK_UNLOCK_MASK);
}

void trigger_mbox_unc_err(void) {
    uint32_t sum;
    while((lsu_read_32(CLP_MBOX_CSR_MBOX_LOCK) & MBOX_CSR_MBOX_LOCK_LOCK_MASK) != 0);
    lsu_write_32(STDOUT, 0xFE);
    // Allocate an array in Mailbox SRAM
    volatile uint32_t* myarray = (uint32_t*) CLP_MBOX_SRAM_BASE_ADDR;
    for (uint32_t ii; ii < 64; ii++) {
        myarray[ii] = 64-ii;
    }
    sum = 0;
    // Read the array from the Mailbox and write to STDOUT
    for (uint32_t ii; ii < 64; ii++) {
        sum += myarray[ii];
    }
    VPRINTF(FATAL, "Sum:%x\n", sum); // no verbosity control -- dereferencing the array IS the test
    lsu_write_32(CLP_MBOX_CSR_MBOX_UNLOCK, MBOX_CSR_MBOX_UNLOCK_UNLOCK_MASK);
    lsu_write_32(STDOUT, 0xE4);
}

void trigger_iccm_unc_err(void) {
    uint32_t *ICCM = (uint32_t *) RV_ICCM_SADR;
    uint32_t *code_word = 0;
    uint32_t *iccm_dest = ICCM;
    uint32_t *actual_iccm_code_end = 0;

    uint32_t tmp_reg;
    lsu_write_32(STDOUT, 0xE1);
    code_word = (uint32_t *) &iccm_code0_start;
    iccm_dest = ICCM;
    VPRINTF(LOW, "Copy code from %x [thru %x] to %x\n", (uintptr_t) code_word, &iccm_code0_end, (uintptr_t) iccm_dest);
    while (code_word < (uint32_t *) &iccm_code0_end) {
        VPRINTF(ALL, "at %x: %x\n", (uintptr_t) code_word, *code_word);
        *iccm_dest++ = *code_word++;
    }
    actual_iccm_code_end = iccm_dest;
    lsu_write_32(STDOUT, 0xE4);
    code_word = (uint32_t *) ICCM;
    VPRINTF(LOW, "Read code from %x [through %x]\n", (uintptr_t) code_word, (uintptr_t) actual_iccm_code_end);
    while (code_word < actual_iccm_code_end) {
        tmp_reg = *code_word++;
        VPRINTF(LOW, "Data in ICCM: 0x%x\n", tmp_reg);
    }
}

void trigger_dccm_unc_err(void) {
    uint32_t array_in_dccm [10];
    while((lsu_read_32(CLP_MBOX_CSR_MBOX_LOCK) & MBOX_CSR_MBOX_LOCK_LOCK_MASK) != 0);
    volatile uint32_t* resp = (uint32_t*) CLP_MBOX_SRAM_BASE_ADDR;
    volatile uint32_t* safe_iter = (uint32_t*) (CLP_MBOX_SRAM_BASE_ADDR + 4);

    *resp = lsu_read_32(CLP_SOC_IFC_REG_INTERNAL_RV_MTIME_L);
    lsu_write_32(STDOUT, 0xE0);
    lsu_write_32(STDOUT, 0xE4);
    __asm__ volatile ("fence.i");
    lsu_write_32(STDOUT, 0xE3);
    *safe_iter = 0;
    while(*safe_iter < 10) {
        *resp = ((*resp) << 1) ^ lsu_read_32(CLP_SOC_IFC_REG_INTERNAL_RV_MTIME_L);
        array_in_dccm[*safe_iter] = (*resp);
        *safe_iter = (*safe_iter) + 1;
    }
    lsu_write_32(STDOUT, 0xE4);
    __asm__ volatile ("fence.i");
    *safe_iter = 0;
    while(*safe_iter < 10) {
        VPRINTF(LOW, "[%d]:%x\n", *safe_iter, array_in_dccm[*safe_iter]);
        *safe_iter = (*safe_iter) + 1;
    }
    lsu_write_32(CLP_MBOX_CSR_MBOX_UNLOCK, MBOX_CSR_MBOX_UNLOCK_UNLOCK_MASK);
}

void trigger_nmi_err(void) {
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_WDT_TIMER1_TIMEOUT_PERIOD_0, 0x00000040);
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_WDT_TIMER1_TIMEOUT_PERIOD_1, 0x00000000);
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_WDT_TIMER2_TIMEOUT_PERIOD_0, 0x00000040);
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_WDT_TIMER2_TIMEOUT_PERIOD_1, 0x00000000);
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_WDT_TIMER1_EN, SOC_IFC_REG_CPTRA_WDT_TIMER1_EN_TIMER1_EN_MASK);
    // Wait for NMI
    for (uint16_t slp = 0; slp < 1000; slp++) {
        __asm__ volatile ("nop");
    }
}

void trigger_crypto_err(void) {
    lsu_write_32(CLP_ECC_REG_ECC_CTRL, ECC_CMD_KEYGEN);
    lsu_write_32(CLP_DOE_REG_DOE_CTRL, 1 << DOE_REG_DOE_CTRL_CMD_LOW);
    // Wait for NMI
    for (uint16_t slp = 0; slp < 1000; slp++) {
        __asm__ volatile ("nop");
    }
}

// In the ROM .text section
void nmi_handler (void) {
    VPRINTF(LOW, "**** NMI ****\n");
    // NMI occurred, check if we had a fatal error
    if (lsu_read_32(CLP_SOC_IFC_REG_CPTRA_HW_ERROR_FATAL) & (SOC_IFC_REG_CPTRA_HW_ERROR_FATAL_ICCM_ECC_UNC_MASK |
                                                             SOC_IFC_REG_CPTRA_HW_ERROR_FATAL_CRYPTO_ERR_MASK |
                                                             SOC_IFC_REG_CPTRA_HW_ERROR_FATAL_NMI_PIN_MASK)) {
        VPRINTF(MEDIUM, "NMI w/ mcause [0x%x] during NMI test\n", csr_read_mcause());
        VPRINTF(MEDIUM, "mepc [0x%x]\n", csr_read_mepc());
        // If the FATAL Error bit for ECC UNC or NMI is masked, manually trigger firmware reset
        if (lsu_read_32(CLP_SOC_IFC_REG_INTERNAL_HW_ERROR_FATAL_MASK) &
                (SOC_IFC_REG_INTERNAL_HW_ERROR_FATAL_MASK_MASK_NMI_PIN_MASK |
                 SOC_IFC_REG_INTERNAL_HW_ERROR_FATAL_MASK_MASK_ICCM_ECC_UNC_MASK)) {
            VPRINTF(MEDIUM, "FATAL_ERROR bit is masked, no rst exp from TB: rst core manually!\n");
            SEND_STDOUT_CTRL(0xf6);
        // Otherwise, wait for core reset
        } else {
            // Test Crypto err
            test_w1c_masking(CLP_SOC_IFC_REG_CPTRA_HW_ERROR_FATAL,
                    SOC_IFC_REG_CPTRA_HW_ERROR_FATAL_CRYPTO_ERR_MASK,
                    1, "CPTRA_HW_ERROR_FATAL_CRYPTO_ERR");
            SEND_STDOUT_CTRL(0xf6);
        }
    }
}

void main(void) {

    rst_count++;
    VPRINTF(LOW, "----------------\nrst count = %d\n----------------\n", rst_count);
    VPRINTF(LOW, "==================\nIFC Register Misc Test\n==================\n\n");

    // Setup defualt trap handling
    init_interrupts();
    // Setup the NMI Handler
    lsu_write_32((uintptr_t) (CLP_SOC_IFC_REG_INTERNAL_NMI_VECTOR), (uint32_t) (nmi_handler));

    if (rst_count == 1) {
        // Test mtime_l overflow
        lsu_write_32(CLP_SOC_IFC_REG_INTERNAL_RV_MTIME_H, 0);
        lsu_write_32(CLP_SOC_IFC_REG_INTERNAL_RV_MTIME_L, 0xFFFFFFFF);
        for (uint8_t ii = 0; ii < 160; ii++) {
            __asm__ volatile ("nop"); // Sleep loop as "nop"
        }
        if(lsu_read_32(CLP_SOC_IFC_REG_INTERNAL_RV_MTIME_H) == 0) {
            VPRINTF(LOW, "MTIME_L overflow did not advance MTIME_H\n");
            error_count += 1;
        }
        // Set debug intent
        lsu_write_32(STDOUT, 0x127F);
        // Set Manuf state
        lsu_write_32(STDOUT, 0x147F);
        SEND_STDOUT_CTRL(TB_CMD_COLD_RESET);
        while(1);
    } else if (rst_count == 2) {
        test_masking(CLP_SOC_IFC_REG_SS_DBG_MANUF_SERVICE_REG_REQ,
                SOC_IFC_REG_SS_DBG_MANUF_SERVICE_REG_REQ_MANUF_DBG_UNLOCK_REQ_MASK,
                1, "SS_DBG_MANUF_SERVICE_REG_REQ");
        // Set Prod state
        lsu_write_32(STDOUT, 0x157F);
        SEND_STDOUT_CTRL(TB_CMD_COLD_RESET);
        while(1);
    } else if (rst_count == 3) {
        test_masking(CLP_SOC_IFC_REG_SS_DBG_MANUF_SERVICE_REG_REQ,
                SOC_IFC_REG_SS_DBG_MANUF_SERVICE_REG_REQ_PROD_DBG_UNLOCK_REQ_MASK,
                1, "SS_DBG_MANUF_SERVICE_REG_REQ");
        // Clear debug intent
        lsu_write_32(STDOUT, 0x137F);
        // Reenable FUSE WR
        lsu_write_32(STDOUT, 0x1A7F);
        test_masking(CLP_SOC_IFC_REG_FUSE_ANTI_ROLLBACK_DISABLE,
                SOC_IFC_REG_FUSE_ANTI_ROLLBACK_DISABLE_DIS_MASK,
                1, "FUSE_ANTI_ROLLBACK_DISABLE");
        test_lock_masking(CLP_SOC_IFC_REG_CPTRA_OWNER_PK_HASH_LOCK,
                "CPTRA_OWNER_PK_HASH_LOCK");
        test_lock_masking(CLP_SOC_IFC_REG_CPTRA_FUSE_AXI_USER_LOCK,
                "CPTRA_FUSE_AXI_USER_LOCK");
        test_lock_masking(CLP_SOC_IFC_REG_CPTRA_MBOX_AXI_USER_LOCK_0,
                "CPTRA_MBOX_AXI_USER_LOCK_0");
        test_lock_masking(CLP_SOC_IFC_REG_CPTRA_MBOX_AXI_USER_LOCK_1,
                "CPTRA_MBOX_AXI_USER_LOCK_1");
        test_lock_masking(CLP_SOC_IFC_REG_CPTRA_MBOX_AXI_USER_LOCK_2,
                "CPTRA_MBOX_AXI_USER_LOCK_2");
        test_lock_masking(CLP_SOC_IFC_REG_CPTRA_MBOX_AXI_USER_LOCK_3,
                "CPTRA_MBOX_AXI_USER_LOCK_3");
        test_lock_masking(CLP_SOC_IFC_REG_CPTRA_MBOX_AXI_USER_LOCK_4,
                "CPTRA_MBOX_AXI_USER_LOCK_4");
        soc_write_32(CLP_SOC_IFC_REG_CPTRA_BOOTFSM_GO, 0);
        test_masking(CLP_SOC_IFC_REG_CPTRA_BOOTFSM_GO,
                SOC_IFC_REG_CPTRA_BOOTFSM_GO_GO_MASK,
                1, "CPTRA_BOOTFSM_GO_GO_MASK");
        test_masking(CLP_SOC_IFC_REG_CPTRA_CLK_GATING_EN,
                SOC_IFC_REG_CPTRA_CLK_GATING_EN_CLK_GATING_EN_MASK,
                1, "CPTRA_CLK_GATING_EN");
        test_lock_masking(CLP_SOC_IFC_REG_CPTRA_FUSE_WR_DONE,
                "CPTRA_FUSE_WR_DONE");
        // TRNG
        lsu_write_32(CLP_SOC_IFC_REG_CPTRA_TRNG_STATUS, SOC_IFC_REG_CPTRA_TRNG_STATUS_DATA_REQ_MASK);
        for (int i = 0; i < 12; ++i) {
            soc_write_32(trng_data_addr_regs[i], xorshift32());
        }
        test_masking(CLP_SOC_IFC_REG_CPTRA_TRNG_STATUS,
                SOC_IFC_REG_CPTRA_TRNG_STATUS_DATA_WR_DONE_MASK,
                1, "CPTRA_TRNG_STATUS");
        lsu_write_32(CLP_SOC_IFC_REG_CPTRA_TRNG_CTRL, SOC_IFC_REG_CPTRA_TRNG_CTRL_CLEAR_MASK);
        for (int i = 0; i < 12; ++i) {
            if (lsu_read_32(trng_data_addr_regs[i]) != 0) {
                VPRINTF(LOW, "TRNG DATA [%d] was not cleared\n", i);
                error_count += 1;
            }
        }
        test_lock_masking(CLP_SOC_IFC_REG_CPTRA_TRNG_AXI_USER_LOCK,
                "CPTRA_TRNG_AXI_USER_LOCK");
        // Trigger MBOX port no lock error
        lsu_write_32(
                CLP_SOC_IFC_REG_INTERNAL_HW_ERROR_NON_FATAL_MASK,
                SOC_IFC_REG_INTERNAL_HW_ERROR_NON_FATAL_MASK_MASK_MBOX_PROT_NO_LOCK_MASK);
        trigger_mbox_prot_err();
        test_w1c_masking(CLP_SOC_IFC_REG_CPTRA_HW_ERROR_NON_FATAL,
                SOC_IFC_REG_CPTRA_HW_ERROR_NON_FATAL_MBOX_PROT_NO_LOCK_MASK,
                1, "CPTRA_HW_ERROR_NON_FATAL_MBOX_PROT_NO_LOCK");
        // Trigger MBOX Out-of-Order error
        lsu_write_32(
                CLP_SOC_IFC_REG_INTERNAL_HW_ERROR_NON_FATAL_MASK,
                SOC_IFC_REG_INTERNAL_HW_ERROR_NON_FATAL_MASK_MASK_MBOX_PROT_OOO_MASK);
        trigger_mbox_ooo_err();
        test_w1c_masking(CLP_SOC_IFC_REG_CPTRA_HW_ERROR_NON_FATAL,
                SOC_IFC_REG_CPTRA_HW_ERROR_NON_FATAL_MBOX_PROT_OOO_MASK,
                1, "CPTRA_HW_ERROR_NON_FATAL_MBOX_PROT_OOO");
        // Trigger MBOX uncorr error
        lsu_write_32(
                CLP_SOC_IFC_REG_INTERNAL_HW_ERROR_NON_FATAL_MASK,
                SOC_IFC_REG_INTERNAL_HW_ERROR_NON_FATAL_MASK_MASK_MBOX_ECC_UNC_MASK);
        trigger_mbox_unc_err();
        test_w1c_masking(CLP_SOC_IFC_REG_CPTRA_HW_ERROR_NON_FATAL,
                SOC_IFC_REG_CPTRA_HW_ERROR_NON_FATAL_MBOX_ECC_UNC_MASK,
                1, "CPTRA_HW_ERROR_NON_FATAL_MBOX_ECC_UNC");
        // Trigger ICCM uncorr error
        lsu_write_32(CLP_SOC_IFC_REG_INTERNAL_HW_ERROR_FATAL_MASK,
                SOC_IFC_REG_INTERNAL_HW_ERROR_FATAL_MASK_MASK_ICCM_ECC_UNC_MASK);
        trigger_iccm_unc_err();
        // This code should not be reached
        error_count += 1;
        while(1);
    } else if (rst_count == 4) {
        // Test ICCM uncorr error
        test_w1c_masking(CLP_SOC_IFC_REG_CPTRA_HW_ERROR_FATAL,
                SOC_IFC_REG_CPTRA_HW_ERROR_FATAL_ICCM_ECC_UNC_MASK,
                1, "CPTRA_HW_ERROR_FATAL_ICCM_ECC_UNC");
        // Mask errors
        lsu_write_32(CLP_SOC_IFC_REG_INTERNAL_HW_ERROR_FATAL_MASK,
                SOC_IFC_REG_INTERNAL_HW_ERROR_FATAL_MASK_MASK_DCCM_ECC_UNC_MASK);
        // Trigger DCCM uncorr error
        trigger_dccm_unc_err();
        // Test DCCM uncorr error
        test_w1c_masking(CLP_SOC_IFC_REG_CPTRA_HW_ERROR_FATAL,
                SOC_IFC_REG_CPTRA_HW_ERROR_FATAL_DCCM_ECC_UNC_MASK,
                1, "CPTRA_HW_ERROR_FATAL_DCCM_ECC_UNC");
        // Mask NMI pin
        lsu_write_32(CLP_SOC_IFC_REG_INTERNAL_HW_ERROR_FATAL_MASK,
                SOC_IFC_REG_INTERNAL_HW_ERROR_FATAL_MASK_MASK_NMI_PIN_MASK);
        // Trigger NMI pin
        trigger_nmi_err();
        // This code should not be reached
        error_count += 1;
        while(1);
    } else if (rst_count == 5) {
        // Test NMI pin
        test_w1c_masking(CLP_SOC_IFC_REG_CPTRA_HW_ERROR_FATAL,
                SOC_IFC_REG_CPTRA_HW_ERROR_FATAL_NMI_PIN_MASK,
                1, "CPTRA_HW_ERROR_FATAL_NMI_PIN");
        // Execute our NMI handler
        lsu_write_32(STDOUT, 0x0E7F);
        // Trigger Crypto err
        trigger_crypto_err();
        // This code should not be reached
        error_count += 1;
        while(1);
    }

    VPRINTF(LOW, "\nIFC Register Misc Test Completed\n");

    for (uint8_t ii = 0; ii < 160; ii++) {
        __asm__ volatile ("nop"); // Sleep loop as "nop"
    }

    if (error_count == 0 ) {
        SEND_STDOUT_CTRL(TB_CMD_TEST_PASS);
    } else {
        SEND_STDOUT_CTRL(TB_CMD_TEST_FAIL);
    }
}
