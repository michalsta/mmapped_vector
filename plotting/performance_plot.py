import subprocess, itertools, json
from tqdm import tqdm
import socket
from random import shuffle
import pandas as pd

def run_performance_script(test_size):
    outputs = []
    file_path = "test.dat"
    if socket.gethostname() == "utm":
        file_path = "/home/mist/test.dat"
    result = subprocess.run(['../performance', str(test_size), file_path], capture_output=True, text=True)
    json_text = '[' + result.stdout.split('[')[1]
    results = json.loads(json_text)
    res = {t['name']: t['duration'] for t in results}
    res['test_size'] = test_size
    del res['item_count']
    return res

if __name__ == "__main__":
    no_repeats = 20  # Change this to the number of times you want to run the script
    test_sizes = [1, 3, 10, 33, 100, 333, 1000, 3333, 10000, 33333, 100000, 333333, 1000000, 3333333, 10000000, 33333333, 100000000]#, 333333333, 1000000000]
    results = []
    loop_args = list(itertools.product(range(no_repeats), test_sizes))
    shuffle(loop_args)
    for repeat_num, test_size in tqdm(loop_args):
        result = run_performance_script(test_size)
        #print(result)
        results.append(result)
    results = pd.DataFrame(results)
    print(results)
    results = results.groupby('test_size').mean().reset_index()
    print(results)

    import matplotlib.pyplot as plt
    import numpy as np
    import matplotlib.cm as cm

    # lines plot, one line per algorithm
    # make the one named "std::vector" really stand out
    # use rainbow color map

    colors = cm.rainbow(np.linspace(0, 1, len(results.columns) - 1))
    color_dict = {colname: color for colname, color in zip(results.columns[1:], colors)}

    for colname in results.columns:
        if colname == 'test_size':
            continue
        if colname == 'std::vector':
            plt.plot(results.test_size, results[colname], label=colname, linewidth=5, color='black')
        else:
            plt.plot(results.test_size, results[colname], label=colname, color=color_dict[colname])
    plt.legend()
    plt.xscale('log')
    plt.yscale('log')
    plt.show()
