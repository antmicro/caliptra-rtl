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

#include "ifc_reg.h"
#include "caliptra_reg.h"
#include "printf.h"
#include "riscv_hw_if.h"
#include "string.h"
#include "stdint.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define ADDRESS_BITS_FOR_INDEXING 12   // 4096 possible indices
#define BITMAP_SIZE_WORDS ((1 << ADDRESS_BITS_FOR_INDEXING) / 32)

/* Global dictionary for expected register values */
ifc_reg_exp_dict_t g_expected_data_dict  __attribute__((section(".dccm.persistent")));
static register_mask_dict_t g_mask_dict  __attribute__((section(".dccm.persistent")));
// The bitmap array for register exclusions
static uint32_t excluded_registers_bitmap[BITMAP_SIZE_WORDS] __attribute__((section(".dccm.persistent")));

// Collision table to handle hash collisions - stores full register addresses
#define MAX_EXCLUDED_REGISTERS 32
static uint32_t collision_table[MAX_EXCLUDED_REGISTERS] __attribute__((section(".dccm.persistent"))) = {0};
static uint8_t collision_count __attribute__((section(".dccm.persistent"))) = 0;

/**
 * Read a 32-bit IFC register value
 *
 * @param reg_addr Register address
 * @return The register value
 */
uint32_t ifc_reg_read(uint32_t reg_addr) {
    return lsu_read_32(reg_addr);
}

/**
 * Write a 32-bit value to an IFC register
 *
 * @param reg_addr Register address
 * @param value Value to write
 */
void ifc_reg_write(uint32_t reg_addr, uint32_t value) {
    lsu_write_32(reg_addr, value);
}

// Array of register with non-zero initial values
const ifc_reg_def_value_t reg_init_values[] = {
    {CLP_SOC_IFC_REG_CPTRA_HW_REV_ID, 0x00000402},
    {CLP_SOC_IFC_REG_CPTRA_MBOX_VALID_AXI_USER_0, 0xFFFFFFFF},
    {CLP_SOC_IFC_REG_CPTRA_MBOX_VALID_AXI_USER_1, 0xFFFFFFFF},
    {CLP_SOC_IFC_REG_CPTRA_MBOX_VALID_AXI_USER_2, 0xFFFFFFFF},
    {CLP_SOC_IFC_REG_CPTRA_MBOX_VALID_AXI_USER_3, 0xFFFFFFFF},
    {CLP_SOC_IFC_REG_CPTRA_MBOX_VALID_AXI_USER_4, 0xFFFFFFFF},
    {CLP_SOC_IFC_REG_CPTRA_TRNG_VALID_AXI_USER, 0xFFFFFFFF},
    {CLP_SOC_IFC_REG_CPTRA_WDT_TIMER1_TIMEOUT_PERIOD_0, 0xFFFFFFFF},
    {CLP_SOC_IFC_REG_CPTRA_WDT_TIMER1_TIMEOUT_PERIOD_1, 0xFFFFFFFF},
    {CLP_SOC_IFC_REG_CPTRA_WDT_TIMER2_TIMEOUT_PERIOD_0, 0xFFFFFFFF},
    {CLP_SOC_IFC_REG_CPTRA_WDT_TIMER2_TIMEOUT_PERIOD_1, 0xFFFFFFFF}
};

