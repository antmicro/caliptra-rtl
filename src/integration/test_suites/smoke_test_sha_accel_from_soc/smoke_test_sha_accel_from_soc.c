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
#include <stdint.h>
#include <string.h>
#include "soc_access.h"
#include "soc_ifc.h"
#include "printf.h"

volatile uint32_t intr_count = 0;
volatile uint32_t *stdout = (uint32_t *)STDOUT;
#ifdef CPT_VERBOSITY
enum printf_verbosity verbosity_g = CPT_VERBOSITY;
#else
enum printf_verbosity verbosity_g = HIGH;
#endif

volatile caliptra_intr_received_s cptra_intr_rcv = {0};

#define INVALID_AXI_USER 0xBADDAD
#define FAIL(...) do { VPRINTF(ERROR, __VA_ARGS__); SEND_STDOUT_CTRL(0x1); for(;;); } while(0);

#define SHA_MODE (SHA_STREAM_512 | SHA512_ACC_CSR_MODE_ENDIAN_TOGGLE_MASK)
#define SIMPLE_SHA_DATA 0xab3443de
#define SIMPLE_SHA_EXPECTED_DIGEST_0 0x82e28097
#define SHA_DATA_LEN 128

static uint32_t SHA_DATA[SHA_DATA_LEN] = {
    0x0c21ff11, 0xb18fa697, 0x8e6c5812, 0xe4acbe0e, 0x2ace5a69, 0x2d79e7bb, 0xfc198ac4, 0x6fc7eff0,
    0xee735727, 0x4ff534dd, 0x3349b46d, 0xdb456562, 0xb8157f80, 0xe3769337, 0x06e1f215, 0xba0a1f63,
    0x409e4a4e, 0x08d498a4, 0x0035ef6a, 0xddfe9319, 0xd4a47ee2, 0x2eb7feb7, 0xd016f61d, 0x165cfb60,
    0xc5090f43, 0x2846bd6e, 0x38d5a635, 0x097ef188, 0x3fcabf7f, 0xc4d31d4b, 0x17b8db2e, 0x12dbbd9b,
    0x020360ab, 0xa629b860, 0x526a0692, 0xd5349b18, 0xfee49a3e, 0x00500715, 0x053cd482, 0x67e6bc54,
    0x7bcdae18, 0xfb32ff1b, 0x267a8098, 0x0b78733f, 0x2f542526, 0xc39d80eb, 0x1ae35ee8, 0x571fe2f7,
    0x00a6a326, 0x6729f6c7, 0x4b9f55fd, 0x2528208b, 0xa386d181, 0x1b4fbb27, 0x20c0f838, 0x7ad264b1,
    0xacbc873b, 0x10cfd07f, 0x3d3f39d0, 0x10743c04, 0xe56976e3, 0x553c626d, 0xbb7b2ed3, 0xd62268e1,
    0x691683af, 0x5351ed31, 0xfb7cf291, 0x5965eafd, 0x4ea2d251, 0x41eee6a9, 0xe86ca301, 0xcb62bfb0,
    0xc923da5d, 0x2b409908, 0x1580bec6, 0xe91ce835, 0xd29ed1b7, 0x8fdf147a, 0x258fa785, 0x070408ce,
    0xb7458ef6, 0xa1d5554d, 0xcf530fd5, 0x45476763, 0xfff235c2, 0x7ba95491, 0x07aff096, 0xb4964d17,
    0xda4833a6, 0xee811ce0, 0x97749fd7, 0xa4dc4070, 0x865f3b99, 0x323bde74, 0x955f844c, 0x5baf9938,
    0xb2025350, 0xd0581711, 0xb73578b8, 0xac7b948c, 0xf506593e, 0x0d251d9e, 0x8495bb02, 0xb4f15d6c,
    0x6bbb9260, 0xaae5f437, 0x5c5a7819, 0x90b6b2c9, 0x04bdff8e, 0x0c5e1793, 0x7e33d6a3, 0x57285f12,
    0xeeabf2e6, 0x65113cf1, 0x3c48dec6, 0x032ae037, 0x4992e164, 0x7fd20cd6, 0x311a716d, 0x0b3a971c,
    0xfa02fc06, 0x1ea47e9e, 0x108a9789, 0x6b5b2f78, 0x73a55092, 0xf9f4c6aa, 0xe3349927, 0xcd9e67eb
};

static uint32_t SHA_EXPECTED_DIGEST[] = {
    0x78c30927, 0xaf968673, 0xa466d7ff, 0xed505e6a, 0x61a6be20, 0xbe2610f9, 0xd2c945e7, 0xfaefad58,
    0xb541697e, 0x182cc58f, 0x37a43ed6, 0x8ba846c9, 0x55addabf, 0xd4e5814f, 0x4827fcd4, 0x31a3e99b
};


