

# list(APPEND CMAKE_MODULE_PATH "~/software/my_tbb/")
find_package(TBB REQUIRED PATHS "~/software/my_tbb/" NO_DEFAULT_PATH) 

# find_package(TBB REQUIRED)

###################### 
file(GLOB_RECURSE
        WOT2LSM_SRCS
        CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/src/leveldb/*.h
        ${CMAKE_SOURCE_DIR}/src/*.cc
        ${CMAKE_SOURCE_DIR}/src/*.h
)

add_library(WotAdapt OBJECT "${WOT2LSM_SRCS}")
set_target_properties(WotAdapt PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_EXTENSIONS OFF
)
target_include_directories(WotAdapt PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    # ${CMAKE_SOURCE_DIR}/extern
    # ${CMAKE_SOURCE_DIR}/extern/rocksdb4wot/include/rocksdb
)

target_link_libraries(WotAdapt PUBLIC TBB::tbb)