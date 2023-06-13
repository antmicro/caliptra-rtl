#!/bin/bash
#
# This script runs Verilator RTL simulation and OpenOCD in background, invokes
# the supplied test command and shuts everything down.

SIM_LOG=`realpath sim.log`
OPENOCD_LOG=`realpath openocd.log`

set +e

if [ "$#" -lt 1 ]; then
    echo "Usage: gdb_test.sh <command> [args ...]"
    exit 1
fi

# Utils
source `dirname ${BASH_SOURCE[0]}`/utils.sh

terminate_all () {
    terminate ${OPENOCD_PID}
    terminate ${SIM_PID}
}

print_logs () {
    echo -e "${COLOR_WHITE}======== Simulation log ========${COLOR_OFF}"
    cat ${SIM_LOG} || true
    echo -e "${COLOR_WHITE}======== OpenOCD log ========${COLOR_OFF}"
    cat ${OPENOCD_LOG} || true
}

echo -e "${COLOR_WHITE}======== Launching interactive simulation ========${COLOR_OFF}"

# Start the simulation
echo -e "Starting simulation..."
obj_dir/Vcaliptra_top_tb >"${SIM_LOG}" 2>&1 &
SIM_PID=$!

# Wait
wait_for_phrase "${SIM_LOG}" "CLP: ROM Flow in progress..."
if [ $? -ne 0 ]; then
    echo -e "${COLOR_RED}Failed to start the simulation!${COLOR_OFF}"
    print_logs
    terminate_all; exit -1
fi
echo -e "Simulation running and ready (pid=${SIM_PID})"

# Launch OpenOCD
echo -e "Launching OpenOCD..."
cd ${CALIPTRA_ROOT}/tools/scripts/openocd && openocd -d2 -f board/caliptra-verilator.cfg >"${OPENOCD_LOG}" 2>&1 &
OPENOCD_PID=$!

# Wait
wait_for_phrase "${OPENOCD_LOG}" "Listening on port 3333 for gdb connections"
if [ $? -ne 0 ]; then
    echo -e "${COLOR_RED}Failed to start OpenOCD!${COLOR_OFF}"
    print_logs
    terminate_all; exit -1
fi
echo -e "OpenOCD running and ready (pid=${OPENOCD_PID})"

# Wait a bit
sleep 1s

# Run the test
echo -e "${COLOR_WHITE}======== Running test '$@' ========${COLOR_OFF}"

bash -c "$(printf ' %q' "$@")"
EXITCODE=$?

if [ ${EXITCODE} -eq 0 ]; then
    echo -e "${COLOR_GREEN}[PASSED]${COLOR_OFF}"
else
    echo -e "${COLOR_RED}[FAILED]${COLOR_OFF}"
fi

sleep 1s

# Terminate
echo -e "${COLOR_WHITE}Terminating...${COLOR_OFF}"
terminate_all

# Display logs
print_logs

# Honor the exitcode
exit ${EXITCODE}

