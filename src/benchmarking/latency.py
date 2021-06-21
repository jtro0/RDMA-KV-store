import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from scipy.stats import norm
import util
import sys

filenames = util.get_all_csv()

#type_arg = str(sys.argv[1])
number_clients_arg = int(sys.argv[1])

types = [["TCP", "o", "tab:blue"], ["RC", "^", "tab:orange"], ["UC", "P", "tab:red"], ["UD", "x", "tab:green"]]

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

        if (type[0] != type_file) or (number_clients_arg != number_clients):
            continue
        df = pd.read_csv(filename)

        # print(df['latency'].mean())
        latency_usec = df['latency'].mul(1000)
        # print(latency_usec.mean())
        all_latencies.append(latency_usec)

    if len(all_latencies) > 0:
        concated = pd.concat(all_latencies)
        mean = concated.mean()
        min = concated.min()
        max = concated.max()
        q_one = concated.quartile(0.25)
        q_three = concated.quartile(0.75)
        std = concated.std()
        print("mean: %d, min: %d, max: %d, q1: %d, q3: %d, std: %d" % mean, min, max, q_one, q_three, std)
        all_types_df[type[0]] = concated

ax = all_types_df.boxplot(showfliers=False)
plot_title = "Latency box plot with %(clients)d clients" % { "clients":number_clients_arg}
plt.title(plot_title)
# ax.legend(bbox_to_anchor=(1.05, 1), loc=2, borderaxespad=0.)
plt.xlabel("Transportation type")
plt.ylabel("Latency (usec)")
plt.grid(linestyle='dotted')

graph_filename = "../../benchmarking/graphs/Latency_box_no_out_%(clients)d.pdf" % {"clients":number_clients_arg}
plt.savefig(graph_filename, dpi=100, bbox_inches="tight")

