recordcount=10000000
operationcount=100000000
workload=com.yahoo.ycsb.workloads.CoreWorkload
readallfields=true

readproportion=0
updateproportion=0
scanproportion=1
insertproportion=0

zeropadding=20

insertorder=nonhashed
# uniform | zipfian_seed
insertdistribution=uniform
insert_scale_factor=100

txnkeyorder=nonhashed
# uniform | zipfian_seed | normal
requestdistribution=normal
# Default mean is in the center
# normal_mean=54306300
# 90%: 1.645; 95%: 1.96
normal_confidence_level=0.9

fieldlength=128

hotspot=0.1
scan_hot=1
update_hot=0.9

zipf_start=0
zipf_end=100000000
requestseed=1
fixedcanlength=100
maxscanlength=1
scanlengthdistribution=uniform
scanlengthseed=2

# 10^8 - 1%
#hot_queried_start=00000000005764306300
#hot_queried_end=00000000005864306300
# 10^8 - 10%
#hot_queried_start=00000000005764306300
#hot_queried_end=00000000006764306300
# 10^8 - 20%
#hot_queried_start=00000000005764306300
#hot_queried_end=00000000007764306300
# 10^8 - 30%
#hot_queried_start=00000000005764306300
#hot_queried_end=00000000008764306300
# 10^8 - 100%
#hot_queried_start=00000000000000000000
#hot_queried_end=00009999998764306300
# 10^7
hot_queried_start=00000000000424306300
hot_queried_end=00000000000524306300
# 10^6
#hot_queried_start=00000000000000000000
#hot_queried_end=00000000999934306300
