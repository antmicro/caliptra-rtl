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
volatile uint32_t  rst_count __attribute__((section(".dccm.persistent"))) = 0;
volatile rv_exception_struct_s exc_flag __attribute__((section(".dccm.persistent")));

#ifdef CPT_VERBOSITY
    enum printf_verbosity verbosity_g = CPT_VERBOSITY;
#else
    enum printf_verbosity verbosity_g = LOW;
#endif

volatile caliptra_intr_received_s cptra_intr_rcv = {0};

#define TB_CMD_TEST_PASS 0xFF
#define TB_CMD_TEST_FAIL 0x01

#define UNPROVISIONED   0x0
#define MANUFACTURING   0x1
#define PRODUCTION      0x3
#define DEBUG_LOCKED    0x4
#define DEBUG_UNLOCKED  0x0

void main(void) {

    rst_count++;
    if (rst_count == 1) {
        // Enable debug intent
        lsu_write_32(STDOUT, 0x127F);
        SEND_STDOUT_CTRL(0xF5);
        while(1);
    }
    VPRINTF(LOW, "==================\nSecurity State Lock Smoke Test\n==================\n\n");
    uint32_t reg_value;
    reg_value = lsu_read_32(CLP_SOC_IFC_REG_CPTRA_SECURITY_STATE);
    if (reg_value != (PRODUCTION | DEBUG_LOCKED)) {
        VPRINTF(LOW,  "Unexpected Security State after reset: expected %x , got %x\n",
                (PRODUCTION | DEBUG_LOCKED), reg_value);
        SEND_STDOUT_CTRL(TB_CMD_TEST_FAIL);
    }
    // Set Unprovisioned mode and debug unlocked
    lsu_write_32(STDOUT, 0x1F7F);
    lsu_write_32(STDOUT, 0x187F);
    __asm__ volatile ("fence.i");

    reg_value = lsu_read_32(CLP_SOC_IFC_REG_CPTRA_SECURITY_STATE);
    if (reg_value != (PRODUCTION | DEBUG_LOCKED)) {
        VPRINTF(LOW,  "Security State was modified, but should be locked: expected %x , got %x\n",
                (PRODUCTION | DEBUG_LOCKED), reg_value);
        SEND_STDOUT_CTRL(TB_CMD_TEST_FAIL);
    }
    // Unlock device
    lsu_write_32(CLP_SOC_IFC_REG_SS_SOC_DBG_UNLOCK_LEVEL_0, 0x1);
    // Let changes propagate
    for (uint8_t ii = 0; ii < 160; ii++) {
        __asm__ volatile ("nop"); // Sleep loop as "nop"
    }
    reg_value = lsu_read_32(CLP_SOC_IFC_REG_CPTRA_SECURITY_STATE);
    if (reg_value != (UNPROVISIONED | DEBUG_UNLOCKED)) {
        VPRINTF(LOW,  "Unexpected Security State: expected %x , got %x\n",
                (UNPROVISIONED | DEBUG_UNLOCKED), reg_value);
        SEND_STDOUT_CTRL(TB_CMD_TEST_FAIL);
    }
    // Set Manufacturing mode
    lsu_write_32(STDOUT, 0x147F);
    // Let changes propagate
    for (uint8_t ii = 0; ii < 160; ii++) {
        __asm__ volatile ("nop"); // Sleep loop as "nop"
    }
    reg_value = lsu_read_32(CLP_SOC_IFC_REG_CPTRA_SECURITY_STATE);
    if (reg_value != (MANUFACTURING | DEBUG_UNLOCKED)) {
        VPRINTF(LOW,  "Unexpected Security State: expected %x , got %x\n",
                (MANUFACTURING | DEBUG_UNLOCKED), reg_value);
        SEND_STDOUT_CTRL(TB_CMD_TEST_FAIL);
    }
    // Set debug locked
    lsu_write_32(STDOUT, 0x197F);
    // Let changes propagate
    for (uint8_t ii = 0; ii < 160; ii++) {
        __asm__ volatile ("nop"); // Sleep loop as "nop"
    }
    reg_value = lsu_read_32(CLP_SOC_IFC_REG_CPTRA_SECURITY_STATE);
    if (reg_value != (MANUFACTURING | DEBUG_LOCKED)) {
        VPRINTF(LOW,  "Unexpected Security State: expected %x , got %x\n",
                (MANUFACTURING | DEBUG_LOCKED), reg_value);
        SEND_STDOUT_CTRL(TB_CMD_TEST_FAIL);
    }
    // Set Unprovisioned mode
    lsu_write_32(STDOUT, 0x1F7F);
    // Let changes propagate
    for (uint8_t ii = 0; ii < 160; ii++) {
        __asm__ volatile ("nop"); // Sleep loop as "nop"
    }
    reg_value = lsu_read_32(CLP_SOC_IFC_REG_CPTRA_SECURITY_STATE);
    if (reg_value != (UNPROVISIONED | DEBUG_LOCKED)) {
        VPRINTF(LOW,  "Unexpected Security State: expected %x , got %x\n",
                (UNPROVISIONED | DEBUG_LOCKED), reg_value);
        SEND_STDOUT_CTRL(TB_CMD_TEST_FAIL);
    }
    // Relock device
    lsu_write_32(CLP_SOC_IFC_REG_SS_SOC_DBG_UNLOCK_LEVEL_0, 0);
    // Set Manufacturing mode and unlock device
    lsu_write_32(STDOUT, 0x147F);
    lsu_write_32(STDOUT, 0x187F);
    // Let changes propagate
    for (uint8_t ii = 0; ii < 160; ii++) {
        __asm__ volatile ("nop"); // Sleep loop as "nop"
    }
    reg_value = lsu_read_32(CLP_SOC_IFC_REG_CPTRA_SECURITY_STATE);
    if (reg_value != (UNPROVISIONED | DEBUG_LOCKED)) {
        VPRINTF(LOW,  "Security State was modified, but should be locked: expected %x , got %x\n",
                (UNPROVISIONED | DEBUG_LOCKED), reg_value);
        SEND_STDOUT_CTRL(TB_CMD_TEST_FAIL);
    }

    VPRINTF(LOW, "\nSecurity State Lock Smoke Test Completed\n");

    for (uint8_t ii = 0; ii < 160; ii++) {
        __asm__ volatile ("nop"); // Sleep loop as "nop"
    }

    SEND_STDOUT_CTRL(TB_CMD_TEST_PASS);
}
