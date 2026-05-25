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
#include "riscv_hw_if.h"
#include "riscv-csr.h"
#include "printf.h"

volatile uint32_t* stdout           = (uint32_t *)STDOUT;
volatile uint32_t intr_count = 0;
#ifdef CPT_VERBOSITY
    enum printf_verbosity verbosity_g = CPT_VERBOSITY;
#else
    enum printf_verbosity verbosity_g = LOW;
#endif

volatile caliptra_intr_received_s cptra_intr_rcv = {0};

volatile uint32_t rst_count __attribute__((section(".dccm.persistent"))) = 0;

void inject_rand_key(uint8_t test_slot) {
    VPRINTF(LOW, "[SETUP] Injecting HMAC512 key into KV slot %d via TB...\n", test_slot);
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_0, (uint32_t)(0x807F | (test_slot << 8)));

    // Enables readout of keyvault
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_0, (uint32_t)(0xA07F | (test_slot << 8)));
    VPRINTF(ERROR, "[TEST] Keyvault readout: %x\n", lsu_read_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_INPUT_WIRES_0));

    if (lsu_read_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_INPUT_WIRES_0) == 0) {
        VPRINTF(ERROR, "[FAIL] Keyvault is not loaded!: %x\n", lsu_read_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_INPUT_WIRES_0));
        SEND_STDOUT_CTRL(0x01);
        while(1);
    }
}

void clear_kv_slot(uint8_t test_slot) {
    uint32_t key_ctrl_addr = CLP_KV_REG_KEY_CTRL_0 + (test_slot * 4);
    uint32_t key_ctrl_val = lsu_read_32(key_ctrl_addr);
    uint32_t clear_val = key_ctrl_val | KV_REG_KEY_CTRL_0_CLEAR_MASK;

    VPRINTF(LOW, "[TEST] Clearing KV slot %d via register...\n", test_slot);
    lsu_write_32(key_ctrl_addr, clear_val);

    // Give a delay, so the clear manages to propagate
    __asm__ volatile ("nop");
    __asm__ volatile ("nop");
    __asm__ volatile ("nop");
}

void run_lock_clear_fail_sequence(uint8_t test_slot, int lock_wr, int lock_use) {
    // ------------------------------------------------------------------
    // Step 1: Inject a key into KV slot (and lock the slot)
    // ------------------------------------------------------------------
    VPRINTF(LOW, "[SETUP] KV slot %d checking for clear failure when locked (write lock %s, use lock %s)...\n",
        test_slot, lock_wr ? "SET" : "CLEARED", lock_use ? "SET" : "CLEARED");
    inject_rand_key(test_slot);

    uint32_t key_ctrl_addr = CLP_KV_REG_KEY_CTRL_0 + (test_slot * 4);
    uint32_t key_ctrl_val = lsu_read_32(key_ctrl_addr);
    VPRINTF(LOW, "[SETUP] KEY_CTRL[%d] = 0x%08x\n", test_slot, key_ctrl_val);

    uint32_t lock_val = key_ctrl_val | (lock_wr ? KV_REG_KEY_CTRL_0_LOCK_WR_MASK : 0 ) | (lock_use ? KV_REG_KEY_CTRL_0_LOCK_USE_MASK : 0);
    lsu_write_32(key_ctrl_addr, lock_val);

    // Give a delay, so the lock manages to propagate
    __asm__ volatile ("nop");
    __asm__ volatile ("nop");
    __asm__ volatile ("nop");

    // ------------------------------------------------------------------
    // Step 2: Try to clear the keyvault
    // ------------------------------------------------------------------
    clear_kv_slot(test_slot);
    // Give a delay, so the clear manages to propagate
    __asm__ volatile ("nop");
    __asm__ volatile ("nop");
    __asm__ volatile ("nop");
    __asm__ volatile ("nop");

    // ------------------------------------------------------------------
    // Step 3: See the failure to clear
    // ------------------------------------------------------------------
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_0, (uint32_t)(0xA07F | (test_slot << 8)));

    if (lsu_read_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_INPUT_WIRES_0) == 0) {
        VPRINTF(ERROR, "[FAIL] Keyvault is cleared, but should have not!: %x\n", lsu_read_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_INPUT_WIRES_0));
        SEND_STDOUT_CTRL(0x01);
        while(1);
    }
    VPRINTF(LOW, "[PASS] KV slot %d was succesfully NOT cleared...\n", test_slot);
}

void run_lock_clear_sequence(uint8_t test_slot) {
    // ------------------------------------------------------------------
    // Step 1: Inject a key into KV slot (and check that the slot is not all zero)
    // ------------------------------------------------------------------
    inject_rand_key(test_slot);

    // ------------------------------------------------------------------
    // Step 2: Clear the keyvault
    // ------------------------------------------------------------------
    clear_kv_slot(test_slot);

    // ------------------------------------------------------------------
    // Step 3: Check if keyvault slot is all zero
    // ------------------------------------------------------------------
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_0, (uint32_t)(0xA07F | (test_slot << 8)));

    if (lsu_read_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_INPUT_WIRES_0) != 0) {
        VPRINTF(ERROR, "[FAIL] Keyvault is not reset!: %x\n", lsu_read_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_INPUT_WIRES_0));
        SEND_STDOUT_CTRL(0x01);
        while(1);
    }
    VPRINTF(LOW, "[PASS] KV slot %d was succesfully cleared...\n", test_slot);
}

void main() {
    uint32_t status;
    uint32_t error_field;

    VPRINTF(LOW, "============================================================\n");
    VPRINTF(LOW, " KV Clear Keyvault Test\n");
    VPRINTF(LOW, "============================================================\n");

    if (rst_count == 0) {
        for (uint8_t test_slot = 0; test_slot < 24; ++test_slot) {
            VPRINTF(LOW, "[TEST] KV slot %d\n", test_slot);

            // ------------------------------------------------------------------
            // PART 1 of test (write, then clear, without any lock)
            // ------------------------------------------------------------------
            run_lock_clear_sequence(test_slot);
            // ------------------------------------------------------------------
            // PART 2 of test (lock_wr)
            // ------------------------------------------------------------------
            run_lock_clear_fail_sequence(test_slot, 1, 0);
            // ------------------------------------------------------------------
            // PART 3 of test (both locks)
            // ------------------------------------------------------------------
            run_lock_clear_fail_sequence(test_slot, 1, 1);

        }
        // Locks are sticky, we need to do a reset to test other scenarios
        VPRINTF(LOW, "[TEST] Resetting the device...\n");
        rst_count++;
        SEND_STDOUT_CTRL(0xf6); //Issue warm reset
    } else if (rst_count == 1) {
        for (uint8_t test_slot = 0; test_slot < 24; ++test_slot) {
            VPRINTF(LOW, "[TEST] KV slot %d\n", test_slot);
            // ------------------------------------------------------------------
            // PART 4 of test (lock_use)
            // ------------------------------------------------------------------
            run_lock_clear_fail_sequence(test_slot, 0, 1);
        }
        // ------------------------------------------------------------------
        // Done
        // ------------------------------------------------------------------
        VPRINTF(LOW, "\n============================================================\n");
        VPRINTF(LOW, " ALL CHECKS PASSED\n");
        VPRINTF(LOW, "============================================================\n");
        SEND_STDOUT_CTRL(0xff);
    } else {
        VPRINTF(LOW, "[FAIL] Unexpected number of resets %d\n", rst_count);
        SEND_STDOUT_CTRL(0x1);
    }
}
