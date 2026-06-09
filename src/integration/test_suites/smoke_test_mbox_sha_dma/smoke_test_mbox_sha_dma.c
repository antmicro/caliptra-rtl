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
//
// Performs edge concurrency cases that are difficult to trigger in regular execution
// Accesses are made directly from TB services with precalculated latencies

#include "caliptra_defines.h"
#include "riscv_hw_if.h"
#include "soc_access.h"
#include "soc_ifc.h"
#include <stdint.h>
#include "printf.h"
#include "caliptra_isr.h"
#include "mbox_vectors.h"

volatile uint32_t* stdout           = (uint32_t *)STDOUT;
volatile uint32_t  intr_count       = 0;
volatile int rst_count __attribute__((section(".dccm.persistent"))) = 0;
#ifdef CPT_VERBOSITY
    enum printf_verbosity             verbosity_g = CPT_VERBOSITY;
#else
    enum printf_verbosity             verbosity_g = LOW;
#endif

#define TB_CMD_TEST_PASS          0xFF
#define TB_CMD_TEST_FAIL          0x01

volatile caliptra_intr_received_s cptra_intr_rcv = {0};

#define FAIL(...) do { VPRINTF(ERROR, __VA_ARGS__); SEND_STDOUT_CTRL(TB_CMD_TEST_FAIL); for(;;); } while(0);
#define SHA_MODE (SHA_MBOX_512 | SHA512_ACC_CSR_MODE_ENDIAN_TOGGLE_MASK)

static void get_sha_lock() {
    for (int i = 0; i < 1000; i++)
        if (lsu_read_32(CLP_SHA512_ACC_CSR_LOCK) & SHA512_ACC_CSR_LOCK_LOCK_MASK)
            return;

    FAIL("Failed to acquire SHA lock from SoC after 1000 iterations!\n");
}

static void release_sha_lock() {
    lsu_write_32(CLP_SHA512_ACC_CSR_LOCK, SHA512_ACC_CSR_LOCK_LOCK_MASK);
}


static void wait_sha_done() {
    for (int i = 0; i < 10000; i++)
        if(lsu_read_32(CLP_SHA512_ACC_CSR_STATUS) & SHA512_ACC_CSR_STATUS_VALID_MASK) {
            lsu_write_32(CLP_SHA512_ACC_CSR_STATUS, 0);
            return;
        }

    FAIL("Awaiting SHA result timed out!\n");
}

void main () {
    uint32_t read_payload[DMA_DATA_LEN];

    VPRINTF(LOW, "----------------------------------------------------\n");
    VPRINTF(LOW, " Mailbox Concurrent DMA/SHA/DIR Access Smoke Test!!\n" );
    VPRINTF(LOW, "----------------------------------------------------\n");

    // Acquire the mailbox lock
    if (soc_ifc_mbox_acquire_lock(1)) {
        FAIL("Acquire mailbox lock failed\n");
    }

    VPRINTF(LOW, "Populate SRAM for SHA\n");
    for (uint32_t dw = 0; dw < SHA_DATA_LEN; dw++) {
        lsu_write_32(CLP_MBOX_SRAM_BASE_ADDR + (dw << 2), SHA_DATA[dw]);
    }

    VPRINTF(LOW, "Populate SRAM for DMA\n");
    for (uint32_t dw = 0; dw < DMA_DATA_LEN; dw++) {
        lsu_write_32(CLP_MBOX_SRAM_BASE_ADDR + 0x4400 + (dw << 2), dma_data[dw%DMA_DATASET_LEN]);
    }

    lsu_write_32(CLP_MBOX_CSR_MBOX_UNLOCK, MBOX_CSR_MBOX_UNLOCK_UNLOCK_MASK);

    get_sha_lock();
    lsu_write_32(CLP_SHA512_ACC_CSR_MODE, SHA_MODE);
    lsu_write_32(CLP_SHA512_ACC_CSR_DLEN, SHA_DATA_LEN * 4);

    for (uint32_t dw = 0; dw < SHA_DATA_LEN; dw++) {
        lsu_write_32(CLP_SHA512_ACC_CSR_DATAIN, SHA_DATA[dw]);
    }

    // Start SHA
    VPRINTF(LOW, "Schedule SHA access\n");
    lsu_write_32(CLP_SHA512_ACC_CSR_EXECUTE, SHA512_ACC_CSR_EXECUTE_EXECUTE_MASK);

    VPRINTF(LOW, "Schedule DMA access\n");
    if (soc_ifc_axi_dma_send_mbox_payload_no_wait(0x4400, AXI_SRAM_BASE_ADDR, 0, DMA_DATA_LEN*4, 0)) {
        FAIL("Failed to run DMA!");
    }

    VPRINTF(LOW, "Perform direct reads\n");
    for (uint32_t dw = 0; dw < DMA_DATA_LEN; dw++) {
        lsu_read_32(CLP_MBOX_SRAM_BASE_ADDR + (dw << 2));
    }

    VPRINTF(LOW, "Wait for DMA done\n");
    soc_ifc_axi_dma_wait_idle(1);

    VPRINTF(LOW, "Wait for SHA done\n");
    wait_sha_done();

    VPRINTF(LOW, "Verify SHA digest\n");
    uint8_t failed = 0, it = 0;
    for (uint32_t addr = CLP_SHA512_ACC_CSR_DIGEST_0; addr < CLP_SHA512_ACC_CSR_DIGEST_15; addr += 4) {
        uint32_t digest = lsu_read_32(addr);
        if (digest != SHA_EXPECTED_DIGEST[it]) {
            failed = 1;
            VPRINTF(ERROR, "SHA accelerator invalid digest. [%d]: Expected: 0x%x, Got: 0x%x\n", it, SHA_EXPECTED_DIGEST[it], digest);
        }
        it++;
    }
    if (failed) {
        FAIL("SHA accelerator invalid digest.\n");
    }

    release_sha_lock();

    // Read data back from AXI SRAM and confirm it matches
    soc_ifc_axi_dma_read_ahb_payload(AXI_SRAM_BASE_ADDR, 0, read_payload, DMA_DATA_LEN*4, 0);

    VPRINTF(LOW, "Verify data written by DMA\n");
    for (uint32_t dw = 0; dw < DMA_DATA_LEN; dw++) {
        if (read_payload[dw] != dma_data[dw%DMA_DATASET_LEN]) {
            VPRINTF(ERROR, "DMA operation mismatch! [%d] Expected: 0x%x. Got: 0x%x. \n", dw, dma_data[dw%DMA_DATASET_LEN], read_payload[dw]);
            failed = 1;
        }
    }

    if (failed) {
        FAIL("Readback after DMA failed.\n");
    }

    VPRINTF(LOW, "Mailbox Concurrent Access test successful!\n");
    SEND_STDOUT_CTRL(TB_CMD_TEST_PASS);
}