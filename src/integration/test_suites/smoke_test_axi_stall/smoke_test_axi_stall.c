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

#define FAIL(...) do { VPRINTF(ERROR, __VA_ARGS__); SEND_STDOUT_CTRL(0x1); for(;;); } while(0);

void main(void) {
    VPRINTF(LOW, "---------------------------------------\n");
    VPRINTF(LOW, " AXI Stall/Stress Test!!\n" );
    VPRINTF(LOW, "---------------------------------------\n");

    // Nothing to check, all was done on RTL/TB level
    // Look at the plusargs this test was launched with
    // At this point, we passed!
    VPRINTF(LOW, "TEST PASSED\n");
}
