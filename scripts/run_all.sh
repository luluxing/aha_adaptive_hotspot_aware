TOTAL_RUNNING_TIME=60
NUM_WORKLOADS=2

SCAN_RATIO1=1.0
UPDATE_RATIO1=0.0
SCAN_WORKLOAD_USES_TIME_TO_RUN=1
EXEC_TIME=40
ADAPT1=1
USE_TREE_UPDATE1=0
HOT_OP_RATIO1=1.0

SCAN_RATIO2=0.0
UPDATE_RATIO2=1.0
UPDATE_WORKLOAD_USES_TIME_TO_RUN=0
EXEC_OP=2000000
ADAPT2=0
USE_TREE_UPDATE2=1
HOT_OP_RATIO2=-1.0

function run_workload {
  local dir="$1"
  local ind_name="$2"

  mkdir -p ${dir}
  
  sed -i "s|lsmt_path=.*|lsmt_path=${dir}|" options/default_${ind_name}.spec

  ../build/ycsbc/ycsb -options options/default_${ind_name}.spec -workload options/default_workload.spec -runningtime ${TOTAL_RUNNING_TIME} -usetime 1 -sequence ${NUM_WORKLOADS} i-0.0-r-0.0-s-${SCAN_RATIO1}-u-${UPDATE_RATIO1} ${SCAN_WORKLOAD_USES_TIME_TO_RUN} ${EXEC_TIME} ${USE_TREE_UPDATE1} ${ADAPT1} ${HOT_OP_RATIO1} i-0.0-r-0.0-s-${SCAN_RATIO2}-u-${UPDATE_RATIO2} ${UPDATE_WORKLOAD_USES_TIME_TO_RUN} ${EXEC_OP} ${USE_TREE_UPDATE2} ${ADAPT2} ${HOT_OP_RATIO2} 1> ${ind_name}_$(date +%F_%H_%M).out 2>&1
  
  rm -rf dir
}

run_workload "../build/wot_path_adapt" "aha"
run_workload "../build/level_path" "level"
run_workload "../build/btree_path" "btree"

