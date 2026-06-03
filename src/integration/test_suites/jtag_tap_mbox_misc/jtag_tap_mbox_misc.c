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

volatile uint32_t* stdout           = (uint32_t *)STDOUT;
volatile uint32_t  intr_count = 0;
volatile uint32_t  rst_count __attribute__((section(".dccm.persistent"))) = 0;
#ifdef CPT_VERBOSITY
    enum printf_verbosity verbosity_g = CPT_VERBOSITY;
#else
    enum printf_verbosity verbosity_g = LOW;
#endif

volatile caliptra_intr_received_s cptra_intr_rcv = {0};

#define TB_CMD_TEST_PASS          0xFF
#define MBOX_DLEN_VAL             0x00000020

/* --------------- Function Definitions --------------- */
void main() {

    // Production mode
    lsu_write_32(stdout, 0x157F);
    // Enable debug intent
    lsu_write_32(stdout, 0x167F);
    lsu_write_32(stdout, 0x127F);
    // Enter debug unlocked
    lsu_write_32(stdout, 0x187F);

    // Initialize interrupts (if any)
    init_interrupts();

    mbox_op_s op;
    uint32_t ii;
    uint32_t data;
    enum mbox_fsm_e state;
    enum mbox_status_e status;
    uint32_t mbox_data[] = { 0x00000000,
                             0x11111111,
                             0x22222222,
                             0x33333333,
                             0x44444444,
                             0x55555555,
                             0x66666666,
                             0x77777777 };

    uint32_t exp_mbox_data[] = { 0x77777777,
                                 0x66666666,
                                 0x55555555,
                                 0x44444444,
                                 0x33333333,
                                 0x22222222,
                                 0x11111111,
                                 0x00000000 };
    uint32_t mbox_cmd = 0xaface0ff;
    uint32_t exp_mbox_cmd = 0x4e110df7;
    uint32_t read_data;

    printf("-------------------------------\n");
    printf(" Mailbox coverage corner cases\n" );
    printf("-------------------------------\n");

    VPRINTF(LOW, "FW: Test 1 - SoC mailbox access request while TAP locks the mailbox\n");

    // Enable injection of SoC mailbox access request
    VPRINTF(LOW, "FW: Enable injection of SoC mailbox access request\n");
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_0, 0x507F);

    // Make tap mailbox available
    lsu_write_32(CLP_SOC_IFC_REG_SS_DBG_MANUF_SERVICE_REG_RSP, SOC_IFC_REG_SS_DBG_MANUF_SERVICE_REG_RSP_TAP_MAILBOX_AVAILABLE_MASK);

    // Put mailbox in tap mode
    lsu_write_32(CLP_MBOX_CSR_TAP_MODE, MBOX_CSR_TAP_MODE_ENABLED_MASK);

    // Poll status until TAP locks it
    while((lsu_read_32(CLP_MBOX_CSR_MBOX_STATUS) & MBOX_CSR_MBOX_STATUS_TAP_HAS_LOCK_MASK) == 0);

    // Disable injection of SoC mailbox access request
    VPRINTF(LOW, "FW: Disable injection of SoC mailbox access request\n");
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_0, 0x517F);

    // Force unlock
    lsu_write_32(CLP_MBOX_CSR_MBOX_UNLOCK, MBOX_CSR_MBOX_UNLOCK_UNLOCK_MASK);

    VPRINTF(LOW, "FW: Test 2 - Send to mailbox from JTAG with TAP enabled\n");

    // Poll status until fsm is in EXECUTE UC
    VPRINTF(LOW, "FW: Wait for mailbox to enter EXECUTE_UC state\n");
    state = (lsu_read_32(CLP_MBOX_CSR_MBOX_STATUS) & MBOX_CSR_MBOX_STATUS_MBOX_FSM_PS_MASK) >> MBOX_CSR_MBOX_STATUS_MBOX_FSM_PS_LOW;
    while (state != MBOX_EXECUTE_UC) {
      state = (lsu_read_32(CLP_MBOX_CSR_MBOX_STATUS) & MBOX_CSR_MBOX_STATUS_MBOX_FSM_PS_MASK) >> MBOX_CSR_MBOX_STATUS_MBOX_FSM_PS_LOW;
    }

    // Check cmd
    VPRINTF(LOW, "FW: Checking cmd from tap\n");
    read_data = lsu_read_32(CLP_MBOX_CSR_MBOX_CMD);
    if (read_data != mbox_cmd) {
      VPRINTF(ERROR, "ERROR: mailbox cmd mismatch actual (0x%x) expected (0x%x)\n", read_data, mbox_cmd);
      SEND_STDOUT_CTRL( 0x1);
      while(1);
    }

    // Check data 
    VPRINTF(LOW, "FW: Checking %d bytes from tap\n", MBOX_DLEN_VAL);
    for (ii = 0; ii < MBOX_DLEN_VAL/4; ii++) {
        VPRINTF(HIGH, "  datain: 0x%x\n", mbox_data[ii]);
        read_data = soc_ifc_mbox_read_dataout_single();
        if (read_data != mbox_data[ii]) {
            VPRINTF(ERROR, "ERROR: mailbox data mismatch actual (0x%x) expected (0x%x)\n", read_data, mbox_data[ii]);
            SEND_STDOUT_CTRL( 0x1);
            while(1);
        };
    }

    // Write command
    lsu_write_32(CLP_MBOX_CSR_MBOX_CMD, exp_mbox_cmd);

    // Write dlen
    lsu_write_32(CLP_MBOX_CSR_MBOX_DLEN, MBOX_DLEN_VAL);

    // Write datain
    VPRINTF(LOW, "FW: Writing %d bytes to mailbox\n", MBOX_DLEN_VAL);
    for (ii = 0; ii < MBOX_DLEN_VAL/4; ii++) {
        VPRINTF(HIGH, "  datain: 0x%x\n", exp_mbox_data[ii]);
        lsu_write_32(CLP_MBOX_CSR_MBOX_DATAIN, exp_mbox_data[ii]);
    }

    status = DATA_READY;

    soc_ifc_set_mbox_status_field(status);

    VPRINTF(LOW, "FW: Test 3 - TAP disable in EXECUTE_TAP\n");

    // Poll status until fsm is in IDLE state
    VPRINTF(LOW, "FW: Wait for mailbox to enter IDLE state\n");
    do {
      state = (lsu_read_32(CLP_MBOX_CSR_MBOX_STATUS) & MBOX_CSR_MBOX_STATUS_MBOX_FSM_PS_MASK) >> MBOX_CSR_MBOX_STATUS_MBOX_FSM_PS_LOW;
    } while (state != MBOX_IDLE);

    VPRINTF(LOW, "FW: Locking mailbox\n");
    //poll for mbox lock
    while((lsu_read_32(CLP_MBOX_CSR_MBOX_LOCK) & MBOX_CSR_MBOX_LOCK_LOCK_MASK) == 1);

    VPRINTF(LOW, "FW: Writing request to mailbox\n");
    // Write command
    lsu_write_32(CLP_MBOX_CSR_MBOX_CMD,0xaface0ff);

    // Write dlen
    lsu_write_32(CLP_MBOX_CSR_MBOX_DLEN,MBOX_DLEN_VAL);

    // Write datain
    VPRINTF(LOW, "FW: Writing %d bytes to mailbox\n", MBOX_DLEN_VAL);
    for (ii = 0; ii < MBOX_DLEN_VAL/4; ii++) {
        VPRINTF(HIGH, "  datain: 0x%x\n", mbox_data[ii]);
        lsu_write_32(CLP_MBOX_CSR_MBOX_DATAIN,mbox_data[ii]);
    }

    // Set execute
    VPRINTF(LOW, "FW: Setting mailbox to execute\n");
    lsu_write_32(CLP_MBOX_CSR_MBOX_EXECUTE, MBOX_CSR_MBOX_EXECUTE_EXECUTE_MASK);

    VPRINTF(LOW, "FW: Waiting for mailbox to enter EXECUTE_TAP state\n");
    // Poll status until fsm is in EXECUTE TAP state
    do  {
        state = (lsu_read_32(CLP_MBOX_CSR_MBOX_STATUS) & MBOX_CSR_MBOX_STATUS_MBOX_FSM_PS_MASK) >> MBOX_CSR_MBOX_STATUS_MBOX_FSM_PS_LOW;
    } while (state != MBOX_EXECUTE_TAP);

    // Disable TAP_MODE to hit missing cond
    VPRINTF(LOW, "FW: Disabling TAP mode\n");
    lsu_write_32(CLP_MBOX_CSR_TAP_MODE, 0);

    // Poll status until TAP notifies it is ready for data
    VPRINTF(LOW, "FW: Waiting for mailbox to be ready for data \n");
    do  {
        status = (lsu_read_32(CLP_MBOX_CSR_MBOX_STATUS) & MBOX_CSR_MBOX_STATUS_STATUS_MASK) >> MBOX_CSR_MBOX_STATUS_STATUS_LOW;
    } while (status == CMD_BUSY);

    // Enable TAP_MODE to finish the test gracefully
    VPRINTF(LOW, "FW: Enabling TAP mode\n");
    lsu_write_32(CLP_MBOX_CSR_TAP_MODE, MBOX_CSR_TAP_MODE_ENABLED_MASK);

    // Poll status until data ready is set
    VPRINTF(LOW, "FW: Waiting for mailbox to be ready for data \n");
    do  {
        status = (lsu_read_32(CLP_MBOX_CSR_MBOX_STATUS) & MBOX_CSR_MBOX_STATUS_STATUS_MASK) >> MBOX_CSR_MBOX_STATUS_STATUS_LOW;
    } while (status != DATA_READY);

    // Check cmd
    VPRINTF(LOW, "FW: Checking CMD from TAP\n");
    read_data = lsu_read_32(CLP_MBOX_CSR_MBOX_CMD);
    if (read_data != exp_mbox_cmd) {
      VPRINTF(ERROR, "ERROR: mailbox cmd mismatch actual (0x%x) expected (0x%x)\n", read_data, exp_mbox_cmd);
      SEND_STDOUT_CTRL( 0x1);
      while(1);
    }

    // Check data
    VPRINTF(LOW, "FW: Checking %d bytes from TAP\n", MBOX_DLEN_VAL);
    for (ii = 0; ii < MBOX_DLEN_VAL/4; ii++) {
        VPRINTF(HIGH, "  datain: 0x%x\n", exp_mbox_data[ii]);
        read_data = soc_ifc_mbox_read_dataout_single();
        if (read_data != exp_mbox_data[ii]) {
            VPRINTF(ERROR, "ERROR: mailbox data mismatch actual (0x%x) expected (0x%x)\n", read_data, exp_mbox_data[ii]);
            SEND_STDOUT_CTRL( 0x1);
            while(1);
        };
    }

    // Clear execute
    lsu_write_32(CLP_MBOX_CSR_MBOX_EXECUTE, 0);

    SEND_STDOUT_CTRL(TB_CMD_TEST_PASS);
}
