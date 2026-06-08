# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

proc req_req_compare {got expected state} {
    set mask 2
    if { $state != 3 } {
        set mask 7
    }
    return [compare $got [expr $expected&$mask]]
}

init

set script_dir [file dirname [info script]]
source [file join $script_dir common.tcl]

puts "Read Debug Module Status Register..."
set val [riscv dmi_read $dmstatus_addr]
puts "dmstatus: $val"
while {($val & 0x00000c00) == 0} {
    wait 100
    echo "Waiting for the tap to be active!"
    set val [riscv dmi_read $dmstatus_addr]
}
puts ""

puts "Poll mailbox status..."
set status [riscv dmi_read $mbox_status_dmi_addr]
#check if in execute tap state
while {($status & 0x000001C0) != 0x00000140} {
    wait 1000
    set status [riscv dmi_read $mbox_status_dmi_addr]
}
puts ""

puts "Read mailbox cmd..."
set golden 0xdeadbeef
set actual [riscv dmi_read $dmi_reg_mbox_cmd]
if {[compare $actual $golden] != 0} {
    puts "$actual $golden"
    shutdown error
}
puts ""

puts "Read mailbox dlen..."
set golden 0
set actual [riscv dmi_read $mbox_dlen_dmi_addr]
if {[compare $actual $golden] != 0} {
    puts "$actual $golden"
    shutdown error
}
puts ""

puts "Get device lifecycle..."
set status [riscv dmi_read $dmi_reg_cptra_dbg_manuf_service_reg]
puts "Device sec state $status"

set golden5a {0x5a5a5a5a}
set goldena5 {0xa5a5a5a5}

puts "Checking the SS_DBG_MANUF_SERVICE_REG_REQ register..."
riscv dmi_write $dmi_reg_ss_dbg_manuf_service_reg_req $golden5a
set actual [riscv dmi_read $dmi_reg_ss_dbg_manuf_service_reg_req]
if {[req_req_compare $actual $golden5a $status] != 0} {
    shutdown error
}
riscv dmi_write $dmi_reg_ss_dbg_manuf_service_reg_req $goldena5
set actual [riscv dmi_read $dmi_reg_ss_dbg_manuf_service_reg_req]
if {[req_req_compare $actual $goldena5 $status] != 0} {
    shutdown error
}

puts "Checking the SS_DBG_MANUF_SERVICE_REG_RSP register..."
riscv dmi_write $dmi_reg_ss_dbg_manuf_service_reg_rsp $golden5a
set actual [riscv dmi_read $dmi_reg_ss_dbg_manuf_service_reg_rsp]
if {[compare $actual 0x200] != 0} {
    shutdown error
}
riscv dmi_write $dmi_reg_ss_dbg_manuf_service_reg_rsp $goldena5
set actual [riscv dmi_read $dmi_reg_ss_dbg_manuf_service_reg_rsp]
if {[compare $actual 0x200] != 0} {
    shutdown error
}

shutdown
