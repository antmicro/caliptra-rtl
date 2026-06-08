// SPDX-License-Identifier: Apache-2.0
// Copyright 2019 Western Digital Corporation or its affiliates.
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
#include "printf.h"
#include "soc_ifc.h"
#include "xorshift.h"

volatile uint32_t* stdout           = (uint32_t *)STDOUT;
volatile uint32_t  intr_count = 0;
volatile uint32_t  rst_count __attribute__((section(".dccm.persistent"))) = 0;
#ifdef CPT_VERBOSITY
    enum printf_verbosity verbosity_g = CPT_VERBOSITY;
#else
    enum printf_verbosity verbosity_g = LOW;
#endif

volatile caliptra_intr_received_s cptra_intr_rcv = {0};

#define MBOX_DLEN_VAL             0x00000020

/* --------------- Function Definitions --------------- */
void main() {

    rst_count++;
    if (rst_count == 1) {
        printf("----------------------------------\n");
        printf(" JTAG unlock/uds flow test setup\n");
        printf("----------------------------------\n");
        uint32_t env = xorshift32();
        // Enable debug
        lsu_write_32(stdout, 0x127F);
        // FIXME: OpenOCD workaround -> enable core DMI access to allow for examination
        lsu_write_32(stdout, 0x0D7F);
        // Select between
        if (env & 1) {
            lsu_write_32(stdout, 0x147F);
        } else {
            lsu_write_32(stdout, 0x157F);
        }
        lsu_write_32(stdout, 0xf5);
    }
    printf("----------------------------------\n");
    printf(" JTAG unlock/uds flow test\n");
    printf("----------------------------------\n");

    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_DBG_MANUF_SERVICE_REG, lsu_read_32(CLP_SOC_IFC_REG_CPTRA_SECURITY_STATE) & SOC_IFC_REG_CPTRA_SECURITY_STATE_DEVICE_LIFECYCLE_MASK);

    //make tap mailbox available
    lsu_write_32(CLP_SOC_IFC_REG_SS_DBG_MANUF_SERVICE_REG_RSP,SOC_IFC_REG_SS_DBG_MANUF_SERVICE_REG_RSP_TAP_MAILBOX_AVAILABLE_MASK);
    //poll for mbox lock
    while((lsu_read_32(CLP_MBOX_CSR_MBOX_LOCK) & MBOX_CSR_MBOX_LOCK_LOCK_MASK) == 1);

    //put mailbox in tap mode
    lsu_write_32(CLP_MBOX_CSR_TAP_MODE, MBOX_CSR_TAP_MODE_ENABLED_MASK);

    //write command
    lsu_write_32(CLP_MBOX_CSR_MBOX_CMD, 0xdeadbeef);

    //write dlen
    lsu_write_32(CLP_MBOX_CSR_MBOX_DLEN, 0);

    //set execute
    lsu_write_32(CLP_MBOX_CSR_MBOX_EXECUTE, MBOX_CSR_MBOX_EXECUTE_EXECUTE_MASK);

    // Test is ended by TCL script
    while(1);
}
