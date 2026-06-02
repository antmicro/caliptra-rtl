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

#include <stddef.h>
#include "soc_access.h"
#include "caliptra_defines.h"
#include "riscv_hw_if.h"

#define AXI_WRITE 1
#define AXI_READ 0
#define AXI_DEFAULT_MASK 0xF
#define AXI_DEFAULT_USER 0


axi_resp_t soc_access_32(axi_req_t req) {
    axi_resp_t axi_resp;
    // Set AXI address
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_1, req.addr);
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_0, 0x017F);

    // Set AXI burst
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_1, req.burst);
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_0, 0x087F);

    // Set AXI aXuser
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_1, req.axuser);
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_0, 0x077F);

    if (req.write) {
        // Clear SoC access queues
        lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_0, 0x0A7F);

        for (int i = 0; i < req.len; i++) {
            // Push AXI wdata
            lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_1, req.wdata ? req.wdata[i] : 0);
            lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_0, 0x027F);
            // Push AXI wuser
            lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_1, req.wuser ? req.wuser[i] : AXI_DEFAULT_USER);
            lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_0, 0x067F);
            // Push AXI wstrb
            lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_1, req.wstrb ? (req.wstrb[i] & 0xF) : 0xF);
            lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_0, 0x097F);
        }
    }

    uint32_t execute = (req.write ? AXI_WRITE : AXI_READ) | ((req.len - 1) << 8);
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_1, execute);
    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_0, 0x037F);

    while (1) {
        axi_resp_t axi_resp;

        // Check if AXI has finished
        lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_0, 0x047F);
        axi_resp.resp = lsu_read_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_INPUT_WIRES_0);

        if(axi_resp.resp & 1) {
            axi_resp.resp = (axi_resp.resp >> 1) & 0b11;

            if (!req.write) {
                for (int i = 0; i < req.len; i++) {
                    lsu_write_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_OUTPUT_WIRES_0, 0x057F);
                    uint32_t rdata = lsu_read_32(CLP_SOC_IFC_REG_CPTRA_GENERIC_INPUT_WIRES_0);
                    if (i == 0) axi_resp.rdata = rdata;
                    if (req.rdata) req.rdata[i] = rdata;
                }
            }
            return axi_resp;
        }
    }
}

uint8_t soc_masked_write_32(uint32_t reg_addr, uint32_t value, uint32_t mask) {
    axi_resp_t axi_resp;

    axi_resp = soc_access_32((axi_req_t){
        .addr = reg_addr,
        .axuser = AXI_DEFAULT_USER,
        .burst = AXI_BURST_INCR,
        .len = 1,
        .write = true,
        .wuser = (uint32_t[]){AXI_DEFAULT_USER},
        .wdata = (uint32_t[]){value},
        .wstrb = (uint8_t[]){mask}
    });

    return axi_resp.resp;
}

uint8_t soc_write_user_32(uint32_t reg_addr, uint32_t value, uint32_t user) {
    axi_resp_t axi_resp;

    axi_resp = soc_access_32((axi_req_t){
        .addr = reg_addr,
        .axuser = user,
        .burst = AXI_BURST_INCR,
        .len = 1,
        .write = true,
        .wuser = (uint32_t[]){user},
        .wdata = (uint32_t[]){value},
        .wstrb = (uint8_t[]){AXI_DEFAULT_MASK}
    });

    return axi_resp.resp;
}

uint8_t soc_write_32(uint32_t reg_addr, uint32_t value) {
    return soc_write_user_32(reg_addr, value, AXI_DEFAULT_USER);
}

axi_resp_t soc_read_user_32(uint32_t reg_addr, uint32_t user) {
    axi_resp_t axi_resp;

    axi_resp = soc_access_32((axi_req_t){
        .addr = reg_addr,
        .axuser = user,
        .burst = AXI_BURST_INCR,
        .len = 1
    });

    return axi_resp;
}

axi_resp_t soc_read_32(uint32_t reg_addr) {
    return soc_read_user_32(reg_addr, AXI_DEFAULT_USER);
}
