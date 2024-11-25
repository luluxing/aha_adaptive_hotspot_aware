
# python3 ../plt_throughput.py 4 test_2_2_64_2023-03-19_19_11.out L2T2F64 test_4_2_64_2023-03-19_19_15.out L4T2F64 test_2_4_64_2023-03-19_19_19.out L2T4F64 test_2_2_128_2023-03-19_19_23.out L2T2F128 Insert-only

import sys
import re
import numpy as np
import matplotlib.pyplot as plt
import math

def parse_throughput(f):
    wl = ''
    record_ops = False
    res = [[], [], [], [], []]
    tmp1, tmp2, tmp3 = [], [], []
    x, y = 0, 0
    while 1:
        line = f.readline()
        if line == '' or 'Tree height is 5' in line:
            break
        # if 'Flush: ' in line:
        #     if 'true' in line:
        #         wl = 'Flush_all'
        #     else:
        #         wl = 'Flush_part'
        #     continue
        if '###### Operations ########' in line:
            record_ops = True
            continue
        # if record_ops and 'output' in line:# 'ops and' not in line and 'TreeHeight' not in line:
        #     line = line.rstrip()
        #     words = line.split(' ')
        #     tmp1.append(int(words[0]))
        #     user_id = int(line.split('[')[1].split(',')[0][4:])
        #     res[4].append(user_id)
        #     # tmp2.append(int(words[-1]))
        #     continue
        # if record_ops and 'ops and' not in line:
        #     record_ops = False
        #     continue
        if record_ops and 'ops and' in line:
            words = re.split('\(|\)|, |,', line)
            time = float(words[-2])
            if time <= 60:
                # continue
                x += float(words[4])
            if time >= 60:
                # break
                y += float(words[4])
            res[0].append((float(words[4])))
            res[1].append(time)
            # Node count
            # res[2].append(sum(tmp1))
            # Result size
            res[2].append(sum(tmp1) / len(tmp1) if tmp1 != [] else 0)
            res[3].append(np.std(tmp1) if tmp1 != [] else 0)
            tmp1, tmp2 = [], []
        # if len(res[0]) >= 400:
        #     break
    print(f, x, y)
    return res

def main():
    file_num = int(sys.argv[1])
    files, specs = [], []
    argv_ind = 2
    for _ in range(file_num):
        files.append(sys.argv[argv_ind])
        argv_ind += 1
        specs.append(sys.argv[argv_ind])
        argv_ind += 1
    
    res = []
    for i in range(file_num):
        res.append(parse_throughput(open(files[i], 'r')))
        # print(len(res[i][0]), len(res[i][1]))

    # fig, ax = plt.subplots()
    fig = plt.figure(figsize=(18,8))
    ax = fig.add_subplot(111)
    marks = ['o-', 's-', 'D-', 'v-', '^-', 'p-', '*-', 'h-']
    colors = ['red', 'green', 'blue', 'cyan', 'magenta', 'yellow', 'black', 'orange']

    window_size = 5
    
     
    for i in range(file_num):
        ax.plot(res[i][1], res[i][0], marks[i], label=specs[i], c=colors[i], alpha=0.5)
        # ax.errorbar(res[i][1], res[i][2], yerr=res[i][3], label=specs[i]+" Avg range query result size", c=colors[i])
        # ax.plot(res[i][1], res[i][3], marks[i+2], label=specs[i], c=colors[i+2])
        # ax.hist(res[i][4], bins=30, label=specs[i], alpha=0.5)

    ax.set_ylabel('Throughput (tps)')
    # ax.set_xticks(np.arange(minx-1, maxx+1, 300.0))
    ax.set_xlabel('Time (sec)')
    # ax.set_xlim(0, 400)
    # ax.set_xticklabels([2, 4, 6, 10, 17, 28, 46, 75, 121])
    ax.set_title(sys.argv[-1])
    ax.legend(fontsize=15)
    fig.tight_layout()

    # plt.show()
    plt.savefig(sys.argv[-1])

if __name__ == '__main__':
    main()