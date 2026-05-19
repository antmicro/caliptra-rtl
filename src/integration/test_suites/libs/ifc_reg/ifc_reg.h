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


#ifndef IFC_LIB
#define IFC_LIB

#include <stdbool.h>
#include "stdint.h"
#include <stddef.h>

#include "ifc_reg_defs.h"
/* Global dictionary */
extern ifc_reg_exp_dict_t g_expected_data_dict;

uint32_t ifc_reg_read(uint32_t reg_addr);
void ifc_reg_write(uint32_t reg_addr, uint32_t value);

const ifc_register_info_t* find_register_by_address(uint32_t address, ifc_register_group_t *group_index, int *reg_index, ifc_register_group_t start_index);
int get_total_register_count(void);
void init_reg_exp_dict(ifc_reg_exp_dict_t *dict);
void reset_exp_reg_data(ifc_reg_exp_dict_t *dict, reset_type_t reset_type, ifc_register_group_t *groups, int num_groups);
int set_reg_exp_data(ifc_reg_exp_dict_t *dict, uint32_t address, uint32_t value, uint32_t mask, bool reg_write, ifc_register_group_t group_index_arg, bool soc_access);
int get_reg_exp_data(ifc_reg_exp_dict_t *dict, uint32_t address, uint32_t *value);
void init_mask_dict(void);
const ifc_register_info_t* get_register_info(ifc_register_group_t group, int index);
int get_register_count(ifc_register_group_t group);
uint32_t get_register_mask(uint32_t address);
const char* get_group_name(ifc_register_group_t group);
int add_mask_entry(uint32_t address, uint32_t mask);
void write_random_to_register_group_and_track(ifc_register_group_t group, ifc_reg_exp_dict_t *dict);
void write_to_register_group_and_track(ifc_register_group_t group, uint32_t write_data, ifc_reg_exp_dict_t *dict);
int read_register_group_and_verify(ifc_register_group_t group, ifc_reg_exp_dict_t *dict, bool reset, reset_type_t reset_type) ;
void read_register_group_and_track(ifc_register_group_t group, ifc_reg_exp_dict_t *dict);
static void address_to_bitmap_position(uint32_t reg_addr, uint32_t *word_index, uint32_t *bit_position);
int exclude_register(uint32_t reg_addr);
int is_register_excluded(uint32_t reg_addr);
uint32_t get_known_register_value(uint32_t reg_addr);
void init_excluded_registers(void);

/* Initialization */
void ifc_init(void);

#endif /* IFC_LIB */
