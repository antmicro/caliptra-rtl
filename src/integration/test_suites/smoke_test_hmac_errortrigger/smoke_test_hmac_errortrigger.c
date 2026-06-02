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
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include "printf.h"
#include "hmac.h"
#include "keyvault.h"

#ifdef CPT_VERBOSITY
    enum printf_verbosity verbosity_g = CPT_VERBOSITY;
#else
    enum printf_verbosity verbosity_g = LOW;
#endif
volatile uint32_t* stdout           = (uint32_t *)STDOUT;
volatile uint32_t  intr_count       = 0;
volatile uint32_t  rst_count __attribute__((section(".dccm.persistent"))) = 0;

#ifdef MY_RANDOM_SEED
    unsigned time = (unsigned) MY_RANDOM_SEED;
#else
    unsigned time = 0;
#endif

volatile caliptra_intr_received_s cptra_intr_rcv = {0};


void main() {

    //Call interrupt init
    init_interrupts();

    // Entry message
    VPRINTF(LOW, "----------------------------------\n");
    VPRINTF(LOW, " HMAC error_trigger smoke test !!\n" );
    VPRINTF(LOW, " iteration:  %d\n", rst_count        );
    VPRINTF(LOW, "----------------------------------\n");

    srand(time);

    volatile uint32_t * reg_ptr;
    uint8_t key_slot = rand() % KV_ENTRY_COUNT;
    uint8_t key_inject_cmd;

    if(rst_count == 0) {
        VPRINTF(LOW, " ***** HMAC512 key_zero_error !!\n");
        // wait for HMAC to be ready
        while((lsu_read_32(CLP_HMAC_REG_HMAC512_STATUS) & HMAC_REG_HMAC512_STATUS_READY_MASK) == 0);

        //inject zero to kv key reg (in RTL)
        key_inject_cmd = 0xa8;
        printf("%c", key_inject_cmd);

        // Program KEY Read from key_kv_id
        lsu_write_32(CLP_HMAC_REG_HMAC512_KV_RD_KEY_CTRL, HMAC_REG_HMAC512_KV_RD_KEY_CTRL_READ_EN_MASK |
                                                        ((key_slot << HMAC_REG_HMAC512_KV_RD_KEY_CTRL_READ_ENTRY_LOW) & HMAC_REG_HMAC512_KV_RD_KEY_CTRL_READ_ENTRY_MASK));

        // Check that HMAC KEY is loaded
        while((lsu_read_32(CLP_HMAC_REG_HMAC512_KV_RD_KEY_STATUS) & HMAC_REG_HMAC512_KV_RD_KEY_STATUS_VALID_MASK) == 0);

        // Enable HMAC core
        lsu_write_32(CLP_HMAC_REG_HMAC512_CTRL, HMAC_REG_HMAC512_CTRL_INIT_MASK |
                                                    (HMAC512_MODE << HMAC_REG_HMAC512_CTRL_MODE_LOW));
        // wait for HMAC process to be done
        wait_for_hmac_intr();
        if ((cptra_intr_rcv.hmac_error == 0)){
            printf("\nHMAC key_zero error is not detected.\n");
            printf("%c", 0x1);
            while(1);
        }
        hmac_zeroize();
        //Issue warm reset
        rst_count++;
        printf("%c",0xf6);
    }
    else if(rst_count == 1) {
        VPRINTF(LOW, " ***** HMAC384 key_zero_error !!\n");
        // wait for HMAC to be ready
        while((lsu_read_32(CLP_HMAC_REG_HMAC512_STATUS) & HMAC_REG_HMAC512_STATUS_READY_MASK) == 0);

        //inject zero to kv key reg (in RTL)
        key_inject_cmd = 0xa8;
        printf("%c", key_inject_cmd);

        // Program KEY Read from key_kv_id
        lsu_write_32(CLP_HMAC_REG_HMAC512_KV_RD_KEY_CTRL, HMAC_REG_HMAC512_KV_RD_KEY_CTRL_READ_EN_MASK |
                                                        ((key_slot << HMAC_REG_HMAC512_KV_RD_KEY_CTRL_READ_ENTRY_LOW) & HMAC_REG_HMAC512_KV_RD_KEY_CTRL_READ_ENTRY_MASK));

        // Check that HMAC KEY is loaded
        while((lsu_read_32(CLP_HMAC_REG_HMAC512_KV_RD_KEY_STATUS) & HMAC_REG_HMAC512_KV_RD_KEY_STATUS_VALID_MASK) == 0);

        // Enable HMAC core
        lsu_write_32(CLP_HMAC_REG_HMAC512_CTRL, HMAC_REG_HMAC512_CTRL_NEXT_MASK |
                                                    (HMAC384_MODE << HMAC_REG_HMAC512_CTRL_MODE_LOW));
        // wait for HMAC process to be done
        wait_for_hmac_intr();
        if ((cptra_intr_rcv.hmac_error == 0)){
            printf("\nHMAC key_zero error is not detected.\n");
            printf("%c", 0x1);
            while(1);
        }
        hmac_zeroize();
        //Issue warm reset
        rst_count++;
        printf("%c",0xf6);
    }
    if(rst_count == 2) {
        VPRINTF(LOW, " ***** HMAC key_mode_error !!\n");
        
        //inject hmac384_key to kv key reg (in RTL)
        key_inject_cmd = 0xa0 + (key_slot & 0x7);
        printf("%c", key_inject_cmd);

        // wait for HMAC to be ready
        while((lsu_read_32(CLP_HMAC_REG_HMAC512_STATUS) & HMAC_REG_HMAC512_STATUS_READY_MASK) == 0);

        // Program KEY Read from key_kv_id
        lsu_write_32(CLP_HMAC_REG_HMAC512_KV_RD_KEY_CTRL, HMAC_REG_HMAC512_KV_RD_KEY_CTRL_READ_EN_MASK |
                                                        ((key_slot << HMAC_REG_HMAC512_KV_RD_KEY_CTRL_READ_ENTRY_LOW) & HMAC_REG_HMAC512_KV_RD_KEY_CTRL_READ_ENTRY_MASK));

        // Check that HMAC KEY is loaded
        while((lsu_read_32(CLP_HMAC_REG_HMAC512_KV_RD_KEY_STATUS) & HMAC_REG_HMAC512_KV_RD_KEY_STATUS_VALID_MASK) == 0);

        // Enable HMAC core with wrong MODE
        lsu_write_32(CLP_HMAC_REG_HMAC512_CTRL, HMAC_REG_HMAC512_CTRL_INIT_MASK |
                                                    (HMAC512_MODE << HMAC_REG_HMAC512_CTRL_MODE_LOW));
        // wait for HMAC process to be done
        wait_for_hmac_intr();
        if ((cptra_intr_rcv.hmac_error == 0)){
            printf("\nHMAC key_mode_error is not detected.\n");
            printf("%c", 0x1);
            while(1);
        }
        hmac_zeroize();
        //Issue warm reset
        rst_count++;
        printf("%c",0xf6);
    }
    if(rst_count == 3) {
        // Adapted from "smoke_test_hmac"
        VPRINTF(LOW, " ***** HMAC KV write error !!\n");

        // Kill kv_hmac_tag_w_flow assertion, since KV slot will not match HMAC tag output (since we will lock the KV slot)
        lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_0, 0x567F);

        hmac_io hmac512_key;
        hmac_io hmac512_tag;
        hmac_io hmac_block;
        hmac_io hmac_lfsr_seed;

        // 512-bit key
        uint32_t key512_data[] = {0x0b0b0b0b,
                                  0x0b0b0b0b,
                                  0x0b0b0b0b,
                                  0x0b0b0b0b,
                                  0x0b0b0b0b,
                                  0x0b0b0b0b,
                                  0x0b0b0b0b,
                                  0x0b0b0b0b,
                                  0x0b0b0b0b,
                                  0x0b0b0b0b,
                                  0x0b0b0b0b,
                                  0x0b0b0b0b,
                                  0x0b0b0b0b,
                                  0x0b0b0b0b,
                                  0x0b0b0b0b,
                                  0x0b0b0b0b};

        // LSFR Seed value
        uint32_t lfsr_seed_data[12] =  {0xC8F518D4,
                                        0xF3AA1BD4,
                                        0x6ED56C1C,
                                        0x3C9E16FB,
                                        0x800AF504,
                                        0xC8F518D4,
                                        0xF3AA1BD4,
                                        0x6ED56C1C,
                                        0x3C9E16FB,
                                        0x800AF504,
                                        0xC8F518D4,
                                        0xF3AA1BD4}; 

        uint32_t block_data[] = {0x48692054,
                                 0x68657265,
                                 0x80000000,
                                 0x00000000,
                                 0x00000000,
                                 0x00000000,
                                 0x00000000,
                                 0x00000000,
                                 0x00000000,
                                 0x00000000,
                                 0x00000000,
                                 0x00000000,
                                 0x00000000,
                                 0x00000000,
                                 0x00000000,
                                 0x00000000,
                                 0x00000000,
                                 0x00000000,
                                 0x00000000,
                                 0x00000000,
                                 0x00000000,
                                 0x00000000,
                                 0x00000000,
                                 0x00000000,
                                 0x00000000,
                                 0x00000000,
                                 0x00000000,
                                 0x00000000,
                                 0x00000000,
                                 0x00000000,
                                 0x00000000,
                                 0x00000440};

        // Lock target register
        // we use key_slot for uniformity, but this is really the target KV slot for TAG, not for the key
        uint32_t key_ctrl_addr = CLP_KV_REG_KEY_CTRL_0 + (key_slot * 4);
        uint32_t key_ctrl_val = lsu_read_32(key_ctrl_addr);
        VPRINTF(LOW, "[SETUP] KEY_CTRL[%d] = 0x%08x\n", key_slot, key_ctrl_val);
        lsu_write_32(key_ctrl_addr, key_ctrl_val | KV_REG_KEY_CTRL_0_LOCK_USE_MASK);

        hmac512_key.kv_intf = FALSE;
        hmac512_key.data_size = 16;
        for (int i = 0; i < hmac512_key.data_size; i++)
            hmac512_key.data[i] = key512_data[i];

        hmac512_tag.kv_intf = TRUE;
        hmac512_tag.kv_id = key_slot;
        hmac512_tag.data_size = 16;

        hmac_block.kv_intf = FALSE;
        hmac_block.data_size = 32;
        for (int i = 0; i < hmac_block.data_size; i++)
            hmac_block.data[i] = block_data[i];

        hmac_lfsr_seed.kv_intf = FALSE;
        hmac_lfsr_seed.data_size = 12;
        for (int i = 0; i < hmac_lfsr_seed.data_size; i++)
            hmac_lfsr_seed.data[i] = lfsr_seed_data[i];

        hmac512_flow(hmac512_key, hmac_block, hmac_lfsr_seed, hmac512_tag, TRUE);

        // KV_WRITE_FAIL is encoded as 0x2, shifted left by 2 bits
        if((lsu_read_32(CLP_HMAC_REG_HMAC512_KV_WR_STATUS) & HMAC_REG_HMAC512_KV_WR_STATUS_ERROR_MASK) != 0x8) {
            VPRINTF(LOW, "[FAIL] No error seen in STATUS_ERROR field, even though tag write slot was locked! Got 0x%x\n",
                        lsu_read_32(CLP_HMAC_REG_HMAC512_KV_WR_STATUS));
        }

    }
    // Write 0xff to STDOUT for TB to terminate test.
    SEND_STDOUT_CTRL( 0xff);
    while(1);

}
