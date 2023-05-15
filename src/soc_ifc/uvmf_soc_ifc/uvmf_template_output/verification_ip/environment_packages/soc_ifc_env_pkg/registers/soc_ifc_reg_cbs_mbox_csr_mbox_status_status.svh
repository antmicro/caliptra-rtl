//----------------------------------------------------------------------
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
//----------------------------------------------------------------------

// Reg predictions that will be scheduled on AHB write to mbox_unlock
class soc_ifc_reg_delay_job_mbox_csr_mbox_status_status extends soc_ifc_reg_delay_job;
    `uvm_object_utils( soc_ifc_reg_delay_job_mbox_csr_mbox_status_status )
    mbox_csr_ext rm; /* mbox_csr_rm */
    mbox_fsm_state_e state_nxt;
    uvm_reg_map map;
    virtual task do_job();
        `uvm_info("SOC_IFC_REG_DELAY_JOB", "Running delayed job for mbox_csr.mbox_status.status", UVM_HIGH)
        rm.mbox_status.mbox_fsm_ps.predict(state_nxt, .kind(UVM_PREDICT_READ), .path(UVM_PREDICT), .map(map));
    endtask
endclass

class soc_ifc_reg_cbs_mbox_csr_mbox_status_status extends soc_ifc_reg_cbs_mbox_csr;

    `uvm_object_utils(soc_ifc_reg_cbs_mbox_csr_mbox_status_status)

    // Function: post_predict
    //
    // Called by the <uvm_reg_field::predict()> method
    // after a successful UVM_PREDICT_READ or UVM_PREDICT_WRITE prediction.
    //
    // ~previous~ is the previous value in the mirror and
    // ~value~ is the latest predicted value. Any change to ~value~ will
    // modify the predicted mirror value.
    //
    virtual function void post_predict(input uvm_reg_field  fld,
                                       input uvm_reg_data_t previous,
                                       inout uvm_reg_data_t value,
                                       input uvm_predict_e  kind,
                                       input uvm_path_e     path,
                                       input uvm_reg_map    map);
        soc_ifc_reg_delay_job_mbox_csr_mbox_status_status delay_job;
        mbox_csr_ext rm; /* mbox_csr_rm */
        uvm_mem mm; /* mbox_mem_rm "mem model" */
        uvm_reg_block blk = fld.get_parent().get_parent(); /* mbox_csr_rm */
        mm = blk.get_parent().get_mem_by_name("mbox_mem_rm");
        if (!$cast(rm,blk)) `uvm_fatal ("SOC_IFC_REG_CBS", "Failed to get valid class handle")
        if (map.get_name() == this.AHB_map_name) begin
            case (kind) inside
                UVM_PREDICT_WRITE: begin
                    if (mbox_status_e'(value) != mbox_status_e'(previous) &&
                        mbox_status_e'(value) != CMD_BUSY) begin
                        if (!rm.mbox_status.soc_has_lock.get_mirrored_value()) begin
                            `uvm_warning("SOC_IFC_REG_CBS", $sformatf("Write to mbox_status in state [%p] on map [%s] is unexpected!", rm.mbox_fn_state_sigs, map.get_name()))
                        end
                        else begin
                            uvm_queue #(soc_ifc_reg_delay_job) delay_jobs;
                            // Maximum allowable size for data transferred via mailbox
                            int unsigned dlen_cap_dw = (mbox_dlen_mirrored(rm) < (mm.get_size() * mm.get_n_bytes())) ? mbox_dlen_mirrored_dword_ceil(rm) :
                                                                                                                       (mm.get_size() * mm.get_n_bytes()) >> ($clog2(MBOX_DATA_W/8));
                            if (rm.mbox_data_q.size() > 0) begin
                                `uvm_info("SOC_IFC_REG_CBS", $sformatf("Write to mbox_status transfers control back to SOC, but mbox_data_q is not empty! Size: %0d", rm.mbox_data_q.size()), UVM_LOW) /* TODO: Make a warning that can be disabled for DLEN violation cases? */
                            end
                            rm.mbox_data_q = rm.mbox_resp_q;
                            rm.mbox_resp_q.delete();
                            // On transfer of control, remove extraneous entries from data_q since
                            // reads of these values will be gated for the receiver
                            // in the DUT
                            if (rm.mbox_data_q.size > dlen_cap_dw) begin
                                `uvm_info("SOC_IFC_REG_CBS", $sformatf("Extra entries detected in mbox_data_q on control transfer - deleting %d entries", rm.mbox_data_q.size() - dlen_cap_dw), UVM_FULL)
                                while (rm.mbox_data_q.size > dlen_cap_dw) begin
                                    // Continuously remove last entry until size is equal to dlen, since entries are added with push_back
                                    rm.mbox_data_q.delete(rm.mbox_data_q.size()-1);
                                end
                            end
                            else if (rm.mbox_data_q.size < dlen_cap_dw) begin
                                uvm_reg_data_t zeros [$];
                                `uvm_info("SOC_IFC_REG_CBS", $sformatf("Insufficient entries detected in mbox_data_q on control transfer - 0-filling %d entries", dlen_cap_dw - rm.mbox_data_q.size()), UVM_FULL)
                                zeros = '{MBOX_DEPTH{32'h0}};
                                zeros = zeros[0:dlen_cap_dw - rm.mbox_data_q.size() - 1];
                                rm.mbox_data_q = {rm.mbox_data_q, zeros};
                            end
                            if (!uvm_config_db#(uvm_queue#(soc_ifc_reg_delay_job))::get(null, "soc_ifc_reg_model_top", "delay_jobs", delay_jobs))
                                `uvm_error("SOC_IFC_REG_CBS", "Failed to get handle for 'delay_jobs' queue from config database!")
                            delay_job = soc_ifc_reg_delay_job_mbox_csr_mbox_status_status::type_id::create("delay_job");
                            delay_job.rm = rm;
                            delay_job.map = map;
                            delay_job.set_delay_cycles(1);
                            delay_job.state_nxt = MBOX_EXECUTE_SOC;
                            delay_jobs.push_back(delay_job);
                            `uvm_info("SOC_IFC_REG_CBS", $sformatf("Write to mbox_status on map [%s] with value [%x] predicts a state change. Delay job is queued to update DUT model.", map.get_name(), value), UVM_HIGH)
                            rm.mbox_fn_state_sigs = '{soc_done_stage: 1'b1, default: 1'b0};
                            `uvm_info("SOC_IFC_REG_CBS", $sformatf("post_predict called through map [%p] with kind [%p] on mbox_status results in state transition. Functional state tracker: %p", map.get_name(), kind, rm.mbox_fn_state_sigs), UVM_FULL)
                        end
                    end
                end
                default: begin
                    `uvm_info("SOC_IFC_REG_CBS", $sformatf("post_predict called with kind [%p] has no effect", kind), UVM_FULL)
                end
            endcase
        end
        else if (map.get_name() == this.APB_map_name) begin
            case (kind) inside
                UVM_PREDICT_WRITE: begin
                    if (mbox_status_e'(value) != mbox_status_e'(previous) &&
                        mbox_status_e'(value) != CMD_BUSY) begin
                        if (rm.mbox_status.soc_has_lock.get_mirrored_value()) begin
                            `uvm_warning("SOC_IFC_REG_CBS", $sformatf("Write to mbox_status in state [%p] on map [%s] is unexpected!", rm.mbox_fn_state_sigs, map.get_name()))
                        end
                        else begin
                            uvm_queue #(soc_ifc_reg_delay_job) delay_jobs;
                            if (rm.mbox_data_q.size() > 0) begin
                                `uvm_info("SOC_IFC_REG_CBS", $sformatf("Write to mbox_status transfers control back to uC, but mbox_data_q is not empty! Size: %0d", rm.mbox_data_q.size()), UVM_LOW) /* TODO: Make a warning that can be disabled for DLEN violation cases? */
                            end
                            if (rm.mbox_resp_q.size() > 0) begin
                                `uvm_warning("SOC_IFC_REG_CBS", $sformatf("Write to mbox_status transfers control back to uC, but mbox_resp_q is not empty! Size: %0d", rm.mbox_resp_q.size()))
                            end
                            rm.mbox_data_q.delete();
                            rm.mbox_resp_q.delete();
                            if (!uvm_config_db#(uvm_queue#(soc_ifc_reg_delay_job))::get(null, "soc_ifc_reg_model_top", "delay_jobs", delay_jobs))
                                `uvm_error("SOC_IFC_REG_CBS", "Failed to get handle for 'delay_jobs' queue from config database!")
                            delay_job = soc_ifc_reg_delay_job_mbox_csr_mbox_status_status::type_id::create("delay_job");
                            delay_job.rm = rm;
                            delay_job.map = map;
                            delay_job.set_delay_cycles(1);
                            delay_job.state_nxt = MBOX_EXECUTE_UC;
                            delay_jobs.push_back(delay_job);
                            `uvm_info("SOC_IFC_REG_CBS", $sformatf("Write to mbox_status on map [%s] with value [%x] predicts a state change. Delay job is queued to update DUT model.", map.get_name(), value), UVM_HIGH)
                            rm.mbox_fn_state_sigs = '{uc_done_stage: 1'b1, default: 1'b0};
                            `uvm_info("SOC_IFC_REG_CBS", $sformatf("post_predict called through map [%p] with kind [%p] on mbox_status results in state transition. Functional state tracker: %p", map.get_name(), kind, rm.mbox_fn_state_sigs), UVM_FULL)
                        end
                    end
                end
                default: begin
                    `uvm_info("SOC_IFC_REG_CBS", $sformatf("post_predict called with kind [%p] has no effect", kind), UVM_FULL)
                end
            endcase
        end
        else begin
            `uvm_error("SOC_IFC_REG_CBS", "post_predict called through unsupported reg map!")
        end
    endfunction

endclass