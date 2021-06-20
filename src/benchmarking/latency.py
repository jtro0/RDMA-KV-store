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

all_types = []
for type in types:
    all_latencies = []
    print(type[0])
    for filename in filenames:
        # print(filename)
        type_file, number_clients, client_number, number_ops = util.get_parts(filename)

        if (type != type_file) or (number_clients_arg != number_clients):
            continue
        print("GOTCHA")
        df = pd.read_csv(filename)

        print(df['latency'].mean())
        if (type[0] == 'TCP'):
            latency_usec = df['latency'].div(1000)
        else:
            latency_usec = df['latency'].mul(1000)
        print(latency_usec.mean())
        all_latencies.append(latency_usec)

    if len(all_latencies) > 0:
        print("append %s" % type[0])
        all_types.append(pd.concat(all_latencies))

all_types_df = pd.DataFrame(all_types, columns=['TCP', 'RC', 'UC', 'UD'])
ax = all_types_df.boxplot()
plot_title = "Latency box plot with %(clients)d clients" % { "clients":number_clients_arg}
plt.title(plot_title)
# ax.legend(bbox_to_anchor=(1.05, 1), loc=2, borderaxespad=0.)
plt.xlabel("Transportation type")
plt.ylabel("Latency (usec)")
plt.grid(linestyle='dotted')

graph_filename = "../../benchmarking/graphs/Latency_box_%(clients)d.pdf" % {"clients":number_clients_arg}
plt.savefig(graph_filename, dpi=100, bbox_inches="tight")

