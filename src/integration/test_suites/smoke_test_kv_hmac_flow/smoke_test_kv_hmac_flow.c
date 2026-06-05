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

volatile uint32_t* stdout           = (uint32_t *)STDOUT;
volatile uint32_t  intr_count = 0;

#ifdef CPT_VERBOSITY
    enum printf_verbosity verbosity_g = CPT_VERBOSITY;
#else
    enum printf_verbosity verbosity_g = LOW;
#endif

#ifdef MY_RANDOM_SEED
    unsigned time = (unsigned) MY_RANDOM_SEED;
#else
    unsigned time = 0;
#endif

const int iteration_count = 2;

volatile caliptra_intr_received_s cptra_intr_rcv = {0};


/* HMAC384 test vector
    KEY = 0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b
    BLOCK = 4869205468657265800000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000440
    LFSR_SEED = C8F518D4F3AA1BD46ED56C1C3C9E16FB800AF504
    TAG = b6a8d5636f5c6a7224f9977dcf7ee6c7fb6d0c48cbdee9737a959796489bddbc4c5df61d5b3297b4fb68dab9f1b582c2
*/

/* HMAC512 test vector
    KEY = 0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b
    BLOCK = 4869205468657265800000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000440
    LFSR_SEED = random
    TAG = 637edc6e01dce7e6742a99451aae82df23da3e92439e590e43e761b33e910fb8ac2878ebd5803f6f0b61dbce5e251ff8789a4722c1be65aea45fd464e89f8f5b
*/

void randomize_kv_ids(uint8_t *key_id, uint8_t *block_id, uint8_t *tag_id){
    *key_id = (rand() % 0x7) + 1; // Limit to allow injection

    do {
        *block_id = rand() % KV_ENTRY_COUNT;
    } while(*block_id == *key_id);

    do {
        *tag_id = rand() % KV_ENTRY_COUNT; 
    } while((*tag_id == *key_id) || 
            (*tag_id == *block_id));

    printf("Randomized keyvault ids, key: 0x%x, block: 0x%x, tag: 0x%x\n", *key_id, *block_id, *tag_id);
}

void main() {
    printf("----------------------------------\n");
    printf(" KV Smoke Test With hmac384 flow !!\n");
    printf("----------------------------------\n");

    srand(time);
    //Call interrupt init
    init_interrupts();

    for(int i = 0; i < iteration_count; i++) {
        printf("\nIteration %0d of %0d\n\n", i + 1, iteration_count);

        //this is a random lfsr_seed
        uint32_t hmac384_lfsr_seed_data[12] =  {0xC8F518D4,
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

        uint8_t hmackey_kv_id;
        uint8_t hmacblock_kv_id;
        uint8_t tag_kv_id;

        hmac_io hmac384_key;
        hmac_io hmac384_block;
        hmac_io hmac384_lfsr_seed;
        hmac_io hmac384_tag;

        randomize_kv_ids(&hmackey_kv_id, &hmacblock_kv_id, &tag_kv_id);

        hmac384_key.kv_intf = TRUE;
        hmac384_key.kv_id = hmackey_kv_id;

        // NOTE: For instances where kv_intf is set to TRUE
        // this uses what is already stored in KV, so with the exception of the injected key - all zeroes
        // the output isn't also verified, since results are stored in KV and SW cannot read from Keyvault
        // There exists a test which does the comparisons but doesn't use KV - "smoke_test_hmac"
        // Otherwise, assertions in `caliptra_top_sva` ensure that the output of HMAC is stored in KV slots correctly
        hmac384_block.kv_intf = TRUE;
        hmac384_block.kv_id = hmacblock_kv_id;
        hmac384_block.data_size = 32;

        hmac384_lfsr_seed.data_size = 12;
        for (int i = 0; i < hmac384_lfsr_seed.data_size; i++)
            hmac384_lfsr_seed.data[i] = hmac384_lfsr_seed_data[i];

        hmac384_tag.kv_intf = TRUE;
        hmac384_tag.kv_id = tag_kv_id;
        hmac384_tag.data_size = 12;

        //inject hmac384_key to kv key reg (in RTL)
        uint8_t key384_inject_cmd = 0xa0 + (hmac384_key.kv_id & 0x7);
        printf("%c", key384_inject_cmd);

        hmac384_flow(hmac384_key, hmac384_block, hmac384_lfsr_seed, hmac384_tag, TRUE);
        hmac_zeroize();

        //release the key injection, so we don't accidentally pollute next iteration of test
        printf("%c", 0x7f);
        lsu_write_32(CLP_KV_REG_KEY_CTRL_0 + (hmackey_kv_id * 4), KV_REG_KEY_CTRL_0_CLEAR_MASK);
        lsu_write_32(CLP_KV_REG_KEY_CTRL_0 + (tag_kv_id * 4), KV_REG_KEY_CTRL_0_CLEAR_MASK);

        printf("----------------------------------\n");
        printf(" KV Smoke Test With hmac512 flow !!\n");
        printf("----------------------------------\n");

        //this is a random lfsr_seed
        uint32_t hmac512_lfsr_seed_data[12] =  {0xC8F518D4,
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

        hmac_io hmac512_key;
        hmac_io hmac512_block;
        hmac_io hmac512_lfsr_seed;
        hmac_io hmac512_tag;

        randomize_kv_ids(&hmackey_kv_id, &hmacblock_kv_id, &tag_kv_id);

        hmac512_key.kv_intf = TRUE;
        hmac512_key.kv_id = hmackey_kv_id;

        hmac512_block.kv_intf = FALSE;
        hmac512_block.data_size = 32;

        hmac512_lfsr_seed.data_size = 12;
        for (int i = 0; i < hmac512_lfsr_seed.data_size; i++)
            hmac512_lfsr_seed.data[i] = hmac512_lfsr_seed_data[i];

        hmac512_tag.kv_intf = TRUE;
        hmac512_tag.kv_id = tag_kv_id;
        hmac512_tag.data_size = 16;

        //inject hmac512_key to kv key reg (in RTL)
        uint8_t key512_inject_cmd = 0xa8 + (hmac512_key.kv_id & 0x7);
        printf("%c", key512_inject_cmd);

        hmac512_flow(hmac512_key, hmac512_block, hmac512_lfsr_seed, hmac512_tag, TRUE);
        hmac_zeroize();

        //release the key injection, so we don't accidentally pollute next iteration of test
        printf("%c", 0x7f);
        lsu_write_32(CLP_KV_REG_KEY_CTRL_0 + (hmackey_kv_id * 4), KV_REG_KEY_CTRL_0_CLEAR_MASK);
        lsu_write_32(CLP_KV_REG_KEY_CTRL_0 + (tag_kv_id * 4), KV_REG_KEY_CTRL_0_CLEAR_MASK);
    }

    printf("%c",0xff); //End the test
    
}
