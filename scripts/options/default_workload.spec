recordcount=10000000
operationcount=100000000
workload=com.yahoo.ycsb.workloads.CoreWorkload
readallfields=true

zeropadding=20
fieldlength=128

insertorder=nonhashed
# uniform | zipfian_seed
insertdistribution=uniform
insert_scale_factor=100

txnkeyorder=nonhashed
# uniform | zipfian_seed | normal | hotspot
requestdistribution=hotspot-1
# Default mean is in the center
# normal_mean=54306300
# 90%: 1.645; 95%: 1.96
normal_confidence_level=0.9

hotdatapercent=0.5

zipf_start=0
zipf_end=100000000
requestseed=1
fixedcanlength=1000
fixedselectivity=0
maxscanlength=1
scanlengthdistribution=uniform
scanlengthseed=2
