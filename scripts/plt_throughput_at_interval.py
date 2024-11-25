import sys
import re
import numpy as np
import matplotlib.pyplot as plt
import math

def parse_throughput(f):
    record_ops = False
    y = []
    while 1:
        line = f.readline()
        if line == '':
            break
        if '###### Operations ########' in line:
            record_ops = True
            continue
        if record_ops:
            words = line.split()
            for word in words:
                if word.startswith('$') and word.endswith(';'):
                    y.append(float(word[1:-1]))
    return y            

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

    fig = plt.figure(figsize=(18,8))
    ax = fig.add_subplot(111)
    marks = ['o-', 's-', 'D-', 'v-', '^-', 'p-', '*-', 'h-']
    colors = ['red', 'green', 'blue', 'cyan', 'magenta', 'yellow', 'black', 'orange']    
     
    for i in range(file_num):
        ax.plot(range(len(res[i])), res[i], marks[i], label=specs[i], c=colors[i], alpha=0.5)
        
    ax.set_ylabel('Throughput/sec (tps)')
    # ax.set_xticks(np.arange(minx-1, maxx+1, 300.0))
    ax.set_xlabel('Every 5k ops')
    # ax.set_xlim(0, 400)
    # ax.set_xticklabels([2, 4, 6, 10, 17, 28, 46, 75, 121])
    ax.set_title(sys.argv[-1])
    ax.legend(fontsize=15)
    fig.tight_layout()

    # plt.show()
    plt.savefig(sys.argv[-1])

if __name__ == '__main__':
    main()