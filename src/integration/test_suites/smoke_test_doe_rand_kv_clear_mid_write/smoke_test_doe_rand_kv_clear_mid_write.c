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
#include "printf.h"

volatile uint32_t* stdout           = (uint32_t *)STDOUT;
volatile uint32_t  intr_count = 0;
volatile uint32_t  rst_count __attribute__((section(".dccm.persistent"))) = 0;
#ifdef CPT_VERBOSITY
    enum printf_verbosity verbosity_g = CPT_VERBOSITY;
#else
    enum printf_verbosity verbosity_g = LOW;
#endif

uint32_t IV_DATA_UDS0 = 0x2eb94297;
uint32_t IV_DATA_UDS1 = 0x77285196;
uint32_t IV_DATA_UDS2 = 0x3dd39a1e;
uint32_t IV_DATA_UDS3 = 0xb95d438f;

volatile uint32_t * doe_iv_0 = (uint32_t *) CLP_DOE_REG_DOE_IV_0;
volatile uint32_t * doe_iv_1 = (uint32_t *) CLP_DOE_REG_DOE_IV_1;
volatile uint32_t * doe_iv_2 = (uint32_t *) CLP_DOE_REG_DOE_IV_2;
volatile uint32_t * doe_iv_3 = (uint32_t *) CLP_DOE_REG_DOE_IV_3;

volatile uint32_t * doe_ctrl = (uint32_t *) CLP_DOE_REG_DOE_CTRL;
volatile uint32_t * doe_status = (uint32_t *) CLP_DOE_REG_DOE_STATUS;

volatile uint32_t * soc_ifc_fw_update_reset = (uint32_t *) (CLP_SOC_IFC_REG_INTERNAL_FW_UPDATE_RESET);

volatile uint32_t * pcr_ctrl0 = (uint32_t *) CLP_PV_REG_PCR_CTRL_0;
volatile uint32_t * pcr_ctrl2 = (uint32_t *) CLP_PV_REG_PCR_CTRL_2;
volatile uint32_t * pcr_ctrl5 = (uint32_t *) CLP_PV_REG_PCR_CTRL_5;

volatile uint32_t * key_ctrl1 = (uint32_t *) CLP_KV_REG_KEY_CTRL_1;
volatile uint32_t * key_ctrl4 = (uint32_t *) CLP_KV_REG_KEY_CTRL_4;
volatile uint32_t * key_ctrl7 = (uint32_t *) CLP_KV_REG_KEY_CTRL_7;

volatile uint32_t doe_status_int;

volatile caliptra_intr_received_s cptra_intr_rcv = {0};

#define TB_CMD_COLD_RESET 0xF5
#define KV_SLOTS 24

void main() {
    VPRINTF(LOW,"---------------------------\n");
    VPRINTF(LOW," DOE Smoke Test With Rand FE and KV clear !!\n");
    VPRINTF(LOW,"---------------------------\n");
    
    VPRINTF(LOW,"Rand FE\n");

    while (rst_count < KV_SLOTS) {
        uint8_t key_entry = rst_count;
        //Clear doe_status_int
        doe_status_int = 0;

        VPRINTF(LOW,"Testing key_entry %d\n", key_entry);
        //Inject random FE
        SEND_STDOUT_CTRL(0xed);

        VPRINTF(LOW, "[TEST] Starting the write...\n");
        //Start FE and store in nth KV
        *doe_ctrl = 0x2 | (key_entry << 2); //Nth entry, FE flow;

        // Issue command to force clear keyvault, at the moment the DOE FSM tries to write data to keyvault
        lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_0, 0xC07F);

        //Poll for DOE status
        while(doe_status_int != (DOE_REG_DOE_STATUS_VALID_MASK | DOE_REG_DOE_STATUS_READY_MASK)) {
            doe_status_int = *doe_status;
            doe_status_int = doe_status_int & (DOE_REG_DOE_STATUS_VALID_MASK | DOE_REG_DOE_STATUS_READY_MASK) ;
        }

        // Send command to check for set/unset dwords in keyvault
        lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_0, (uint32_t)(0xA07F | (key_entry << 8)));

        // Why not 0xFF? Because there are several stages of "write" happening, and we only force clean one of them
        // so a partial write still occurs, and as such some dwords in KV slot are filled
        // there is low probability of even full write not filling all dwords, but it's quite unlikely
        if (lsu_read_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_INPUT_WIRES_0) == 0xFF) {
            VPRINTF(ERROR, "[FAIL] Keyvault is not partially clear!: %x\n", lsu_read_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_INPUT_WIRES_0));
            SEND_STDOUT_CTRL(0x01);
            while(1);
        }
        VPRINTF(LOW, "[PASS] KV slot %d was succesfully partially cleared...\n", key_entry);

        // Issue cold reset, and test the next key slot
        ++rst_count;
        if (rst_count < KV_SLOTS) {
            SEND_STDOUT_CTRL(TB_CMD_COLD_RESET);
        }
    }
}
