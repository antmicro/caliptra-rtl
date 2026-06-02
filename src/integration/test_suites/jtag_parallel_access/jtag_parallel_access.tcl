# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
init

set script_dir [file dirname [info script]]
source [file join $script_dir common.tcl]

riscv set_mem_access sysbus progbuf abstract

echo "Accessing AES IV register"
set AES_IV_ADDR 0x10011044
set CORE_DCCM 0x50002000
set data_sample [list 0xDEADBEEF 0x12345678 0xCAFEDECA 0x31415928]

# Do repeated accessses to generate traffic
proc repeat_access_and_check {addr} {
    global data_sample
    for {set counter 0} { $counter < 4 } {incr counter} {
        echo "Loop counter : ${counter}"
        for {set sample_index 0} { $sample_index < 4} { incr sample_index} {
            set golden $data_sample
            write_memory $addr 32 $golden phys
            set actual [read_memory $addr 32 4 phys ]
            if {[compare $actual $golden] != 0} {
                shutdown error
            }
        }
    }
}

repeat_access_and_check $AES_IV_ADDR

# Trigger a FW reset update
set SOC_IFC_FW_UPDATE_RST_ADDR 0x30030624
write_memory $SOC_IFC_FW_UPDATE_RST_ADDR 32 0x1 phys

# Check value after reset, register is auto-clearing
# Hence expecting 0x0 instead of previously written 0x1
set golden 0x0
set actual [read_memory $SOC_IFC_FW_UPDATE_RST_ADDR 32 1 phys ]
if {[compare $actual $golden] != 0} {
    shutdown error
}

repeat_access_and_check $CORE_DCCM

# Success
shutdown
