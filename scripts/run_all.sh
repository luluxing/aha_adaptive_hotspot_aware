NUM_WORKLOADS=1

SCAN_RATIO1=1.0
UPDATE_RATIO1=0.0
SCAN_WORKLOAD_USES_TIME_TO_RUN=1
EXEC_TIME=40
ADAPT1=1
USE_TREE_UPDATE1=0
HOT_OP_RATIO1=1.0

function run_workload {
  local dir="$1"
  local ind_name="$2"

  mkdir -p ${dir}
  
  sed -i "s|lsmt_path=.*|lsmt_path=${dir}|" options/default_${ind_name}.spec

  ../build/ycsbc/ycsb -options options/default_${ind_name}.spec -workload options/default_workload.spec -sequence ${NUM_WORKLOADS} i-0.0-r-0.0-s-${SCAN_RATIO1}-u-${UPDATE_RATIO1} ${SCAN_WORKLOAD_USES_TIME_TO_RUN} ${EXEC_TIME} ${USE_TREE_UPDATE1} ${ADAPT1} ${HOT_OP_RATIO1} 1> ${ind_name}_$(date +%F_%H_%M).out 2>&1
  
  rm -rf dir
}

run_workload "../build/wot_path_adapt" "aha"
run_workload "../build/level_path" "level"
run_workload "../build/btree_path" "btree"

# Example cmd:
# ../build/ycsbc/ycsb -options options/template_${ind_name}.spec -workload options/template_workload.spec -sequence 3 i-0.0-r-0.0-s-1.0-u-0.0 0 5000000 0 1 1.0 i-0.0-r-0.0-s-0.0-u-1.0 0 5000000 0 0 -1.0 i-0.0-r-0.0-s-1.0-u-0.0 0 15000000 0 1 1.0
# The workload includes 3 phases:
# Phase 1: 50M scans. Uses operation number; op num 5000000; does not use tree-insert; adapt; 100% scan is over hot data.
# Phase 2: 50M updates. Uses operation number; op num 5000000; does not use tree-insert; does not adapt; update is over entire space.
# Phase 3: 150M scans. Uses operation number; op num 15000000; does not use tree-insert; adapt; 100% scan is over hot data.
