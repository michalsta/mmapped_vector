import subprocess, itertools, json
from tqdm import tqdm

def run_performance_script(test_size):
    outputs = []
    result = subprocess.run(['../performance', str(test_size), "a"], capture_output=True, text=True)
    json_text = '[' + result.stdout.split('[')[1]
    results = json.loads(json_text)
    res = {t['name']: t['duration'] for t in results}
    return res

if __name__ == "__main__":
    no_repeats = 5  # Change this to the number of times you want to run the script
    test_sizes = [1, 3, 10, 33, 100, 333, 1000, 3333, 10000, 33333, 100000, 333333, 1000000, 3333333, 10000000]
    results = []
    for repeat_num, test_size in tqdm(itertools.product(range(no_repeats), test_sizes), total=no_repeats*len(test_sizes)):
        result = run_performance_script(test_size)
        results.append(result)
    #print(results)

    from collections import defaultdict
    grouped = defaultdict(list)
    for result in results:
        item_count = result['item_count']
        for key, value in result.items():
            grouped[(item_count, key)].append(value)

    averages = {key: sum(values) / len(values) for key, values in grouped.items()}
    # drop the item_count key
    averages = {key: value for key, value in averages.items() if key[1] != 'item_count'}
    x_axis = test_sizes
    y_axes = defaultdict(list)
    for key, value in averages.items():
        y_axes[key[1]].append(value)

    print(x_axis)
    print(y_axes)

    import matplotlib.pyplot as plt
    import numpy as np

    # lines plot, one line per algorithm
    # make the one named "std::vector" really stand out
    for key, values in y_axes.items():
        if key == 'std::vector':
            plt.plot(x_axis, values, label=key, linewidth=5)
        else:
            plt.plot(x_axis, values, label=key)
    plt.legend()
    plt.xscale('log')
    plt.yscale('log')
    plt.show()