/* Array of register infos by group */
const ifc_register_info_t register_groups[][MAX_REGISTERS_PER_GROUP] = {
    // REG_GROUP_KNOWN_VALUES
    {
        { CLP_SOC_IFC_REG_CPTRA_HW_REV_ID, "HW_REV_ID", REG_NOT_STICKY, true },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_CAPABILITIES
    {
        { CLP_SOC_IFC_REG_CPTRA_HW_CAPABILITIES, "CPTRA_HW_CAPABILITIES", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_CPTRA_FW_CAPABILITIES, "CPTRA_FW_CAPABILITIES", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_CPTRA_CAP_LOCK, "CAP_LOCK", REG_SELF_LOCK_NON_ZERO, false },
        { CLP_SOC_IFC_REG_CPTRA_FW_REV_ID_0, "FW_REV_ID_0", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_FW_REV_ID_1, "FW_REV_ID_1", REG_NOT_STICKY, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_CAPABILITIES_RO
    {
        { CLP_SOC_IFC_REG_CPTRA_HW_CONFIG, "HW_CONFIG",  REG_NOT_STICKY, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_STRAPS_RO
    {
        { CLP_SOC_IFC_REG_SS_CALIPTRA_BASE_ADDR_L, "CALIPTRA_BASE_ADDR_L", REG_EXT_LOCK_STICKY, false },
        { CLP_SOC_IFC_REG_SS_CALIPTRA_BASE_ADDR_H, "CALIPTRA_BASE_ADDR_H", REG_EXT_LOCK_STICKY, false },
        { CLP_SOC_IFC_REG_SS_MCI_BASE_ADDR_L, "MCI_BASE_ADDR_L", REG_EXT_LOCK_STICKY, false },
        { CLP_SOC_IFC_REG_SS_MCI_BASE_ADDR_H, "MCI_BASE_ADDR_H", REG_EXT_LOCK_STICKY, false },
        { CLP_SOC_IFC_REG_SS_RECOVERY_IFC_BASE_ADDR_L, "RECOVERY_IFC_BASE_ADDR_L", REG_EXT_LOCK_STICKY, false },
        { CLP_SOC_IFC_REG_SS_RECOVERY_IFC_BASE_ADDR_H, "RECOVERY_IFC_BASE_ADDR_H", REG_EXT_LOCK_STICKY, false },
        { CLP_SOC_IFC_REG_SS_OTP_FC_BASE_ADDR_L, "OTP_FC_BASE_ADDR_L", REG_EXT_LOCK_STICKY, false },
        { CLP_SOC_IFC_REG_SS_OTP_FC_BASE_ADDR_H, "OTP_FC_BASE_ADDR_L", REG_EXT_LOCK_STICKY, false },
        { CLP_SOC_IFC_REG_SS_UDS_SEED_BASE_ADDR_L, "UDS_SEED_BASE_ADDR_L", REG_EXT_LOCK_STICKY, false },
        { CLP_SOC_IFC_REG_SS_UDS_SEED_BASE_ADDR_H, "UDS_SEED_BASE_ADDR_H", REG_EXT_LOCK_STICKY, false },
        { CLP_SOC_IFC_REG_SS_PROD_DEBUG_UNLOCK_AUTH_PK_HASH_REG_BANK_OFFSET, "PROD_DEBUG_UNLOCK_AUTH_PK_HASH_REG_BANK_OFFSET", REG_EXT_LOCK_STICKY, false },
        { CLP_SOC_IFC_REG_SS_NUM_OF_PROD_DEBUG_UNLOCK_AUTH_PK_HASHES, "NUM_OF_PROD_DEBUG_UNLOCK_AUTH_PK_HASHES", REG_EXT_LOCK_STICKY, false },
        { CLP_SOC_IFC_REG_SS_DEBUG_INTENT, "DEBUG_INTENT", REG_EXT_LOCK_STICKY, false },
        { CLP_SOC_IFC_REG_SS_CALIPTRA_DMA_AXI_USER, "CALIPTRA_DMA_AXI_USER", REG_EXT_LOCK_STICKY, false },
        { CLP_SOC_IFC_REG_SS_STRAP_GENERIC_0, "STRAP_GENERIC_0", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_SS_STRAP_GENERIC_1, "STRAP_GENERIC_1", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_SS_STRAP_GENERIC_2, "STRAP_GENERIC_2", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_SS_STRAP_GENERIC_3, "STRAP_GENERIC_3", REG_EXT_LOCK, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_STATUS
    {
        { CLP_SOC_IFC_REG_CPTRA_BOOT_STATUS, "BOOT_STATUS", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_FLOW_STATUS, "FLOW_STATUS", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_DBG_MANUF_SERVICE_REG, "DBG_SERVICE", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_RSVD_REG_0, "RSVD_REG_0", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_RSVD_REG_1, "RSVD_REG_1", REG_NOT_STICKY, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_STATUS_RO
    {
        { CLP_SOC_IFC_REG_CPTRA_RESET_REASON, "RESET_REASON", REG_STICKY, false }, // bit 1 non sticky
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_SECURITY_RO
    {
        { CLP_SOC_IFC_REG_CPTRA_SECURITY_STATE, "SEC_STATE", REG_NOT_STICKY, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_ERROR_RW1C
    {
        { CLP_SOC_IFC_REG_CPTRA_HW_ERROR_FATAL, "HW_ERROR_FATAL", REG_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_HW_ERROR_NON_FATAL, "HW_ERROR_NON_FATAL", REG_STICKY, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_ERROR
    {
        { CLP_SOC_IFC_REG_CPTRA_FW_ERROR_FATAL, "FW_ERROR_FATAL", REG_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_FW_ERROR_NON_FATAL, "FW_ERROR_NON_FATAL", REG_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_HW_ERROR_ENC, "HW_ERROR_ENC", REG_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_FW_ERROR_ENC, "FW_ERROR_ENC", REG_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_FW_EXTENDED_ERROR_INFO_0, "FW_EXTENDED_ERROR_INFO_0", REG_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_FW_EXTENDED_ERROR_INFO_1, "FW_EXTENDED_ERROR_INFO_1", REG_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_FW_EXTENDED_ERROR_INFO_2, "FW_EXTENDED_ERROR_INFO_2", REG_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_FW_EXTENDED_ERROR_INFO_3, "FW_EXTENDED_ERROR_INFO_3", REG_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_FW_EXTENDED_ERROR_INFO_4, "FW_EXTENDED_ERROR_INFO_4", REG_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_FW_EXTENDED_ERROR_INFO_5, "FW_EXTENDED_ERROR_INFO_5", REG_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_FW_EXTENDED_ERROR_INFO_6, "FW_EXTENDED_ERROR_INFO_6", REG_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_FW_EXTENDED_ERROR_INFO_7, "FW_EXTENDED_ERROR_INFO_7", REG_STICKY, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_ERROR_MASK
    {
        { CLP_SOC_IFC_REG_INTERNAL_HW_ERROR_FATAL_MASK, "INTERNAL_HW_ERROR_FATAL_MASK", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_INTERNAL_HW_ERROR_NON_FATAL_MASK, "INTERNAL_HW_ERROR_NON_FATAL_MASK", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_INTERNAL_FW_ERROR_FATAL_MASK, "INTERNAL_FW_ERROR_FATAL_MASK", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_INTERNAL_FW_ERROR_NON_FATAL_MASK, "INTERNAL_FW_ERROR_NON_FATAL_MASK", REG_NOT_STICKY, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_WATCHDOG
    {
        { CLP_SOC_IFC_REG_CPTRA_WDT_TIMER1_EN, "WDT_TIMER1_EN", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_WDT_TIMER1_CTRL, "WDT_TIMER1_CTRL", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_WDT_TIMER1_TIMEOUT_PERIOD_0, "WDT_TIMER1_TIMEOUT_PERIOD_0", REG_NOT_STICKY, true },
        { CLP_SOC_IFC_REG_CPTRA_WDT_TIMER1_TIMEOUT_PERIOD_1, "WDT_TIMER1_TIMEOUT_PERIOD_1", REG_NOT_STICKY, true },
        { CLP_SOC_IFC_REG_CPTRA_WDT_TIMER2_EN, "WDT_TIMER2_EN", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_WDT_TIMER2_CTRL, "WDT_TIMER2_CTRL", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_WDT_TIMER2_TIMEOUT_PERIOD_0, "WDT_TIMER2_TIMEOUT_PERIOD_0", REG_NOT_STICKY, true },
        { CLP_SOC_IFC_REG_CPTRA_WDT_TIMER2_TIMEOUT_PERIOD_1, "WDT_TIMER2_TIMEOUT_PERIOD_1", REG_NOT_STICKY, true },
        { CLP_SOC_IFC_REG_CPTRA_WDT_CFG_0, "WDT_CFG_0", REG_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_WDT_CFG_1, "WDT_CFG_1", REG_STICKY, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_WATCHDOG_RO
    {
        { CLP_SOC_IFC_REG_CPTRA_WDT_STATUS, "WDT_STATUS", REG_NOT_STICKY, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_MCU
    {
        { CLP_SOC_IFC_REG_CPTRA_TIMER_CONFIG, "TIMER_CONFIG", REG_STICKY, false },
        { CLP_SOC_IFC_REG_INTERNAL_RV_MTIME_L, "MCU_RV_MTIME_L", REG_STICKY, false },
        { CLP_SOC_IFC_REG_INTERNAL_RV_MTIME_H, "MCU_RV_MTIME_H", REG_STICKY, false },
        { CLP_SOC_IFC_REG_INTERNAL_RV_MTIMECMP_L, "MCU_RV_MTIMECMP_L", REG_STICKY, false },
        { CLP_SOC_IFC_REG_INTERNAL_RV_MTIMECMP_H, "MCU_RV_MTIMECMP_H", REG_STICKY, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_CONTROL
    {
        { CLP_SOC_IFC_REG_INTERNAL_ICCM_LOCK, "ICCM_LOCK", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_INTERNAL_NMI_VECTOR, "NMI_VECTOR", REG_NOT_STICKY, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_CONTROL_RO
    {
        { CLP_SOC_IFC_REG_CPTRA_BOOTFSM_GO, "BOOTFSM_GO", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_CLK_GATING_EN, "CLK_GATING_EN", REG_NOT_STICKY, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_MBOX
    {
        { CLP_SOC_IFC_REG_CPTRA_MBOX_VALID_AXI_USER_0, "MBOX_VALID_AXI_USER_0", REG_EXT_LOCK, true },
        { CLP_SOC_IFC_REG_CPTRA_MBOX_VALID_AXI_USER_1, "MBOX_VALID_AXI_USER_1", REG_EXT_LOCK, true },
        { CLP_SOC_IFC_REG_CPTRA_MBOX_VALID_AXI_USER_2, "MBOX_VALID_AXI_USER_2", REG_EXT_LOCK, true },
        { CLP_SOC_IFC_REG_CPTRA_MBOX_VALID_AXI_USER_3, "MBOX_VALID_AXI_USER_3", REG_EXT_LOCK, true },
        { CLP_SOC_IFC_REG_CPTRA_MBOX_VALID_AXI_USER_4, "MBOX_VALID_AXI_USER_4", REG_EXT_LOCK, true },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_MBOX_RW1S
    {
        { CLP_SOC_IFC_REG_CPTRA_MBOX_AXI_USER_LOCK_0, "MBOX_AXI_USER_LOCK_0", REG_SELF_LOCK_NON_ZERO, false },
        { CLP_SOC_IFC_REG_CPTRA_MBOX_AXI_USER_LOCK_1, "MBOX_AXI_USER_LOCK_1", REG_SELF_LOCK_NON_ZERO, false },
        { CLP_SOC_IFC_REG_CPTRA_MBOX_AXI_USER_LOCK_2, "MBOX_AXI_USER_LOCK_2", REG_SELF_LOCK_NON_ZERO, false },
        { CLP_SOC_IFC_REG_CPTRA_MBOX_AXI_USER_LOCK_3, "MBOX_AXI_USER_LOCK_3", REG_SELF_LOCK_NON_ZERO, false },
        { CLP_SOC_IFC_REG_CPTRA_MBOX_AXI_USER_LOCK_4, "MBOX_AXI_USER_LOCK_4", REG_SELF_LOCK_NON_ZERO, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_DBG_MANUF_SERVICE
    {
        { CLP_SOC_IFC_REG_SS_DBG_MANUF_SERVICE_REG_REQ, "DBG_MANUF_SERVICE_REG_REQ", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_SS_DBG_MANUF_SERVICE_REG_RSP, "DBG_MANUF_SERVICE_REG_RSP", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_SS_SOC_DBG_UNLOCK_LEVEL_0, "SOC_DBG_UNLOCK_LEVEL_0", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_SS_SOC_DBG_UNLOCK_LEVEL_1, "SOC_DBG_UNLOCK_LEVEL_1", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_SS_GENERIC_FW_EXEC_CTRL_0, "GENERIC_FW_EXEC_CTRL_0", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_SS_GENERIC_FW_EXEC_CTRL_1, "GENERIC_FW_EXEC_CTRL_1", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_SS_GENERIC_FW_EXEC_CTRL_2, "GENERIC_FW_EXEC_CTRL_2", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_SS_GENERIC_FW_EXEC_CTRL_3, "GENERIC_FW_EXEC_CTRL_3", REG_NOT_STICKY, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_GENERIC_WIRES
    {
        { CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_0, "GENERIC_OUTPUT_WIRES_0", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_1, "GENERIC_OUTPUT_WIRES_1", REG_NOT_STICKY, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_GENERIC_WIRES_RO
    {
        { CLP_SOC_IFC_REG_CPTRA_GENERIC_INPUT_WIRES_0, "GENERIC_INPUT_WIRES_0", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_GENERIC_INPUT_WIRES_1, "GENERIC_INPUT_WIRES_1", REG_NOT_STICKY, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_FW
    {
        { CLP_SOC_IFC_REG_INTERNAL_FW_UPDATE_RESET_WAIT_CYCLES, "FW_UPDATE_RESET_WAIT_CYCLES", REG_NOT_STICKY, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_FW_PULSE_RW1S
    {
        { CLP_SOC_IFC_REG_INTERNAL_FW_UPDATE_RESET, "FW_UPDATE_RESET", REG_NOT_STICKY, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_TRNG
    {
        { CLP_SOC_IFC_REG_CPTRA_TRNG_VALID_AXI_USER, "TRNG_VALID_AXI_USER_0", REG_EXT_LOCK, true },
        { CLP_SOC_IFC_REG_CPTRA_TRNG_CTRL, "TRNG_CTRL", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_TRNG_STATUS, "TRNG_STATUS", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_ITRNG_ENTROPY_CONFIG_0, "iTRNG_CONFIG_0", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_ITRNG_ENTROPY_CONFIG_1, "iTRNG_CONFIG_1", REG_NOT_STICKY, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_TRNG_RW1S
    {
        { CLP_SOC_IFC_REG_CPTRA_TRNG_AXI_USER_LOCK, "TRNG_AXI_USER_LOCK_0", REG_SELF_LOCK_NON_ZERO, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_TRNG_RO
    {
        { CLP_SOC_IFC_REG_CPTRA_TRNG_DATA_0, "TRNG_DATA_0", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_TRNG_DATA_1, "TRNG_DATA_1", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_TRNG_DATA_2, "TRNG_DATA_2", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_TRNG_DATA_3, "TRNG_DATA_3", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_TRNG_DATA_4, "TRNG_DATA_4", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_TRNG_DATA_5, "TRNG_DATA_5", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_TRNG_DATA_6, "TRNG_DATA_6", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_TRNG_DATA_7, "TRNG_DATA_7", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_TRNG_DATA_8, "TRNG_DATA_8", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_TRNG_DATA_9, "TRNG_DATA_9", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_TRNG_DATA_10, "TRNG_DATA_10", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_CPTRA_TRNG_DATA_11, "TRNG_DATA_11", REG_NOT_STICKY, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_FUSE
    {
        { CLP_SOC_IFC_REG_CPTRA_FUSE_VALID_AXI_USER, "FUSE_VALID_AXI_USER_0", REG_EXT_LOCK, true },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_FUSE_RW1S
    {
        { CLP_SOC_IFC_REG_CPTRA_FUSE_AXI_USER_LOCK, "FUSE_AXI_USER_LOCK_0", REG_SELF_LOCK_NON_ZERO_STICKY, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_FUSE_RO
    {
        { CLP_SOC_IFC_REG_CPTRA_FUSE_WR_DONE, "FUSE_DONE", REG_STICKY, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_OWNER_PK_HASH_RO
    {
        { CLP_SOC_IFC_REG_CPTRA_OWNER_PK_HASH_0, "OWNER_PK_HASH_REG_0", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_CPTRA_OWNER_PK_HASH_1, "OWNER_PK_HASH_REG_1", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_CPTRA_OWNER_PK_HASH_2, "OWNER_PK_HASH_REG_2", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_CPTRA_OWNER_PK_HASH_3, "OWNER_PK_HASH_REG_3", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_CPTRA_OWNER_PK_HASH_4, "OWNER_PK_HASH_REG_4", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_CPTRA_OWNER_PK_HASH_5, "OWNER_PK_HASH_REG_5", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_CPTRA_OWNER_PK_HASH_6, "OWNER_PK_HASH_REG_6", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_CPTRA_OWNER_PK_HASH_7, "OWNER_PK_HASH_REG_7", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_CPTRA_OWNER_PK_HASH_8, "OWNER_PK_HASH_REG_8", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_CPTRA_OWNER_PK_HASH_9, "OWNER_PK_HASH_REG_9", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_CPTRA_OWNER_PK_HASH_10, "OWNER_PK_HASH_REG_10", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_CPTRA_OWNER_PK_HASH_11, "OWNER_PK_HASH_REG_11", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_CPTRA_OWNER_PK_HASH_LOCK, "OWNER_PK_HASH_LOCK", REG_SELF_LOCK_NON_ZERO_STICKY, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_UDS_RO
    {
        { CLP_SOC_IFC_REG_FUSE_UDS_SEED_0, "UDS_SEED_0", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_UDS_SEED_1, "UDS_SEED_1", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_UDS_SEED_2, "UDS_SEED_2", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_UDS_SEED_3, "UDS_SEED_3", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_UDS_SEED_4, "UDS_SEED_4", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_UDS_SEED_5, "UDS_SEED_5", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_UDS_SEED_6, "UDS_SEED_6", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_UDS_SEED_7, "UDS_SEED_7", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_UDS_SEED_8, "UDS_SEED_8", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_UDS_SEED_9, "UDS_SEED_9", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_UDS_SEED_10, "UDS_SEED_10", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_UDS_SEED_11, "UDS_SEED_11", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_UDS_SEED_12, "UDS_SEED_12", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_UDS_SEED_13, "UDS_SEED_13", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_UDS_SEED_14, "UDS_SEED_14", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_UDS_SEED_15, "UDS_SEED_15", REG_EXT_LOCK, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_FIELD_ENTROPY_RO
    {
        { CLP_SOC_IFC_REG_FUSE_FIELD_ENTROPY_0, "FIELD_ENTROPY_0", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_FIELD_ENTROPY_1, "FIELD_ENTROPY_1", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_FIELD_ENTROPY_2, "FIELD_ENTROPY_2", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_FIELD_ENTROPY_3, "FIELD_ENTROPY_3", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_FIELD_ENTROPY_4, "FIELD_ENTROPY_4", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_FIELD_ENTROPY_5, "FIELD_ENTROPY_5", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_FIELD_ENTROPY_6, "FIELD_ENTROPY_6", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_FIELD_ENTROPY_7, "FIELD_ENTROPY_7", REG_EXT_LOCK, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_VENDOR_PK_HASH_RO
    {
        { CLP_SOC_IFC_REG_FUSE_VENDOR_PK_HASH_0, "VENDOR_PK_HASH_0", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_VENDOR_PK_HASH_1, "VENDOR_PK_HASH_1", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_VENDOR_PK_HASH_2, "VENDOR_PK_HASH_2", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_VENDOR_PK_HASH_3, "VENDOR_PK_HASH_3", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_VENDOR_PK_HASH_4, "VENDOR_PK_HASH_4", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_VENDOR_PK_HASH_5, "VENDOR_PK_HASH_5", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_VENDOR_PK_HASH_6, "VENDOR_PK_HASH_6", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_VENDOR_PK_HASH_7, "VENDOR_PK_HASH_7", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_VENDOR_PK_HASH_8, "VENDOR_PK_HASH_8", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_VENDOR_PK_HASH_9, "VENDOR_PK_HASH_9", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_VENDOR_PK_HASH_10, "VENDOR_PK_HASH_10", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_VENDOR_PK_HASH_11, "VENDOR_PK_HASH_11", REG_EXT_LOCK, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_REVOCATION_RO
    {
        { CLP_SOC_IFC_REG_FUSE_ECC_REVOCATION, "ECC_REVOCATION", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_LMS_REVOCATION, "LMS_REVOCATION", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_MLDSA_REVOCATION, "MLDSA_REVOCATION", REG_EXT_LOCK, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_SVN_RO
    {
        { CLP_SOC_IFC_REG_FUSE_FMC_KEY_MANIFEST_SVN, "FMC_KEY_MANIFEST_SVN", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_RUNTIME_SVN_0, "RUNTIME_SVN_0", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_RUNTIME_SVN_1, "RUNTIME_SVN_1", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_RUNTIME_SVN_2, "RUNTIME_SVN_2", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_RUNTIME_SVN_3, "RUNTIME_SVN_3", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_SOC_MANIFEST_SVN_0, "SOC_MANIFEST_SVN_0", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_SOC_MANIFEST_SVN_1, "SOC_MANIFEST_SVN_1", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_SOC_MANIFEST_SVN_2, "SOC_MANIFEST_SVN_2", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_SOC_MANIFEST_SVN_3, "SOC_MANIFEST_SVN_3", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_SOC_MANIFEST_MAX_SVN, "SOC_MANIFEST_MAX_SVN", REG_EXT_LOCK, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_ANTI_ROLLBACK_RO
    {
        { CLP_SOC_IFC_REG_FUSE_ANTI_ROLLBACK_DISABLE, "ANI_ROLLBACK",  REG_EXT_LOCK, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_IDEVID_RO
    {
        { CLP_SOC_IFC_REG_FUSE_IDEVID_CERT_ATTR_0, "IDEVID_CERT_0", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_IDEVID_CERT_ATTR_1, "IDEVID_CERT_1", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_IDEVID_CERT_ATTR_2, "IDEVID_CERT_2", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_IDEVID_CERT_ATTR_3, "IDEVID_CERT_3", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_IDEVID_CERT_ATTR_4, "IDEVID_CERT_4", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_IDEVID_CERT_ATTR_5, "IDEVID_CERT_5", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_IDEVID_CERT_ATTR_6, "IDEVID_CERT_6", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_IDEVID_CERT_ATTR_7, "IDEVID_CERT_7", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_IDEVID_CERT_ATTR_8, "IDEVID_CERT_8", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_IDEVID_CERT_ATTR_9, "IDEVID_CERT_9", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_IDEVID_CERT_ATTR_10, "IDEVID_CERT_10", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_IDEVID_CERT_ATTR_11, "IDEVID_CERT_11", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_IDEVID_CERT_ATTR_12, "IDEVID_CERT_12", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_IDEVID_CERT_ATTR_13, "IDEVID_CERT_13", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_IDEVID_CERT_ATTR_14, "IDEVID_CERT_14", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_IDEVID_CERT_ATTR_15, "IDEVID_CERT_15", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_IDEVID_CERT_ATTR_16, "IDEVID_CERT_16", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_IDEVID_CERT_ATTR_17, "IDEVID_CERT_17", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_IDEVID_CERT_ATTR_18, "IDEVID_CERT_18", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_IDEVID_CERT_ATTR_19, "IDEVID_CERT_19", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_IDEVID_CERT_ATTR_20, "IDEVID_CERT_20", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_IDEVID_CERT_ATTR_21, "IDEVID_CERT_21", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_IDEVID_CERT_ATTR_22, "IDEVID_CERT_22", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_IDEVID_CERT_ATTR_23, "IDEVID_CERT_23", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_IDEVID_MANUF_HSM_ID_0, "IDEVID_HSM_ID_0", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_IDEVID_MANUF_HSM_ID_1, "IDEVID_HSM_ID_1", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_IDEVID_MANUF_HSM_ID_2, "IDEVID_HSM_ID_2", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_IDEVID_MANUF_HSM_ID_3, "IDEVID_HSM_ID_3", REG_EXT_LOCK, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_MANUF_DBG_UNLOCK_RO
    {
        { CLP_SOC_IFC_REG_FUSE_MANUF_DBG_UNLOCK_TOKEN_0, "MANUF_DBG_0", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_MANUF_DBG_UNLOCK_TOKEN_1, "MANUF_DBG_1", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_MANUF_DBG_UNLOCK_TOKEN_2, "MANUF_DBG_2", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_MANUF_DBG_UNLOCK_TOKEN_3, "MANUF_DBG_3", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_MANUF_DBG_UNLOCK_TOKEN_4, "MANUF_DBG_4", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_MANUF_DBG_UNLOCK_TOKEN_5, "MANUF_DBG_5", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_MANUF_DBG_UNLOCK_TOKEN_6, "MANUF_DBG_6", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_MANUF_DBG_UNLOCK_TOKEN_7, "MANUF_DBG_7", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_MANUF_DBG_UNLOCK_TOKEN_8, "MANUF_DBG_8", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_MANUF_DBG_UNLOCK_TOKEN_9, "MANUF_DBG_9", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_MANUF_DBG_UNLOCK_TOKEN_10, "MANUF_DBG_10", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_MANUF_DBG_UNLOCK_TOKEN_11, "MANUF_DBG_11", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_MANUF_DBG_UNLOCK_TOKEN_12, "MANUF_DBG_12", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_MANUF_DBG_UNLOCK_TOKEN_13, "MANUF_DBG_13", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_MANUF_DBG_UNLOCK_TOKEN_14, "MANUF_DBG_14", REG_EXT_LOCK, false },
        { CLP_SOC_IFC_REG_FUSE_MANUF_DBG_UNLOCK_TOKEN_15, "MANUF_DBG_15", REG_EXT_LOCK, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_SOC_STEPPING_RO
    {
        { CLP_SOC_IFC_REG_FUSE_SOC_STEPPING_ID, "SOC_STEPPING_ID", REG_EXT_LOCK, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_KEY_TYPE_RO
    {
        { CLP_SOC_IFC_REG_FUSE_PQC_KEY_TYPE, "PQC_KEY_TYPE", REG_EXT_LOCK, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_INTERRUPT_EN
    {
        { CLP_SOC_IFC_REG_INTR_BLOCK_RF_GLOBAL_INTR_EN_R, "GLOBAL_INTR_EN_R", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTR_EN_R, "ERROR_INTR_EN_R", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_INTR_BLOCK_RF_NOTIF_INTR_EN_R, "NOTIF_INTR_EN_R", REG_NOT_STICKY, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_INTERRUPT_GLOBAL_STATUS_RO
    {
        { CLP_SOC_IFC_REG_INTR_BLOCK_RF_ERROR_GLOBAL_INTR_R, "ERROR_GLOBAL_INTR_R", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_INTR_BLOCK_RF_NOTIF_GLOBAL_INTR_R, "NOTIF_GLOBAL_INTR_R", REG_NOT_STICKY, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_INTERRUPT_STATUS_RW1C
    {
        { CLP_SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_R, "ERROR_INTR_TRIG_R", REG_STICKY, false },
        { CLP_SOC_IFC_REG_INTR_BLOCK_RF_NOTIF_INTERNAL_INTR_R, "NOTIF_INTR_TRIG_R", REG_NOT_STICKY, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_INTERRUPT_TRIGGER_PULSE_RW1S
    {
        { CLP_SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTR_TRIG_R, "ERROR_INTR_TRIG_R", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_INTR_BLOCK_RF_NOTIF_INTR_TRIG_R, "NOTIF_INTR_TRIG_R", REG_NOT_STICKY, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_INTERRUPT_ERROR_COUNTERS
    {
        { CLP_SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_COUNT_R,           "ERROR_INTERNAL_INTR_COUNT_R", REG_STICKY, false },
        { CLP_SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INV_DEV_INTR_COUNT_R,            "ERROR_INV_DEV_INTR_COUNT_R", REG_STICKY, false },
        { CLP_SOC_IFC_REG_INTR_BLOCK_RF_ERROR_CMD_FAIL_INTR_COUNT_R,           "ERROR_CMD_FAIL_INTR_COUNT_R", REG_STICKY, false },
        { CLP_SOC_IFC_REG_INTR_BLOCK_RF_ERROR_BAD_FUSE_INTR_COUNT_R,           "ERROR_BAD_FUSE_INTR_COUNT_R", REG_STICKY, false },
        { CLP_SOC_IFC_REG_INTR_BLOCK_RF_ERROR_ICCM_BLOCKED_INTR_COUNT_R,       "ERROR_ICCM_BLOCKED_INTR_COUNT_R", REG_STICKY, false },
        { CLP_SOC_IFC_REG_INTR_BLOCK_RF_ERROR_MBOX_ECC_UNC_INTR_COUNT_R,       "ERROR_MBOX_ECC_UNC_INTR_COUNT_R", REG_STICKY, false },
        { CLP_SOC_IFC_REG_INTR_BLOCK_RF_ERROR_WDT_TIMER1_TIMEOUT_INTR_COUNT_R, "ERROR_WDT_TIMER1_TIMEOUT_INTR_COUNT_R", REG_STICKY, false },
        { CLP_SOC_IFC_REG_INTR_BLOCK_RF_ERROR_WDT_TIMER2_TIMEOUT_INTR_COUNT_R, "ERROR_WDT_TIMER2_TIMEOUT_INTR_COUNT_R", REG_STICKY, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    },
    // REG_GROUP_INTERRUPT_NOTIF_COUNTERS
    {
        { CLP_SOC_IFC_REG_INTR_BLOCK_RF_NOTIF_CMD_AVAIL_INTR_COUNT_R,     "NOTIF_CMD_AVAIL_INTR_COUNT_R", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_INTR_BLOCK_RF_NOTIF_MBOX_ECC_COR_INTR_COUNT_R,  "NOTIF_MBOX_ECC_COR_INTR_COUNT_R", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_INTR_BLOCK_RF_NOTIF_DEBUG_LOCKED_INTR_COUNT_R,  "NOTIF_DEBUG_LOCKED_INTR_COUNT_R", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_INTR_BLOCK_RF_NOTIF_SCAN_MODE_INTR_COUNT_R,     "NOTIF_SCAN_MODE_INTR_COUNT_R", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_INTR_BLOCK_RF_NOTIF_SOC_REQ_LOCK_INTR_COUNT_R,  "NOTIF_SOC_REQ_LOCK_INTR_COUNT_R", REG_NOT_STICKY, false },
        { CLP_SOC_IFC_REG_INTR_BLOCK_RF_NOTIF_GEN_IN_TOGGLE_INTR_COUNT_R, "NOTIF_GEN_IN_TOGGLE_INTR_COUNT_R", REG_NOT_STICKY, false },
        { 0, NULL, REG_NOT_STICKY, false }  // End marker
    }
};

/* Function to get a string representation of a register group */
const char* get_group_name(ifc_register_group_t group) {
    switch(group) {
        // EXAMPLE
        // case REG_GROUP_KNOWN_VALUES: return "Hardcoded Values";
        // TODO: Fill with ifc regs groups
        case REG_GROUP_KNOWN_VALUES: return "Hardcoded Values";
        case REG_GROUP_CAPABILITIES: return "Capabilities";
        case REG_GROUP_CAPABILITIES_RO: return "Capabilities-RO";
        case REG_GROUP_STRAPS_RO: return "Caliptra Straps RO";
        case REG_GROUP_STATUS: return "Status";
        case REG_GROUP_STATUS_RO: return "Status-RO";
        case REG_GROUP_SECURITY_RO: return "Security-RO";
        case REG_GROUP_ERROR_RW1C: return "Ftl/Non-Ftl err W1C";
        case REG_GROUP_ERROR: return "Ftl/Non-Ftl Err";
        case REG_GROUP_ERROR_MASK: return "Err Mask";
        case REG_GROUP_WATCHDOG: return "Watchdog";
        case REG_GROUP_WATCHDOG_RO: return "Watchdog-RO";
        case REG_GROUP_MCU: return "MCU";
        case REG_GROUP_CONTROL: return "Control";
        case REG_GROUP_CONTROL_RO: return "Control RO (Tap Access RW in Debug Mode)";
        case REG_GROUP_MBOX: return "IFC Mailbox AXI User";
        case REG_GROUP_MBOX_RW1S: return "IFC Mailbox AXI User Lock";
        case REG_GROUP_DBG_MANUF_SERVICE: return "Debug Services";
        case REG_GROUP_GENERIC_WIRES: return "Generic Wires";
        case REG_GROUP_GENERIC_WIRES_RO: return "Generic Wires - RO";
        case REG_GROUP_FW: return "FW Reset Cycles";
        case REG_GROUP_FW_PULSE_RW1S: return "FW Reset";
        case REG_GROUP_TRNG: return "TRNG control registers";
        case REG_GROUP_TRNG_RW1S: return "TRNG AXI User Lock";
        case REG_GROUP_TRNG_RO: return "TRNG Input data";
        case REG_GROUP_FUSE: return "Fuse AXI User";
        case REG_GROUP_FUSE_RW1S: return "Fuse AXI User Lock";
        case REG_GROUP_FUSE_RO: return "Fuse Status - RO";
        case REG_GROUP_OWNER_PK_HASH_RO: return "Caliptra Owner PK Hash - RO";
        case REG_GROUP_UDS_RO: return "Unique Device Secret - RO";
        case REG_GROUP_FIELD_ENTROPY_RO: return "Field Entropy - RO";
        case REG_GROUP_VENDOR_PK_HASH_RO: return "Vendor PK Hash - RO";
        case REG_GROUP_ECC_REVOCATION_RO: return "ECC Revocation - RO";
        case REG_GROUP_SVN_RO: return "Security Version Number - RO";
        case REG_GROUP_ANTI_ROLLBACK_RO: return "Anti Rollback - RO";
        case REG_GROUP_IDEVID_RO: return "IDevID - RO";
        case REG_GROUP_MANUF_DBG_UNLOCK_RO: return "Manufacturing Debug Unlock Token - RO";
        case REG_GROUP_SOC_STEPPING_RO: return " SoC Stepping - RO";
        case REG_GROUP_KEY_TYPE_RO: return "PQC Key Type - RO";
        case REG_GROUP_INTERRUPT_EN: return "Intrpt Enable";
        case REG_GROUP_INTERRUPT_GLOBAL_STATUS_RO: return "Intrpt Global Status RO";
        case REG_GROUP_INTERRUPT_STATUS_RW1C: return "Intrpt Status W1C";
        case REG_GROUP_INTERRUPT_TRIGGER_PULSE_RW1S: return "Intrpt Trigger Pulse W1S";
        case REG_GROUP_INTERRUPT_ERROR_COUNTERS: return "Err Intrpt Counters";
        case REG_GROUP_INTERRUPT_NOTIF_COUNTERS: return "Notif Intrpt Counters";
        default: return "Unknown";
    }
}

/* Get the number of registers in a group */
int get_register_count(ifc_register_group_t group) {
    int count = 0;

    if (group >= REG_GROUP_COUNT) {
        return 0;
    }

    while (register_groups[group][count].address != 0) {
        count++;
    }

    return count;
}

/* Get register information by its index within a group */
const ifc_register_info_t* get_register_info(ifc_register_group_t group, int index) {
    if (group >= REG_GROUP_COUNT) {
        return NULL;
    }

    if (index < 0 || index >= MAX_REGISTERS_PER_GROUP) {
        return NULL;
    }

    if (register_groups[group][index].address == 0) {
        return NULL;
    }

    return &register_groups[group][index];
}

/**
 * Function to find a register by address (across all groups)
 *
 * @param address Register address
 * @param group_index Pointer to store group index
 * @param reg_index Pointer to store register index
 * @return Register info pointer, or NULL if not found
 */
const ifc_register_info_t* find_register_by_address(uint32_t address,
                                                   ifc_register_group_t *group_index,
                                                   int *reg_index,
                                                   ifc_register_group_t start_index) {
    for (int group = start_index; group < REG_GROUP_COUNT; group++) {
        int count = get_register_count((ifc_register_group_t)group);

        for (int i = 0; i < count; i++) {
            const ifc_register_info_t *reg = get_register_info((ifc_register_group_t)group, i);

            if (reg && reg->address == address) {
                if (group_index) *group_index = (ifc_register_group_t)group;
                if (reg_index) *reg_index = i;
                return reg;
            }
        }
    }

    return NULL;

}

/**
 * Function to calculate the total number of registers across all groups
 *
 * @return Total number of registers
 */
int get_total_register_count(void) {
    int total = 0;
    for (int group = 0; group < REG_GROUP_COUNT; group++) {
        total += get_register_count((ifc_register_group_t)group);
    }
    return total;
}

/**
 * Initialize the register expected data dictionary
 *
 * @param dict Pointer to dictionary to initialize
 */
void init_reg_exp_dict(ifc_reg_exp_dict_t *dict) {
    VPRINTF(LOW, "Initializing expected data dict\n");
    dict->count = 0;

    // Add new entry if space available
    for (ifc_register_group_t group = 0; group < REG_GROUP_COUNT; group++) {
        int count = get_register_count(group);

        VPRINTF(MEDIUM, "Initializing %d registers in group %s\n", count, get_group_name(group) );

        if (group == REG_GROUP_KNOWN_VALUES) {
            for (int i = 0; i < count; i++) {
                const ifc_register_info_t *reg = get_register_info(group, i);
                VPRINTF(MEDIUM, "Init reg 0x%08x\n", reg->address);
                uint32_t known_value = get_known_register_value(reg->address);
                dict->entries[dict->count].address = reg->address;
                dict->entries[dict->count].expected_data = known_value;
                dict->count++;
            }
        } else {
            for (int i = 0; i < count; i++) {
                const ifc_register_info_t *reg = get_register_info(group, i);
                VPRINTF(MEDIUM, "Init reg 0x%08x\n", reg->address);
                dict->entries[dict->count].address = reg->address;
                dict->entries[dict->count].expected_data = 0;
                dict->count++;
            }
        }
    }
}

/**
 * Add or update an entry in the register expected data dictionary
 *
 * @param dict Pointer to dictionary
 * @param address Register address (key)
 * @param name Register name
 * @param value Expected data value
 * @param mask Mask to apply to the value
 * @return 0 on success, -1 if dictionary is full
 */
 int set_reg_exp_data(ifc_reg_exp_dict_t *dict, uint32_t address, uint32_t value, uint32_t mask, bool reg_write, ifc_register_group_t group_index_arg) {

    VPRINTF(MEDIUM, "UPDATE REG [0x%0x] with Value = 0x%0x, Mask = 0x%0x\n", address, value, mask);

    ifc_register_group_t group_index;
    int reg_index;
    const ifc_register_info_t *reg_info;
    const ifc_register_info_t *intr_glb_sts_reg;
    const ifc_register_info_t *intr_sts_reg;
    const ifc_register_info_t *axi_user_lock_reg;
    const ifc_register_info_t *cap_lock_reg;
    uint32_t glb_sts_mask;
    uint32_t intr_sts_mask;
    uint32_t err_data;
    uint32_t read_intr_sts;
    uint32_t axi_user_lock;
    uint32_t cap_lock_reg_value;
    bool ext_allowed = false;
    bool reset_reason_reg = false;
    bool update_exp_data = false;

    reg_info = find_register_by_address(address, &group_index, &reg_index, group_index_arg);
    VPRINTF(MEDIUM, "Register Name = %s\n", reg_info->name);

    if (group_index == REG_GROUP_ERROR_RW1C || group_index == REG_GROUP_INTERRUPT_STATUS_RW1C) {
        err_data = ifc_reg_read(address);
        if (reg_write) {
            value = err_data & ~value;
        }
        VPRINTF(MEDIUM, "Read current register data = 0x%0x\n", err_data);
    }

    if (group_index == REG_GROUP_INTERRUPT_TRIGGER_PULSE_RW1S) {
        intr_sts_reg = get_register_info(REG_GROUP_INTERRUPT_STATUS_RW1C, reg_index);
        read_intr_sts = ifc_reg_read(intr_sts_reg->address) | (value & mask);
        intr_sts_mask = get_register_mask(intr_sts_reg->address);

        if (reg_index == 0) {
            intr_glb_sts_reg = get_register_info(REG_GROUP_INTERRUPT_GLOBAL_STATUS_RO, 0);
            glb_sts_mask = SOC_IFC_REG_INTR_BLOCK_RF_ERROR_GLOBAL_INTR_R_AGG_STS_MASK;
        } else if (reg_index == 1) {
            intr_glb_sts_reg = get_register_info(REG_GROUP_INTERRUPT_GLOBAL_STATUS_RO, 1);
            glb_sts_mask = SOC_IFC_REG_INTR_BLOCK_RF_NOTIF_GLOBAL_INTR_R_AGG_STS_MASK;
        }
    }

    if (group_index == REG_GROUP_MBOX) {
        axi_user_lock_reg = get_register_info(REG_GROUP_MBOX_RW1S, reg_index);
        axi_user_lock = ifc_reg_read(axi_user_lock_reg->address);
        if (axi_user_lock == 0) {
            ext_allowed = true;
        }
    }

    if (group_index == REG_GROUP_TRNG && reg_index == 0) {
        axi_user_lock_reg = get_register_info(REG_GROUP_TRNG_RW1S, reg_index);
        axi_user_lock = ifc_reg_read(axi_user_lock_reg->address);
        if (axi_user_lock == 0) {
            ext_allowed = true;
        }
    }

    if (group_index == REG_GROUP_FUSE) {
        axi_user_lock_reg = get_register_info(REG_GROUP_FUSE_RW1S, reg_index);
        axi_user_lock = ifc_reg_read(axi_user_lock_reg->address);
        if (axi_user_lock == 0) {
            ext_allowed = true;
        }
    }

    // Special case for capabilities registers - override the above conditions
    if (group_index == REG_GROUP_CAPABILITIES && reg_index <= 2) {
        cap_lock_reg = get_register_info(REG_GROUP_CAPABILITIES, 2);
        cap_lock_reg_value = ifc_reg_read(cap_lock_reg->address);
        ext_allowed = (cap_lock_reg_value == 0);
        VPRINTF(MEDIUM, "Capabilities REG %d, cap lock = %d, update_exp_data = %d\n", reg_index, cap_lock_reg_value, update_exp_data);
    }

    if (group_index == REG_GROUP_DBG_MANUF_SERVICE) {
        if (reg_index == 1) {
            const ifc_register_info_t *debug_intent_reg = get_register_info(REG_GROUP_STRAPS_RO, 12);
            const ifc_register_info_t *sec_state_reg = get_register_info(REG_GROUP_SECURITY_RO, 0);
            const ifc_register_info_t *debug_rsp_reg = get_register_info(REG_GROUP_DBG_MANUF_SERVICE, 1);
            value |= (
                ifc_reg_read(debug_rsp_reg->address) & (
                    SOC_IFC_REG_SS_DBG_MANUF_SERVICE_REG_RSP_MANUF_DBG_UNLOCK_SUCCESS_MASK |
                    SOC_IFC_REG_SS_DBG_MANUF_SERVICE_REG_RSP_PROD_DBG_UNLOCK_SUCCESS_MASK
                )
            );
            if (ifc_reg_read(debug_intent_reg->address) & SOC_IFC_REG_SS_DEBUG_INTENT_DEBUG_INTENT_MASK) {
                if ((ifc_reg_read(sec_state_reg->address) & SOC_IFC_REG_CPTRA_SECURITY_STATE_DEVICE_LIFECYCLE_MASK) == 1) {
                    mask |= 0x7;
                } else if ((ifc_reg_read(sec_state_reg->address) & SOC_IFC_REG_CPTRA_SECURITY_STATE_DEVICE_LIFECYCLE_MASK) == 3) {
                    mask |= 0x38;
                }
            }
            ext_allowed = true;
        }
    }

    // Standard update condition
    if ((reg_info->is_sticky == REG_SELF_LOCK_NON_ZERO && ifc_reg_read(address) == 0) ||
        (reg_info->is_sticky == REG_SELF_LOCK_NON_ZERO_STICKY && ifc_reg_read(address) == 0) ||
        (reg_info->is_sticky != REG_CONFIG_DONE_STICKY && reg_info->is_sticky != REG_CONFIG_DONE &&
         reg_info->is_sticky != REG_SELF_LOCK_NON_ZERO_STICKY && reg_info->is_sticky != REG_EXT_LOCK &&
         reg_info->is_sticky != REG_SELF_LOCK_NON_ZERO) ||
        ext_allowed) {
        update_exp_data = true;
    }

    bool pulse_timer_reg = (address == CLP_SOC_IFC_REG_CPTRA_WDT_TIMER1_CTRL || address == CLP_SOC_IFC_REG_CPTRA_WDT_TIMER2_CTRL);
    bool pulse_intr_reg = (group_index == REG_GROUP_INTERRUPT_TRIGGER_PULSE_RW1S);

    // First check if entry already exists
    for (int i = 0; i < dict->count; i++) {
        if (dict->entries[i].address == address) {
            VPRINTF(MEDIUM, "Entry exists!\n");
            // Update existing entry's expected data only if sticky bit is NOT set
            if (update_exp_data) {
                if (!pulse_timer_reg && !pulse_intr_reg) {
                    VPRINTF(MEDIUM, "Not pulse reg, value = 0x%0x\n", value & mask);
                    dict->entries[i].expected_data = value & mask;
                } else if (pulse_timer_reg) {
                    VPRINTF(MEDIUM, "Pulse Timer reg, value = 0x0\n");
                    dict->entries[i].expected_data = 0x0;
                } else {
                    VPRINTF(MEDIUM, "Pulse Interrupt reg, value = 0x0\n");
                    dict->entries[i].expected_data = 0x0;
                    // Update expected data for corresponding interrupt status register
                    VPRINTF(MEDIUM, "Recursively Updating exp_data for %s [ 0x%0x ] = 0x%0x (read_intr_sts)\n", intr_sts_reg->name, intr_sts_reg->address, read_intr_sts);
                    set_reg_exp_data(dict, intr_sts_reg->address, read_intr_sts, intr_sts_mask, false, 0);
                    // Update global interrupt status register
                    set_reg_exp_data(dict, intr_glb_sts_reg->address, (1U << (glb_sts_mask - 1)), glb_sts_mask, false, 0);
                }
            }
            // If sticky bit is set, retain previous expected value (do nothing)

            return 0; // Return after handling existing entry
        }
    }

    // Add new entry if space available
    if (dict->count < MAX_REGISTER_ENTRIES) {
        dict->entries[dict->count].address = address;
        if (!pulse_timer_reg && !pulse_intr_reg) {
            VPRINTF(MEDIUM, "Not pulse reg, value = 0x%0x\n", value & mask);
            dict->entries[dict->count].expected_data = value & mask;
        } else if (pulse_timer_reg) {
            VPRINTF(MEDIUM, "Pulse reg, value = 0x0\n");
            dict->entries[dict->count].expected_data = 0x0;
        }
        else {
            VPRINTF(MEDIUM, "Pulse Interrupt reg, value = 0x0\n");
            dict->entries[dict->count].expected_data = 0x0;
            // Update expected data for corresponding interrupt status register
            set_reg_exp_data(dict, intr_sts_reg->address, read_intr_sts, intr_sts_mask, false, 0);
            // Update global interrupt status register
            set_reg_exp_data(dict, intr_glb_sts_reg->address, (1U << (glb_sts_mask - 1)), glb_sts_mask, false, 0);
        }
        dict->count++;
        return 0;
    }

    return -1; // Dictionary full
}

/**
 * Get expected data for a register
 *
 * @param dict Pointer to dictionary
 * @param address Register address to lookup
 * @param value Pointer to store expected value
 * @return 0 if found, -1 if not found
 */
int get_reg_exp_data(ifc_reg_exp_dict_t *dict, uint32_t address, uint32_t *value) {
    for (int i = 0; i < dict->count; i++) {
        if (dict->entries[i].address == address) {
            if (value) {
                *value = dict->entries[i].expected_data;
            }
            return 0;
        }
    }

    return -1; // Not found
}

/**
 * Get known value for a register with REG_ACCESS_KNOWN type
 */
uint32_t get_known_register_value(uint32_t reg_addr) {
    switch (reg_addr) {
        case CLP_SOC_IFC_REG_CPTRA_HW_REV_ID:
            return 0x00000402;  // Caliptra Version 2.0.4

        default:
            return 0x00000000;
    }
}

/**
 * Convert a register address to a bitmap index and bit position
 *
 * @param reg_addr Register address
 * @param word_index Pointer to store the word index in the bitmap
 * @param bit_position Pointer to store the bit position in the word
 */
static void address_to_bitmap_position(uint32_t reg_addr, uint32_t *word_index, uint32_t *bit_position) {
    // Use the lower bits of the address as a simple hash
    // This works because register addresses are typically aligned and spaced apart
    uint32_t hash_value = reg_addr & ((1 << ADDRESS_BITS_FOR_INDEXING) - 1);

    *word_index = hash_value / 32;
    *bit_position = hash_value % 32;
}

/**
 * Exclude a register by its address with collision handling
 *
 * @param reg_addr Register address to exclude
 * @return 0 on success, -1 if collision table is full
 */
int exclude_register(uint32_t reg_addr) {
    uint32_t word_index, bit_position;

    // Compute position in bitmap
    address_to_bitmap_position(reg_addr, &word_index, &bit_position);

    // Set the bit in the bitmap
    excluded_registers_bitmap[word_index] |= (1UL << bit_position);

    // Add to collision table
    if (collision_count < MAX_EXCLUDED_REGISTERS) {
        collision_table[collision_count++] = reg_addr;
        return 0;
    }

    // Collision table is full
    printf("WARNING: Collision table full, cannot add register 0x%08x\n", reg_addr);
    return -1;
}

/**
 * Check if a register is excluded with collision handling
 *
 * @param reg_addr Register address to check
 * @return 1 if excluded, 0 otherwise
 */
int is_register_excluded(uint32_t reg_addr) {
    uint32_t word_index, bit_position;

    // Compute position in bitmap
    address_to_bitmap_position(reg_addr, &word_index, &bit_position);

    // First, check the bit in the bitmap
    if (excluded_registers_bitmap[word_index] & (1UL << bit_position)) {
        // Potential match, verify in collision table to handle hash collisions
        for (int i = 0; i < collision_count; i++) {
            if (collision_table[i] == reg_addr) {
                return 1;  // Confirmed match in collision table
            }
        }
    }

    return 0;  // Not excluded
}

/**
 * Initialize the excluded registers bitmap
 */
void init_excluded_registers(void) {

    VPRINTF(LOW, "Initialize excluded registers\n");
    // Clear the bitmap and collision table
    memset(excluded_registers_bitmap, 0, sizeof(excluded_registers_bitmap));
    memset(collision_table, 0, sizeof(collision_table));
    collision_count = 0;
}


void write_random_to_register_group_and_track(ifc_register_group_t group, ifc_reg_exp_dict_t *dict) {
    int count = get_register_count(group);
    VPRINTF(LOW, "Writing random values to all %s registers (%d total):\n", get_group_name(group), count);

    bool ro_reg = false;
    if (group == REG_GROUP_KNOWN_VALUES ||
        group == REG_GROUP_CAPABILITIES_RO ||
        group == REG_GROUP_STRAPS_RO ||
        group == REG_GROUP_STATUS_RO ||
        group == REG_GROUP_SECURITY_RO ||
        group == REG_GROUP_WATCHDOG_RO ||
        group == REG_GROUP_GENERIC_WIRES_RO ||
        group == REG_GROUP_TRNG_RO ||
        group == REG_GROUP_FUSE_RO ||
        group == REG_GROUP_OWNER_PK_HASH_RO ||
        group == REG_GROUP_UDS_RO ||
        group == REG_GROUP_FIELD_ENTROPY_RO ||
        group == REG_GROUP_VENDOR_PK_HASH_RO ||
        group == REG_GROUP_ECC_REVOCATION_RO ||
        group == REG_GROUP_SVN_RO ||
        group == REG_GROUP_ANTI_ROLLBACK_RO ||
        group == REG_GROUP_IDEVID_RO ||
        group == REG_GROUP_MANUF_DBG_UNLOCK_RO ||
        group == REG_GROUP_SOC_STEPPING_RO ||
        group == REG_GROUP_KEY_TYPE_RO ||
        group == REG_GROUP_INTERRUPT_GLOBAL_STATUS_RO) {
            ro_reg = true;
        }

    for (int i = 0; i < count; i++) {
        const ifc_register_info_t *reg = get_register_info(group, i);

        if (reg) {
            // Check if this register should be excluded using our efficient method
            if (!is_register_excluded(reg->address)) {
                // Generate a unique value for each register
                uint32_t rand_value = rand();

                /* Get mask for this register */
                uint32_t mask = get_register_mask(reg->address);

                // Store in dictionary
                if (!ro_reg) {
                    if (set_reg_exp_data(dict, reg->address, rand_value, mask, true, group) != 0) {
                        VPRINTF(MEDIUM, "  WARNING: Could not store expected data for %s\n", reg->name);
                    }
                }

                VPRINTF(MEDIUM, "  Writing 0x%08x to %s (0x%08x)\n", rand_value, reg->name, reg->address);
                ifc_reg_write(reg->address, rand_value);
            } else {
                VPRINTF(MEDIUM, "  Skipping excluded register %s (0x%08x)\n", reg->name, reg->address);
            }
        }
    }
}

void write_to_register_group_and_track(ifc_register_group_t group, uint32_t write_data, ifc_reg_exp_dict_t *dict) {
    int count = get_register_count(group);
    VPRINTF(LOW, "Writing fixed value to all %s registers (%d total):\n", get_group_name(group), count);

    for (int i = 0; i < count; i++) {
        const ifc_register_info_t *reg = get_register_info(group, i);

        if (reg) {
            // Check if this register should be excluded using our efficient method
            if (!is_register_excluded(reg->address)) {

                /* Get mask for this register */
                uint32_t mask = get_register_mask(reg->address);

                // Store in dictionary
                if (set_reg_exp_data(dict, reg->address, write_data, mask, true, group) != 0) {
                    VPRINTF(MEDIUM, "  WARNING: Could not store expected data for %s\n", reg->name);
                }

                VPRINTF(MEDIUM, "  Writing 0x%08x to %s (0x%08x)\n", write_data, reg->name, reg->address);
                ifc_reg_write(reg->address, write_data);
            } else {
                VPRINTF(MEDIUM, "  Skipping excluded register %s (0x%08x)\n", reg->name, reg->address);
            }
        }
    }
}

/**
 * Function to read all registers in a group and verify their values against expected data
 *
 * @param group Register group
 * @param dict Dictionary containing expected register values
 * @return Number of registers that failed verification
 */
int read_register_group_and_verify(ifc_register_group_t group, ifc_reg_exp_dict_t *dict, bool reset, reset_type_t reset_type) {
    uint32_t read_data;
    int count = get_register_count(group);
    int mismatch_count = 0;
    uint32_t exp_data;
    uint32_t read_intr0_sts;
    uint32_t read_intr1_sts;
    uint32_t read_intr0_en;
    uint32_t read_intr1_en;
    const ifc_register_info_t *intr0_reg;
    const ifc_register_info_t *intr1_reg;
    const ifc_register_info_t *intr0_en_reg;
    const ifc_register_info_t *intr1_en_reg;

    bool ro_reg = false;
    if (group == REG_GROUP_KNOWN_VALUES ||
        group == REG_GROUP_CAPABILITIES_RO ||
        group == REG_GROUP_STRAPS_RO ||
        group == REG_GROUP_STATUS_RO ||
        group == REG_GROUP_SECURITY_RO ||
        group == REG_GROUP_WATCHDOG_RO ||
        group == REG_GROUP_GENERIC_WIRES_RO ||
        group == REG_GROUP_TRNG_RO ||
        group == REG_GROUP_FUSE_RO ||
        group == REG_GROUP_OWNER_PK_HASH_RO ||
        group == REG_GROUP_UDS_RO ||
        group == REG_GROUP_FIELD_ENTROPY_RO ||
        group == REG_GROUP_VENDOR_PK_HASH_RO ||
        group == REG_GROUP_ECC_REVOCATION_RO ||
        group == REG_GROUP_SVN_RO ||
        group == REG_GROUP_ANTI_ROLLBACK_RO ||
        group == REG_GROUP_IDEVID_RO ||
        group == REG_GROUP_MANUF_DBG_UNLOCK_RO ||
        group == REG_GROUP_SOC_STEPPING_RO ||
        group == REG_GROUP_KEY_TYPE_RO ||
        group == REG_GROUP_INTERRUPT_GLOBAL_STATUS_RO) {
            ro_reg = true;
        }

    VPRINTF(LOW, "Reading and verifying %s registers (%d total):\n", get_group_name(group), count);

    for (int i = 0; i < count; i++) {
        const ifc_register_info_t *reg = get_register_info(group, i);

        if (reg) {
            // Skip excluded registers with collision-aware check
            if (is_register_excluded(reg->address)) {
                VPRINTF(MEDIUM, "  Skipping excluded register %s (0x%08x)\n", reg->name, reg->address);
                continue;
            }
            VPRINTF(MEDIUM,"Reg %s [0x%0x]\n", reg->name, reg->address);

            // Read the register value
            read_data = ifc_reg_read(reg->address);

            // Get expected data from dictionary
            if (reset) {
                if (reset_type == COLD_RESET) {
                    if (reg->has_init_value == false && ro_reg == false) {
                        exp_data  = 0;
                        if (reg->address == CLP_SOC_IFC_REG_INTERNAL_RV_MTIME_H || reg->address == CLP_SOC_IFC_REG_INTERNAL_RV_MTIME_L && read_data >= exp_data) {
                            VPRINTF(MEDIUM, "  Expect reg increment: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n", reg->name, reg->address, read_data, exp_data);
                        } else if (read_data == exp_data) {
                            VPRINTF(MEDIUM,"  Match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n", reg->name, reg->address, read_data, exp_data);
                        } else if (reg->address == CLP_SOC_IFC_REG_INTR_BLOCK_RF_NOTIF_INTERNAL_INTR_R) {
                            if (ifc_reg_read(CLP_SOC_IFC_REG_CPTRA_SECURITY_STATE) & SOC_IFC_REG_CPTRA_SECURITY_STATE_DEBUG_LOCKED_MASK) {
                                exp_data |= SOC_IFC_REG_INTR_BLOCK_RF_NOTIF_INTERNAL_INTR_R_NOTIF_DEBUG_LOCKED_STS_MASK;
                            }
                            if (ifc_reg_read(CLP_SOC_IFC_REG_CPTRA_SECURITY_STATE) & SOC_IFC_REG_CPTRA_SECURITY_STATE_SCAN_MODE_MASK) {
                                exp_data |= SOC_IFC_REG_INTR_BLOCK_RF_NOTIF_INTERNAL_INTR_R_NOTIF_SCAN_MODE_STS_MASK;
                            }
                            if (read_data == exp_data) {
                                VPRINTF(MEDIUM,"  Match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n", reg->name, reg->address, read_data, exp_data);
                            } else {
                                VPRINTF(LOW, "  No match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n", reg->name, reg->address, read_data, exp_data);
                                mismatch_count++;
                            }
                        } else if (reg->address == CLP_SOC_IFC_REG_CPTRA_FLOW_STATUS) {
                            exp_data |= 4 << 25; //uC out of reset, so BOOTFSM is in DONE state
                            if (read_data == exp_data) {
                                VPRINTF(MEDIUM,"  Match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n", reg->name, reg->address, read_data, exp_data);
                            } else {
                                VPRINTF(LOW, "  No match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n", reg->name, reg->address, read_data, exp_data);
                                mismatch_count++;
                            }
                        } else {
                            VPRINTF(LOW, "  No match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n", reg->name, reg->address, read_data, exp_data);
                            mismatch_count++;
                        }
                    } else if (reg->has_init_value == false && ro_reg == true) {
                        if (get_reg_exp_data(&g_expected_data_dict, reg->address, &exp_data) == 0) {
                            if (read_data == exp_data) {
                                VPRINTF(MEDIUM,"  Match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n",
                                       reg->name, reg->address, read_data, exp_data);
                            } else if (group == REG_GROUP_INTERRUPT_GLOBAL_STATUS_RO) {
                                exp_data = 0;
                                if (i == 0) {
                                    // ERROR global status = ERROR status & ERROR enable
                                    intr0_reg = get_register_info(REG_GROUP_INTERRUPT_STATUS_RW1C, 0);
                                    intr0_en_reg = get_register_info(REG_GROUP_INTERRUPT_EN, 1); // ERROR_INTR_EN_R

                                    read_intr0_sts = ifc_reg_read(intr0_reg->address);
                                    read_intr0_en = ifc_reg_read(intr0_en_reg->address);

                                    // Global status bit is set only if (status & enable) != 0
                                    if ((read_intr0_sts & read_intr0_en) != 0) {
                                        exp_data |= SOC_IFC_REG_INTR_BLOCK_RF_ERROR_GLOBAL_INTR_R_AGG_STS_MASK;
                                    }
                                } else {
                                    // NOTIF global status = NOTIF status & NOTIF enable
                                    intr0_reg = get_register_info(REG_GROUP_INTERRUPT_STATUS_RW1C, 1);
                                    intr0_en_reg = get_register_info(REG_GROUP_INTERRUPT_EN, 2); // NOTIF_INTR_EN_R

                                    read_intr0_sts = ifc_reg_read(intr0_reg->address);
                                    read_intr0_en = ifc_reg_read(intr0_en_reg->address);

                                    // Global status bit is set only if (status & enable) != 0
                                    if ((read_intr0_sts & read_intr0_en) != 0) {
                                        exp_data |= SOC_IFC_REG_INTR_BLOCK_RF_NOTIF_GLOBAL_INTR_R_AGG_STS_MASK;
                                    }
                                }
                                if (read_data == exp_data) {
                                    VPRINTF(MEDIUM,"  Match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n", reg->name, reg->address, read_data, exp_data);
                                }
                                else {
                                    VPRINTF(LOW, "  No match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n", reg->name, reg->address, read_data, exp_data);
                                    mismatch_count++;
                                }
                            } else {
                                VPRINTF(LOW, "  No match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n",
                                       reg->name, reg->address, read_data, exp_data);
                                mismatch_count++;
                            }
                        } else {
                            VPRINTF(LOW, "  ! %s (0x%08x): Read 0x%08x, No expected data in dictionary\n",
                                   reg->name, reg->address, read_data);
                        }
                    } else {
                        size_t init_array_size = sizeof(reg_init_values) / sizeof(reg_init_values[0]);
                        for (size_t i = 0; i < init_array_size; i++) {
                            if (reg_init_values[i].address == reg->address) {
                                exp_data = reg_init_values[i].default_value;
                                break;
                            }
                        }
                        if (read_data == exp_data) {
                            VPRINTF(MEDIUM,"  Match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n", reg->name, reg->address, read_data, exp_data);
                        } else {
                            VPRINTF(LOW, "  No match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n", reg->name, reg->address, read_data, exp_data);
                            mismatch_count++;
                        }
                    }
                } else if (reset_type == WARM_RESET) {
                    if (reg->is_sticky == REG_STICKY || reg->is_sticky == REG_CONFIG_DONE_STICKY || reg->is_sticky == REG_SELF_LOCK_NON_ZERO_STICKY) {
                        if (get_reg_exp_data(&g_expected_data_dict, reg->address, &exp_data) == 0) {
                            VPRINTF(MEDIUM, "Expected data for %s = 0x%0x\n", reg->name, exp_data);
                            // Compare and report
                            if (group == REG_GROUP_INTERRUPT_ERROR_COUNTERS) {
                                if ((i == 0 && (ifc_reg_read(CLP_SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_R) & SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_R_ERROR_INTERNAL_STS_MASK)) ||
                                    (i == 1 && (ifc_reg_read(CLP_SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_R) & SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_R_ERROR_INV_DEV_STS_MASK)) ||
                                    (i == 2 && (ifc_reg_read(CLP_SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_R) & SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_R_ERROR_CMD_FAIL_STS_MASK)) ||
                                    (i == 3 && (ifc_reg_read(CLP_SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_R) & SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_R_ERROR_BAD_FUSE_STS_MASK)) ||
                                    (i == 4 && (ifc_reg_read(CLP_SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_R) & SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_R_ERROR_ICCM_BLOCKED_STS_MASK)) ||
                                    (i == 5 && (ifc_reg_read(CLP_SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_R) & SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_R_ERROR_MBOX_ECC_UNC_STS_MASK)) ||
                                    (i == 6 && (ifc_reg_read(CLP_SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_R) & SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_R_ERROR_WDT_TIMER1_TIMEOUT_STS_MASK)) ||
                                    (i == 7 && (ifc_reg_read(CLP_SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_R) & SOC_IFC_REG_INTR_BLOCK_RF_ERROR_INTERNAL_INTR_R_ERROR_WDT_TIMER2_TIMEOUT_STS_MASK))) {
                                    if (read_data >= exp_data) {
                                        VPRINTF(MEDIUM,"  Match: %s (0x%08x): Read 0x%08x > Expected 0x%08x\n", reg->name, reg->address, read_data, exp_data);
                                    } else {
                                        VPRINTF(LOW, "  No match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n", reg->name, reg->address, read_data, exp_data);
                                        mismatch_count++;
                                    }
                                }
                            } else if (reg->address == CLP_SOC_IFC_REG_CPTRA_RESET_REASON) {
                                exp_data = exp_data | SOC_IFC_REG_CPTRA_RESET_REASON_WARM_RESET_MASK; // bit 1 is not sticky
                                if (read_data == exp_data) {
                                    VPRINTF(MEDIUM,"  Match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n",reg->name, reg->address, read_data, exp_data);
                                } else {
                                    VPRINTF(LOW, "  No match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n", reg->name, reg->address, read_data, exp_data);
                                    mismatch_count++;
                                }
                            } else if (read_data == exp_data) {
                                VPRINTF(MEDIUM,"  Match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n",
                                       reg->name, reg->address, read_data, exp_data);
                            } else if (read_data > exp_data && reg->address == CLP_SOC_IFC_REG_INTERNAL_RV_MTIME_H || reg->address == CLP_SOC_IFC_REG_INTERNAL_RV_MTIME_L) {
                                VPRINTF(MEDIUM, "  Expect reg increment: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n",
                                        reg->name, reg->address, read_data, exp_data);
                            } else {
                                VPRINTF(LOW, "  No match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n",
                                       reg->name, reg->address, read_data, exp_data);
                                mismatch_count++;
                            }
                        } else {
                            VPRINTF(LOW, "  ! %s (0x%08x): Read 0x%08x, No expected data in dictionary\n",
                                   reg->name, reg->address, read_data);
                        }
                    } else {
                        if (reg->has_init_value == false && ro_reg == false) {
                            exp_data  = 0;

                            if (read_data == exp_data) {
                                VPRINTF(MEDIUM,"  Match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n", reg->name, reg->address, read_data, exp_data);
                            } else if (group == REG_GROUP_INTERRUPT_NOTIF_COUNTERS) {
                                if ((i == 0 &&  (ifc_reg_read(CLP_SOC_IFC_REG_INTR_BLOCK_RF_NOTIF_INTERNAL_INTR_R) & SOC_IFC_REG_INTR_BLOCK_RF_NOTIF_INTERNAL_INTR_R_NOTIF_CMD_AVAIL_STS_MASK)) ||
                                    (i == 1 &&  (ifc_reg_read(CLP_SOC_IFC_REG_INTR_BLOCK_RF_NOTIF_INTERNAL_INTR_R) & SOC_IFC_REG_INTR_BLOCK_RF_NOTIF_INTERNAL_INTR_R_NOTIF_MBOX_ECC_COR_STS_MASK)) ||
                                    (i == 2 &&  (ifc_reg_read(CLP_SOC_IFC_REG_INTR_BLOCK_RF_NOTIF_INTERNAL_INTR_R) & SOC_IFC_REG_INTR_BLOCK_RF_NOTIF_INTERNAL_INTR_R_NOTIF_DEBUG_LOCKED_STS_MASK)) ||
                                    (i == 3 &&  (ifc_reg_read(CLP_SOC_IFC_REG_INTR_BLOCK_RF_NOTIF_INTERNAL_INTR_R) & SOC_IFC_REG_INTR_BLOCK_RF_NOTIF_INTERNAL_INTR_R_NOTIF_SCAN_MODE_STS_MASK)) ||
                                    (i == 4 &&  (ifc_reg_read(CLP_SOC_IFC_REG_INTR_BLOCK_RF_NOTIF_INTERNAL_INTR_R) & SOC_IFC_REG_INTR_BLOCK_RF_NOTIF_INTERNAL_INTR_R_NOTIF_SOC_REQ_LOCK_STS_MASK)) ||
                                    (i == 5 &&  (ifc_reg_read(CLP_SOC_IFC_REG_INTR_BLOCK_RF_NOTIF_INTERNAL_INTR_R) & SOC_IFC_REG_INTR_BLOCK_RF_NOTIF_INTERNAL_INTR_R_NOTIF_GEN_IN_TOGGLE_STS_MASK))) {
                                    if (read_data >= exp_data) {
                                        VPRINTF(MEDIUM,"  Match: %s (0x%08x): Read 0x%08x > Expected 0x%08x\n", reg->name, reg->address, read_data, exp_data);
                                    } else {
                                        VPRINTF(LOW, "  No match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n", reg->name, reg->address, read_data, exp_data);
                                    mismatch_count++;
                                    }
                                }
                            } else if (reg->address == CLP_SOC_IFC_REG_CPTRA_FLOW_STATUS) {
                                exp_data |= 4 << 25; //uC out of reset, so BOOTFSM is in DONE state
                                if (read_data == exp_data) {
                                    VPRINTF(MEDIUM,"  Match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n", reg->name, reg->address, read_data, exp_data);
                                } else {
                                    VPRINTF(LOW, "  No match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n", reg->name, reg->address, read_data, exp_data);
                                    mismatch_count++;
                                }
                            } else {
                                VPRINTF(LOW, "  No match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n", reg->name, reg->address, read_data, exp_data);
                                mismatch_count++;
                            }
                        } else if (reg->has_init_value == false && ro_reg == true) {
                            if (get_reg_exp_data(&g_expected_data_dict, reg->address, &exp_data) == 0) {
                                if (read_data == exp_data) {
                                    VPRINTF(MEDIUM,"  Match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n",
                                           reg->name, reg->address, read_data, exp_data);
                                } else if (group == REG_GROUP_INTERRUPT_GLOBAL_STATUS_RO) {
                                    exp_data = 0;
                                    if (i == 0) {
                                        // ERROR global status = ERROR status & ERROR enable
                                        intr0_reg = get_register_info(REG_GROUP_INTERRUPT_STATUS_RW1C, 0);
                                        intr0_en_reg = get_register_info(REG_GROUP_INTERRUPT_EN, 1); // ERROR_INTR_EN_R

                                        read_intr0_sts = ifc_reg_read(intr0_reg->address);
                                        read_intr0_en = ifc_reg_read(intr0_en_reg->address);

                                        // Global status bit is set only if (status & enable) != 0
                                        if ((read_intr0_sts & read_intr0_en) != 0) {
                                            exp_data |= SOC_IFC_REG_INTR_BLOCK_RF_ERROR_GLOBAL_INTR_R_AGG_STS_MASK;
                                        }
                                    } else {
                                        // NOTIF global status = NOTIF status & NOTIF enable
                                        intr0_reg = get_register_info(REG_GROUP_INTERRUPT_STATUS_RW1C, 1);
                                        intr0_en_reg = get_register_info(REG_GROUP_INTERRUPT_EN, 2); // NOTIF_INTR_EN_R

                                        read_intr0_sts = ifc_reg_read(intr0_reg->address);
                                        read_intr0_en = ifc_reg_read(intr0_en_reg->address);

                                        // Global status bit is set only if (status & enable) != 0
                                        if ((read_intr0_sts & read_intr0_en) != 0) {
                                            exp_data |= SOC_IFC_REG_INTR_BLOCK_RF_NOTIF_GLOBAL_INTR_R_AGG_STS_MASK;
                                        }
                                    }
                                    if (read_data == exp_data) {
                                        VPRINTF(MEDIUM,"  Match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n", reg->name, reg->address, read_data, exp_data);
                                    }
                                    else {
                                        VPRINTF(LOW, "  No match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n", reg->name, reg->address, read_data, exp_data);
                                        mismatch_count++;
                                    }
                                } else {
                                    VPRINTF(LOW, "  No match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n",
                                           reg->name, reg->address, read_data, exp_data);
                                    mismatch_count++;
                                }
                            } else {
                                VPRINTF(LOW, "  ! %s (0x%08x): Read 0x%08x, No expected data in dictionary\n",
                                       reg->name, reg->address, read_data);
                            }
                        } else {
                            size_t init_array_size = sizeof(reg_init_values) / sizeof(reg_init_values[0]);
                            for (size_t i = 0; i < init_array_size; i++) {
                                if (reg_init_values[i].address == reg->address) {
                                    exp_data = reg_init_values[i].default_value;
                                    break;
                                }
                            }
                            if (read_data == exp_data) {
                                VPRINTF(MEDIUM,"  Match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n", reg->name, reg->address, read_data, exp_data);
                            } else {
                                VPRINTF(LOW, "  No match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n", reg->name, reg->address, read_data, exp_data);
                                mismatch_count++;
                            }
                        }
                    }
                }
            } else { // Verifying after a write operation
                if (get_reg_exp_data(&g_expected_data_dict, reg->address, &exp_data) == 0) {
                    // Compare and report
                    if (read_data == exp_data) {
                        VPRINTF(MEDIUM,"  Match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n",
                            reg->name, reg->address, read_data, exp_data);
                    } else if (read_data > exp_data && reg->address == CLP_SOC_IFC_REG_INTERNAL_RV_MTIME_H || reg->address == CLP_SOC_IFC_REG_INTERNAL_RV_MTIME_L) {
                        VPRINTF(MEDIUM, "  Expect reg increment: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n",
                                reg->name, reg->address, read_data, exp_data);
                    } else if (reg->address == CLP_SOC_IFC_REG_CPTRA_FLOW_STATUS) {
                        exp_data |= 4 << 25; //uC out of reset, so BOOTFSM is in DONE state
                        if (read_data == exp_data) {
                            VPRINTF(MEDIUM,"  Match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n", reg->name, reg->address, read_data, exp_data);
                        } else {
                            VPRINTF(LOW, "  No match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n", reg->name, reg->address, read_data, exp_data);
                            mismatch_count++;
                        }
                    } else if (group == REG_GROUP_INTERRUPT_GLOBAL_STATUS_RO) {
                        exp_data = 0;
                        if (i == 0) {
                            // ERROR global status = ERROR status & ERROR enable
                            intr0_reg = get_register_info(REG_GROUP_INTERRUPT_STATUS_RW1C, 0);
                            intr0_en_reg = get_register_info(REG_GROUP_INTERRUPT_EN, 1); // ERROR_INTR_EN_R

                            read_intr0_sts = ifc_reg_read(intr0_reg->address);
                            read_intr0_en = ifc_reg_read(intr0_en_reg->address);

                            // Global status bit is set only if (status & enable) != 0
                            if ((read_intr0_sts & read_intr0_en) != 0) {
                                exp_data |= SOC_IFC_REG_INTR_BLOCK_RF_ERROR_GLOBAL_INTR_R_AGG_STS_MASK;
                            }
                        } else {
                            // NOTIF global status = NOTIF status & NOTIF enable
                            intr0_reg = get_register_info(REG_GROUP_INTERRUPT_STATUS_RW1C, 1);
                            intr0_en_reg = get_register_info(REG_GROUP_INTERRUPT_EN, 2); // NOTIF_INTR_EN_R

                            read_intr0_sts = ifc_reg_read(intr0_reg->address);
                            read_intr0_en = ifc_reg_read(intr0_en_reg->address);

                            // Global status bit is set only if (status & enable) != 0
                            if ((read_intr0_sts & read_intr0_en) != 0) {
                                exp_data |= SOC_IFC_REG_INTR_BLOCK_RF_NOTIF_GLOBAL_INTR_R_AGG_STS_MASK;
                            }
                        }
                        if (read_data == exp_data) {
                            VPRINTF(MEDIUM,"  Match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n", reg->name, reg->address, read_data, exp_data);
                        }
                        else {
                            VPRINTF(LOW, "  No match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n", reg->name, reg->address, read_data, exp_data);
                            mismatch_count++;
                        }
                    } else {
                        VPRINTF(LOW, "  No match: %s (0x%08x): Read 0x%08x, Expected 0x%08x\n",
                            reg->name, reg->address, read_data, exp_data);
                        mismatch_count++;
                    }
                } else {
                    VPRINTF(LOW, "  ! %s (0x%08x): Read 0x%08x, No expected data in dictionary\n",
                        reg->name, reg->address, read_data);
                }
            }
        } else {
            VPRINTF(LOW, "  ! Register index %d not found in group\n", i);
        }
    }

    VPRINTF(LOW, "Verification complete: %d register(s) matched, %d register(s) mismatched\n",
           count - mismatch_count, mismatch_count);

    return mismatch_count;
}

/**
 * Function to read all registers in a group and track their values in a dictionary
 *
 * @param group Register group
 * @param dict Dictionary to store register values
 */
void read_register_group_and_track(ifc_register_group_t group, ifc_reg_exp_dict_t *dict) {
    uint32_t read_data;
    int count = get_register_count(group);

    VPRINTF(LOW, "Reading and tracking %s registers (%d total):\n", get_group_name(group), count);

    for (int i = 0; i < count; i++) {
        const ifc_register_info_t *reg = get_register_info(group, i);

        if (reg) {
            // Check if this register should be excluded
            if (!is_register_excluded(reg->address)) {
                // Read the register value
                read_data = ifc_reg_read(reg->address);

                VPRINTF(MEDIUM, "  Reading 0x%08x from %s (0x%08x)\n", read_data, reg->name, reg->address);

                /* Get mask for this register */
                uint32_t mask = get_register_mask(reg->address);

                // Store in dictionary
                if (set_reg_exp_data(dict, reg->address, read_data, mask, false, group) != 0) {
                    VPRINTF(LOW, "  WARNING: Could not store read data for %s\n", reg->name);
                }
            } else {
                VPRINTF(MEDIUM, "  Skipping excluded register %s (0x%08x)\n", reg->name, reg->address);
            }
        }
    }

    VPRINTF(LOW, "Register tracking complete: %d register(s) read and tracked\n", count);
}


void init_mask_dict(void) {
    VPRINTF(LOW, "Initializing mask dict\n");
    g_mask_dict.count = 0;

    // TODO: fill with correct registers
    add_mask_entry(CLP_SOC_IFC_REG_CPTRA_CAP_LOCK,
            SOC_IFC_REG_CPTRA_CAP_LOCK_LOCK_MASK);
    add_mask_entry(CLP_SOC_IFC_REG_CPTRA_FLOW_STATUS,
            SOC_IFC_REG_CPTRA_FLOW_STATUS_MAILBOX_FLOW_DONE_MASK |
            SOC_IFC_REG_CPTRA_FLOW_STATUS_READY_FOR_RUNTIME_MASK |
            SOC_IFC_REG_CPTRA_FLOW_STATUS_READY_FOR_MB_PROCESSING_MASK |
            SOC_IFC_REG_CPTRA_FLOW_STATUS_IDEVID_CSR_READY_MASK |
            SOC_IFC_REG_CPTRA_FLOW_STATUS_STATUS_MASK);
    add_mask_entry(CLP_SOC_IFC_REG_CPTRA_WDT_TIMER1_EN,
            SOC_IFC_REG_CPTRA_WDT_TIMER1_EN_TIMER1_EN_MASK);
    add_mask_entry(CLP_SOC_IFC_REG_CPTRA_WDT_TIMER2_EN,
            SOC_IFC_REG_CPTRA_WDT_TIMER2_EN_TIMER2_EN_MASK);
    add_mask_entry(CLP_SOC_IFC_REG_INTERNAL_ICCM_LOCK,
            SOC_IFC_REG_INTERNAL_ICCM_LOCK_LOCK_MASK);
    add_mask_entry(CLP_SOC_IFC_REG_CPTRA_MBOX_AXI_USER_LOCK_0,
            SOC_IFC_REG_CPTRA_MBOX_AXI_USER_LOCK_0_LOCK_MASK);
    add_mask_entry(CLP_SOC_IFC_REG_CPTRA_MBOX_AXI_USER_LOCK_1,
            SOC_IFC_REG_CPTRA_MBOX_AXI_USER_LOCK_1_LOCK_MASK);
    add_mask_entry(CLP_SOC_IFC_REG_CPTRA_MBOX_AXI_USER_LOCK_2,
            SOC_IFC_REG_CPTRA_MBOX_AXI_USER_LOCK_2_LOCK_MASK);
    add_mask_entry(CLP_SOC_IFC_REG_CPTRA_MBOX_AXI_USER_LOCK_3,
            SOC_IFC_REG_CPTRA_MBOX_AXI_USER_LOCK_3_LOCK_MASK);
    add_mask_entry(CLP_SOC_IFC_REG_CPTRA_MBOX_AXI_USER_LOCK_4,
            SOC_IFC_REG_CPTRA_MBOX_AXI_USER_LOCK_4_LOCK_MASK);
    add_mask_entry(CLP_SOC_IFC_REG_CPTRA_FLOW_STATUS,
            SOC_IFC_REG_CPTRA_FLOW_STATUS_STATUS_MASK |
            SOC_IFC_REG_CPTRA_FLOW_STATUS_IDEVID_CSR_READY_MASK |
            SOC_IFC_REG_CPTRA_FLOW_STATUS_READY_FOR_MB_PROCESSING_MASK |
            SOC_IFC_REG_CPTRA_FLOW_STATUS_READY_FOR_RUNTIME_MASK |
            SOC_IFC_REG_CPTRA_FLOW_STATUS_MAILBOX_FLOW_DONE_MASK);
    add_mask_entry(CLP_SOC_IFC_REG_CPTRA_TRNG_STATUS,
            SOC_IFC_REG_CPTRA_TRNG_STATUS_DATA_REQ_MASK);
    add_mask_entry(CLP_SOC_IFC_REG_SS_DBG_MANUF_SERVICE_REG_RSP,
            SOC_IFC_REG_SS_DBG_MANUF_SERVICE_REG_RSP_UDS_PROGRAM_SUCCESS_MASK |
            SOC_IFC_REG_SS_DBG_MANUF_SERVICE_REG_RSP_UDS_PROGRAM_FAIL_MASK |
            SOC_IFC_REG_SS_DBG_MANUF_SERVICE_REG_RSP_UDS_PROGRAM_IN_PROGRESS_MASK |
            SOC_IFC_REG_SS_DBG_MANUF_SERVICE_REG_RSP_TAP_MAILBOX_AVAILABLE_MASK);
}

/**
 * Add an entry to the register mask dictionary
 *
 * @param address Register address
 * @param mask Combined mask for the register
 * @return 0 on success, -1 if dictionary is full
 */
int add_mask_entry(uint32_t address, uint32_t mask) {
    if (g_mask_dict.count < MAX_REGISTER_ENTRIES) {
        g_mask_dict.entries[g_mask_dict.count].address = address;
        g_mask_dict.entries[g_mask_dict.count].combined_mask = mask;
        g_mask_dict.count++;
        return 0;
    }
    return -1;
}

/**
 * Get the combined mask for a register
 *
 * @param address Register address
 * @return Combined mask, or 0xFFFFFFFF if not found
 */
uint32_t get_register_mask(uint32_t address) {
    for (int i = 0; i < g_mask_dict.count; i++) {
        if (g_mask_dict.entries[i].address == address) {
            return g_mask_dict.entries[i].combined_mask;
        }
    }

    /* Default mask for unknown registers */
    return 0xFFFFFFFF;
}

/**
 * Initialize IFC module
 */
void ifc_init(void) {
    // Initialize register mask dictionary if not already done

    VPRINTF(LOW, "Initializing mask dict and expected data dict\n");
    static int masks_initialized = 0;
    if (!masks_initialized) {
        init_mask_dict();
        masks_initialized = 1;
    }

    // Initialize expected data dictionary
    init_reg_exp_dict(&g_expected_data_dict);

    // Initialize excluded registers
    init_excluded_registers();

    // Perform other IFC initialization
    VPRINTF(LOW, "IFC module initialized\n");

}
