# An adpative index AHA
In this repo, we include instructions on how to build and run our code.

## Dependencies
- [LevelDB](https://github.com/google/leveldb)
- [TBB](https://github.com/oneapi-src/oneTBB)

The following line in `CMakeLists.txt` may require modifiction:
```
include_directories(~/software/leveldb/include)
```
Please also modify the following line in `src/CMakeLists.txt`:
```
find_package(TBB REQUIRED PATHS "~/software/my_tbb/" NO_DEFAULT_PATH) 
```

## Running the code
The configuration of each index is included in `src/options/default_INDEX.spec` and the workload specification is in `src/options/default_workload.spec`.

The example code is in `scripts/`. 