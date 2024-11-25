# Define the output file
aha_output_file=aha_original_$(date +%F_%H_%M).out
pidstat_file=pidstat_$(date +%F_%H_%M).out

numactl --cpunodebind=0 --membind=0 ../build/ycsbc/ycsb -options options/default_aha.spec -workload options/default_workload.spec -sequence 1 i-0.0-r-0.0-s-1.0-u-0.0 0 5000000 0 1 1.0 1> ${aha_output_file} 2>&1 &

CMD_PID=$!

pidstat -d 1 -p $CMD_PID > ${pidstat_file} &
PIDSTAT_PID=$!

wait $CMD_PID

kill $PIDSTAT_PID

TOTAL_WRITE_IO=$(awk '{total += $6} END {print total}' ${pidstat_file})

echo "Total Write I/O: $TOTAL_WRITE_IO KB"

du -sh ../build/wot_path_adapt/

rm -rf ../build/wot_path_adapt/
mkdir -p ../build/wot_path_adapt/


