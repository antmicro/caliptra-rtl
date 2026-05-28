// SPDX-License-Identifier: Apache-2.0
// //
// // Licensed under the Apache License, Version 2.0 (the "License");
// // you may not use this file except in compliance with the License.
// // You may obtain a copy of the License at
// //
// // http://www.apache.org/licenses/LICENSE-2.0
// //
// // Unless required by applicable law or agreed to in writing, software
// // distributed under the License is distributed on an "AS IS" BASIS,
// // WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// // See the License for the specific language governing permissions and
// // limitations under the License.

#include "soc_access.h"
#include "caliptra_defines.h"
#include "riscv_hw_if.h"

uint8_t soc_write_32(uint32_t reg_addr, uint32_t value) {
    // Set AXI address
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_1, reg_addr);
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_0, 0x017F);
    // Set AXI write data
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_1, value);
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_0, 0x027F);
    // Issue AXI command
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_1, 1); // Write
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_0, 0x037F);

    while (1) {
        // Check if AXI has finished
        lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_0, 0x047F);
        uint32_t axi_resp = lsu_read_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_INPUT_WIRES_0);

        if(axi_resp & 1) {
            return (axi_resp >> 1) & 0b11;
        }
    }
}
