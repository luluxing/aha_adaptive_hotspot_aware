dbname=wot
loading_threads=10
threads=3
report_throughput=1

lsmt_path=../build/wot_path_adapt

buffer_manager_num=512
batch_size=24
block_cache_size=3145728
max_open_files=74

is_lsmt=1
node_lsmt_level_limit=2
lsmt_level_limit=3
# Only used in the transition state when small leaf is allowed to have one file
leaf_limit=1

flush_file_num=0

# 6MB=6291456
# 4MB=4194304
memtable_size=6291456
file_size=6291456

# For small leaf, the scale to reduce file size. Not sure whether it is useful
scale_in=4

# Adapt strategy
adapt_strategy=4
# Default proactive validation is false
proactive_validation=0

# First page policy: evensplit=1, soundremedy=2
page_split_first_policy=1
# Second page policy: evensplit=1
page_split_second_policy=1