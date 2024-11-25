
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
    while 1:
        line = f.readline()
        if line == '':
            break
        if '###### Operations ########' in line:
            record_ops = True
            continue
        if record_ops and 'ops and' in line:
            words = re.split('\(|\)|, |,', line)
            time = float(words[-2])
            res[0].append(math.log10(float(words[4])) if float(words[4]) != 0 else 0)
            res[1].append(time)
        if record_ops and 'wait_for_lock' in line:
            # Total_read: 169540.072325, wait_for_lock: 2159.012866: lsmt 511.739597, root 368.812734, traverse: 2092.212881, reader_cnt: 25883
            # Extract the above line to get the wait_for_lock time
            words = re.split(': |, ', line)
            res[2].append(math.log10(float(words[3])) if float(words[3]) != 0 else 0)
        if record_ops and 'update-pivot' in line:
            # Tree#0: -nan (real_work 0.000000 includes compact 0.000000; flush 0.000000; split-node 0.000000; split-small-leaf 0.000000; update-pivot: 0.000000 + -nan + -nan | -nan)
            # Extract the above line to get the update-pivot time
            words = re.split(': |; | ', line)
            res[3].append((math.log10(float(words[-9]))) if float(words[-9]) != 0 else 0)
            res[4].append(float(words[-7]))
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

    fig = plt.figure(figsize=(18,8))
    # fig, ax = plt.subplots()
    ax = fig.add_subplot(111)

    marks = ['o-', 's-', 'D-', 'v-', '^-', 'p-', '*-', 'h-']
    colors = ['red', 'green', 'blue', 'cyan', 'magenta', 'yellow', 'black', 'orange']

    window_size = 10
    
    for i in range(file_num):
        new_res, new_x = [], []
        j = 0
        while j < len(res[i][1]):
            t0 = res[i][1][j]
            k = j + 1
            tmp = [res[i][0][j]]
            while k < len(res[i][1]):
                t1 = res[i][1][k]
                if t1 < t0 + window_size:
                    tmp.append(res[i][0][k])
                    k += 1
                else:
                    break
            new_res.append(sum(tmp) / len(tmp))
            new_x.append(t0)
            j += 1
        ax.plot(new_x, new_res, marks[i], label=specs[i], c=colors[i], alpha=0.4, markersize=4)
        # print(len(new_res), len(res[i][0]))
        # ax.plot(range(len(res[i][0])), res[i][0], marks[i], label=specs[i], c=colors[i], alpha=0.5)
        ax.plot(range(len(res[i][2])), res[i][2], marks[i+1], label=specs[i]+" rlock", c=colors[i+2])
        ax.plot(range(len(res[i][3])), res[i][3], marks[i+2], label=specs[i]+" wlock", c=colors[i+3])
        ax.plot(range(len(res[i][4])), res[i][4], marks[i+3], label=specs[i]+" ins", c=colors[i+4])

    ax.set_ylabel('Throughput (log10 (tps))\nWindow-size=10')
    # ax.set_xticks(np.arange(minx-1, maxx+1, 300.0))
    ax.set_xlabel('Time (sec)')
    # ax.set_xticks(range(0, 360, 60))
    # ax.set_xticklabels([2, 4, 6, 10, 17, 28, 46, 75, 121])
    ax.set_title(sys.argv[-1])
    ax.legend(fontsize=15)
    fig.tight_layout()

    # plt.show()
    plt.savefig(sys.argv[-1])

if __name__ == '__main__':
    main()