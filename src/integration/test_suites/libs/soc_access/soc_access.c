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

#define AXI_WRITE 1
#define AXI_READ 0
#define AXI_DEFAULT_USER 0


axi_resp_t soc_access_32(uint32_t reg_addr, uint32_t value, uint32_t user, uint8_t is_write) {
    axi_resp_t axi_resp;

    // Set AXI address
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_1, reg_addr);
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_0, 0x017F);

    // Set AXI user ID
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_1, user);
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_0, 0x067F);

    if (is_write) {
        // Set AXI write data
        lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_1, value);
        lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_0, 0x027F);
    }

    // Issue AXI command
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_1, is_write);
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_0, 0x037F);

    while (1) {
        axi_resp_t axi_resp;

        // Check if AXI has finished
        lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_0, 0x047F);
        axi_resp.resp = lsu_read_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_INPUT_WIRES_0);

        if(axi_resp.resp & 1) {
            axi_resp.resp = (axi_resp.resp >> 1) & 0b11;

            if (!is_write) {
                lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_0, 0x057F);
                axi_resp.rdata = lsu_read_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_INPUT_WIRES_0);
            }
            return axi_resp;
        }
    }
}

uint8_t soc_write_user_32(uint32_t reg_addr, uint32_t value, uint32_t user) {
    axi_resp_t axi_resp;

    axi_resp = soc_access_32(reg_addr, value, user, AXI_WRITE);

    return axi_resp.resp;
}

uint8_t soc_write_32(uint32_t reg_addr, uint32_t value) {
    return soc_write_user_32(reg_addr, value, AXI_DEFAULT_USER);
}

axi_resp_t soc_read_user_32(uint32_t reg_addr, uint32_t user) {
    axi_resp_t axi_resp;

    axi_resp = soc_access_32(reg_addr, 0, user, AXI_READ);

    return axi_resp;
}

axi_resp_t soc_read_32(uint32_t reg_addr) {
    return soc_read_user_32(reg_addr, AXI_DEFAULT_USER);
}
