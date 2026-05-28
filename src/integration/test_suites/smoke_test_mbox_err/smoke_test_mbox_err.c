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
#include "riscv_hw_if.h"
#include "soc_access.h"
#include "soc_ifc.h"
#include <stdint.h>
#include "printf.h"
#include "caliptra_isr.h"

volatile uint32_t* stdout           = (uint32_t *)STDOUT;
volatile uint32_t  intr_count       = 0;
#ifdef CPT_VERBOSITY
    enum printf_verbosity             verbosity_g = CPT_VERBOSITY;
#else
    enum printf_verbosity             verbosity_g = LOW;
#endif

#define MBOX_VALID_USER 0xFFFFFFFF

volatile caliptra_intr_received_s cptra_intr_rcv = {0};

void main () {
    axi_resp_t axi_resp;

    // Message
    VPRINTF(LOW, "----------------------------------------------\n");
    VPRINTF(LOW, " Caliptra Mailbox Error/Notif Smoke Test!!\n"    );
    VPRINTF(LOW, "----------------------------------------------\n");

    // Poll for mbox lock from SoC
    do {
        axi_resp = soc_read_user_32(CLP_MBOX_CSR_MBOX_LOCK, MBOX_VALID_USER);
    } while ((axi_resp.rdata & MBOX_CSR_MBOX_LOCK_LOCK_MASK) == 1);

    // Write mailbox command, data and length
    soc_write_user_32(CLP_MBOX_CSR_MBOX_CMD,    0x0,        MBOX_VALID_USER);
    soc_write_user_32(CLP_MBOX_CSR_MBOX_DLEN,   0x1,        MBOX_VALID_USER);
    soc_write_user_32(CLP_MBOX_CSR_MBOX_DATAIN, 0x0FACE0FF, MBOX_VALID_USER);

    // Set mailbox execute for uC
    soc_write_user_32(CLP_MBOX_CSR_MBOX_EXECUTE, 1, MBOX_VALID_USER);

    // Try to write to DATAIN from SoC which is not allowed after execute from SoC
    soc_write_user_32(CLP_MBOX_CSR_MBOX_DATAIN, 0x0FACE0FF, MBOX_VALID_USER);

    if (cptra_intr_rcv.soc_ifc_error & SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_R_ERROR_INTERNAL_STS_MASK == 0) {
        VPRINTF(ERROR, "ERROR: Mailbox did not report error on protocol violation!\n");
        SEND_STDOUT_CTRL(0x1);
        while(1);
    }

    // Force unlock
    lsu_write_32(CLP_MBOX_CSR_MBOX_UNLOCK, MBOX_CSR_MBOX_UNLOCK_UNLOCK_MASK);

    // Poll for mbox lock
    while((lsu_read_32(CLP_MBOX_CSR_MBOX_LOCK) & MBOX_CSR_MBOX_LOCK_LOCK_MASK) == 1);

    // Poll for mbox lock from SoC to trigger a notification interrupt
    soc_read_user_32(CLP_MBOX_CSR_MBOX_LOCK, MBOX_VALID_USER);

    if (cptra_intr_rcv.soc_ifc_notif & SOC_IFC_REG_INTR_BLOCK_RF_NOTIF_INTERNAL_INTR_R_NOTIF_SOC_REQ_LOCK_STS_MASK == 0) {
        VPRINTF(ERROR, "ERROR: Mailbox did not notify about SoC trying to lock!\n");
        SEND_STDOUT_CTRL(0x1);
        while(1);
    }
}
