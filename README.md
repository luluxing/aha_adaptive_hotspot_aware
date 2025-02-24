# An adpative index for Oscillating Write-Heavy and Read-Heavy Workloads (AHA-tree)
AHA-tree (Adaptive Hotspot Aware Tree) is an adaptive index that can adapt itself to the cyclic oscillating workloads that oscillate between write-heavy and read-heavy. With the observation that real-world datasets are skewed, the focus is to optimize the index within the hotspot regions. In this repo, we include instructions on how to build and run our code.

## Dependencies
- [LevelDB](https://github.com/google/leveldb)
- [TBB](https://github.com/oneapi-src/oneTBB)

Please modify the following path in `CMakeLists.txt` to include `LevelDB` as a baseline:
```
include_directories(~/software/leveldb/include)
```
Please also modify the following path in `src/CMakeLists.txt` to include the path for `TBB`:
```
find_package(TBB REQUIRED PATHS "~/software/my_tbb/" NO_DEFAULT_PATH) 
```

## Running the code
There are three branches in this repo. Please use the default one for the main experiment and use the other two for the experiments on hotspot versatility.

All experiments related code is in `scripts/`. The parameters for the baselines as well as AHA-tree are included in files in `scripts/options/default_INDEX.spec`. Since all indexes are disk-based, data are written to the path stored in `lsmt_path` in the parameter file. The parameter for the workload is in `scripts/options/default_workload.spec`.

`run_all.sh` shows how to run the code. There are other parameters that need to be set when running and they are specified in `run_all.sh`.

`write_amplification.sh` is used to record the write amplification of the baselines and AHA-tree.

You may use the plotting scripts to generate figures using the output of the experiments.
