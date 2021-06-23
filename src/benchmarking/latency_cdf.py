import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from scipy.stats import norm
import util
import sys


#type_arg = str(sys.argv[1])

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

#type_arg = str(sys.argv[1])

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
        # concated = pd.concat(all_latencies)
        # mean = concated.mean()
        # min = concated.min()
        # max = concated.max()
        # q_one = concated.quantile(0.25)
        # q_three = concated.quantile(0.75)
        # std = concated.std()
        # print(f"mean: {mean}, min: {min}, max: {max}, q1: {q_one}, q3: {q_three}, std: {std}")
        # all_types_df[type[0]] = concated

        concat = pd.concat(all_latencies).to_frame(name='latency')
        print(concat)
        stats = concat.groupby('latency')['latency'].agg('count').pipe(pd.DataFrame).rename(columns = {'latency' : 'frequency'})

        stats['pdf'] = stats['frequency'] / sum(stats['frequency'])

        stats['cdf'] = stats['pdf'].cumsum()
        stats = stats.reset_index()

        #stats.plot(x = 'latency', y = 'cdf', grid=True)

        #norm_cdf = norm.cdf(concat)
        plot_label = type[0]
        ax.plot(stats['latency'], stats['cdf'], label=plot_label, color=type[2])

if blocking_arg == "blocking":
    plot_title = "%(clients)d clients with blocking for WC: Latency CDF" % {"clients":max_clients}
    filename_addition = "_blocking"
else:
    plot_title = "%(clients)d clients: Latency CDF" % {"clients":max_clients}
    filename_addition = ""

plt.xlim(0, 500)
plt.title(plot_title)
ax.legend(bbox_to_anchor=(1.05, 1), loc=2, borderaxespad=0.)
plt.xlabel("Latency (usec)")
plt.ylabel("Probability")
plt.grid(linestyle='dotted')

graph_filename = "../../benchmarking/graphs/Latency_cdf_%(clients)d%(add)s.pdf" % {"clients":max_clients, "add":filename_addition}
plt.savefig(graph_filename, dpi=100, bbox_inches="tight")
