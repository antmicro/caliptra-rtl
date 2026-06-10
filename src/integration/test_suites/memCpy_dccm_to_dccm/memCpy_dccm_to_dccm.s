// SPDX-License-Identifier: Apache-2.0
// Copyright 2019 Western Digital Corporation or its affiliates.
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

// Assembly code for Hello World
// Not using only ALU ops for creating the string


#include "caliptra_defines.h"


// Code to execute
.section .text
.global _start
_start:

    // Clear minstret
    csrw minstret, zero
    csrw minstreth, zero

    // Set up MTVEC - not expecting to use it though
    li x1, RV_ICCM_SADR
    csrw mtvec, x1


    // Enable Caches in MRAC
    li x1, 0xaaaaaaaa
    csrw 0x7c0, x1

    // Call interrupt init
    call init_interrupts

    // Load string from hw_data
    // and write to DCCM

    la x3, hw_data_copy
    la x4, hw_data

loop:
    lw x5, 0(x4)
    sw x5, 0(x3)
    addi x4, x4, 4
    addi x3, x3, 4
    bnez x5, loop

    // Read the data back and compare

    la x3, hw_data_copy
    la x4, hw_data

loop2:
    lb x5, 0(x4)
    lb x1, 0(x3)
    addi x4, x4, 1
    addi x3, x3, 1
    sb x5, 0(x2)
    bne  x1, x5, fail
    bnez x5, loop2

// Write 0xff to STDOUT for TB to termiate test.
success:
    addi x5, x0, 0xff
    j _finish

fail:
    addi x5, x0, 0x01

_finish:
    li x3, STDOUT
    sb x5, 0(x3)

_halt_loop:
    beq x0, x0, _halt_loop

.section .dccm
.global stdout
stdout: .word STDOUT
.global verbosity_g
verbosity_g: .word 2
// FW polls this variable for intr
.global intr_count
intr_count: .word 0
.global cptra_intr_rcv
cptra_intr_rcv: .word 0
hw_data:
.ascii "----------------------------------\n"
.ascii "Hello World from VeeR EL2  !!\n"
.ascii "----------------------------------\n"
.word 0
.align
hw_data_copy:
.fill 256