static void soc_release_sha_lock() {
    soc_write_32(CLP_SHA512_ACC_CSR_LOCK, SHA512_ACC_CSR_LOCK_LOCK_MASK);
}

static void soc_get_sha_lock() {
    // Make sure uC is not holding it
    lsu_write_32(CLP_SHA512_ACC_CSR_LOCK, SHA512_ACC_CSR_LOCK_LOCK_MASK);
    soc_release_sha_lock();

    for (int i = 0; i < 1000; i++)
        if (soc_read_32(CLP_SHA512_ACC_CSR_LOCK).rdata & SHA512_ACC_CSR_LOCK_LOCK_MASK)
            return;

    FAIL("Failed to acquire SHA lock from SoC after 1000 iterations!\n");
}

static void soc_await_sha_done() {
    for (int i = 0; i < 10000; i++)
        if(soc_read_32(CLP_SHA512_ACC_CSR_STATUS).rdata & SHA512_ACC_CSR_STATUS_VALID_MASK) {
            soc_write_32(CLP_SHA512_ACC_CSR_STATUS, 0);
            return;
        }
    
    FAIL("Awaiting SHA result timed out after 10000 iterations!\n");
}

static uint32_t do_soc_access(axi_req_t req) {
    axi_resp_t resp = soc_access_32(req);
    if (resp.resp)
        FAIL("SOC AXI access error for addr = 0x%x, is_write = %hhd\n", req.addr, req.write);
    return resp.rdata;
}

static void simple_sha_test() {
    soc_get_sha_lock();

    soc_write_32(CLP_SHA512_ACC_CSR_MODE, SHA_MODE);
    soc_write_32(CLP_SHA512_ACC_CSR_DLEN, 4);
    soc_write_32(CLP_SHA512_ACC_CSR_DATAIN, SIMPLE_SHA_DATA);
    soc_write_32(CLP_SHA512_ACC_CSR_EXECUTE, SHA512_ACC_CSR_EXECUTE_EXECUTE_MASK);
    
    soc_await_sha_done();
    uint32_t dig0 = 0;
    if ((dig0 = soc_read_32(CLP_SHA512_ACC_CSR_DIGEST_0).rdata) != SIMPLE_SHA_EXPECTED_DIGEST_0)
        FAIL("SHA accelerator invalid digest. Expected[0]: 0x%x, Got[0]: 0x%x\n", SIMPLE_SHA_EXPECTED_DIGEST_0, dig0);
    
    VPRINTF(LOW, "Simple SoC SHA test successful!\n");
    soc_release_sha_lock();
}

static void overflow_with_stall_sha_test() {
    soc_get_sha_lock();

    soc_write_32(CLP_SHA512_ACC_CSR_MODE, SHA_MODE);
    soc_write_32(CLP_SHA512_ACC_CSR_DLEN, SHA_DATA_LEN * 4);

    do_soc_access((axi_req_t){
        .addr = CLP_SHA512_ACC_CSR_DATAIN,
        .burst = AXI_BURST_FIXED,
        // The length is out of spec for a FIXED burst,
        // but here we just need to fill the DATAIN FIFO
        // fast enough to trigger a stall, and this enables that
        .len = SHA_DATA_LEN,
        .write = true,
        .wdata = SHA_DATA,
    });

    soc_write_32(CLP_SHA512_ACC_CSR_EXECUTE, SHA512_ACC_CSR_EXECUTE_EXECUTE_MASK);

    soc_await_sha_done();

    uint32_t digest[16] = {};
    do_soc_access((axi_req_t){
        .addr = CLP_SHA512_ACC_CSR_DIGEST_0,
        .burst = AXI_BURST_INCR,
        .len = 16,
        .write = false,
        .rdata = digest
    });

    if (memcmp(SHA_EXPECTED_DIGEST, digest, 64) != 0)
        FAIL("SHA accelerator invalid digest. Expected[0]: 0x%x, Got[0]: 0x%x\n", SHA_EXPECTED_DIGEST[0], digest[0]);

    VPRINTF(LOW, "Overflow with stall SoC SHA test successful!\n");
    soc_release_sha_lock();
}

void main(void) {
    VPRINTF(LOW, "----------------------------------------\n");
    VPRINTF(LOW, " SHA accel SoC intf access Smoke Test!!\n" );
    VPRINTF(LOW, "----------------------------------------\n");

    // (Coverage) Access SHA from SoC while not being a valid axi user
    soc_get_sha_lock();
    soc_write_user_32(CLP_SHA512_ACC_CSR_MODE, SHA_MODE, INVALID_AXI_USER);
    soc_release_sha_lock();

    // A simple test that hashes a dword over SoC intf and 
    // verifies the first dword of the digest
    simple_sha_test();

    // A test that hashes 512B of data by continuously bursting it
    // into the SHA accelerator so that its block buffer overfills
    // and a stall condition is encountered. 
    overflow_with_stall_sha_test();
}
