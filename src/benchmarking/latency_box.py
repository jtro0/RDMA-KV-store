import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from scipy.stats import norm
import util
import sys


types = [["TCP", "o", "tab:blue"], ["RC", "^", "tab:orange"], ["UC", "P", "tab:red"], ["UD", "x", "tab:green"]]

# Change to use getopt?
max_clients = int(sys.argv[1])
blocking_arg = ""
if len(sys.argv) > 2:
    blocking_arg = str(sys.argv[2])

if blocking_arg == "blocking":
    filenames = util.get_all_csv_blocking()
else:
    filenames = util.get_all_csv()

fig = plt.figure()
ax = fig.add_axes([0.1, 0.1, 0.6, 0.75])

all_types_df = pd.DataFrame(columns=['TCP', 'RC', 'UC', 'UD'])
# all_types = []
for type in types:
    all_latencies = []
    print(type[0])
    for filename in filenames:
        # print(filename)
        type_file, number_clients, client_number, number_ops = util.get_parts(filename)

        if (type[0] != type_file) or (max_clients != number_clients):
            continue
        df = pd.read_csv(filename)

        # print(df['latency'].mean())
        latency_usec = df['latency']
        # print(latency_usec.mean())
        all_latencies.append(latency_usec)

    if len(all_latencies) > 0:
        concated = pd.concat(all_latencies)
        mean = concated.mean()
        min = concated.min()
        max = concated.max()
        q_one = concated.quantile(0.25)
        q_three = concated.quantile(0.75)
        q_inner = q_three - q_one
        q_ninefive = concated.quantile(0.95)
        q_ninenine = concated.quantile(0.99)

    std = concated.std()
        print(f"mean: {mean}, min: {min}, max: {max}, q1: {q_one}, q3: {q_three}, inner quartile: {q_inner}, 95th percentile: {q_ninefive}, 99th percentile: {q_ninenine}, std: {std}")
        all_types_df[type[0]] = concated


if blocking_arg == "blocking":
    plot_title = "Latency box plot with %(clients)d clients and blocking for WC" % { "clients":max_clients}
    filename_addition = "_blocking"
else:
    plot_title = "Latency box plot with %(clients)d clients" % { "clients":max_clients}
    filename_addition = ""

ax = all_types_df.boxplot(showfliers=False)
plt.title(plot_title)
# ax.legend(bbox_to_anchor=(1.05, 1), loc=2, borderaxespad=0.)
plt.xlabel("Transportation type")
plt.ylabel("Latency (usec)")
plt.grid(linestyle='dotted')

graph_filename = f"../../benchmarking/graphs/Latency_box_no_out_{max_clients}{filename_addition}.pdf"
plt.savefig(graph_filename, dpi=100, bbox_inches="tight")

