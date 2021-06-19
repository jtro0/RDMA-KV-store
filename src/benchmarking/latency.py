import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from scipy.stats import norm
import util
import sys

filenames = util.get_all_csv()

type_arg = str(sys.argv[1])
number_clients_arg = int(sys.argv[2])
    
fig = plt.figure()
ax = fig.add_axes([0.1, 0.1, 0.6, 0.75])

all_latencies = []
for filename in filenames:
    type, number_clients, client_number, number_ops = util.get_parts(filename)
    
    if (type_arg != type) or (number_clients_arg != number_clients):
        continue
    
    df = pd.read_csv(filename)
    
    # if client_number == 0:
    #     first_sec = df[:1]["start_sec"]
    #     first_usec = df[:1]["start_usec"]
    # elif client_number == 9:
    #     last_sec = df.tail(1)["end_sec"]
    #     last_usec = df.tail(1)["end_usec"]
    #
    # current_first_sec = df.head(1)["start_sec"].at[0]
    # current_first_usec = df.head(1)["start_usec"].at[0]
    # current_last_sec = df.tail(1)["end_sec"].at[len(df.index)-1]
    # current_last_usec = df.tail(1)["end_usec"].at[len(df.index)-1]
    #
    # time_taken_sec = util.calc_time_difference_sec(current_first_sec, current_first_usec, current_last_sec, current_last_usec)
    #
    print(df['latency'].mean())
    latency_usec = df['latency'].mul(1000)
    print(latency_usec.mean())
    all_latencies.append(latency_usec)
# ops_per_sec = len(df.index) / time_taken_sec
    
    # plot_label = 'Client %(id)d: %(ops)d ops/sec' % {"id":client_number, "ops":ops_per_sec}
concat = pd.concat(all_latencies)
stats = concat.groupby('latency')['latency'].agg('count').pipe(pd.DataFrame).rename(columns = {'latency' : 'frequency'})

stats['pdf'] = stats['frequency'] / sum(stats['frequency'])

stats['cdf'] = stats['pdf'].cumsum()
stats = stats.reset_index()

#stats.plot(x = 'latency', y = 'cdf', grid=True)

#norm_cdf = norm.cdf(concat)
ax.plot(stats['latency'], stats['cdf'])

plot_title = "%(type)s connection, %(clients)d clients: Latency CDF" % {"type": type_arg, "clients":number_clients_arg}
plt.title(plot_title)
# ax.legend(bbox_to_anchor=(1.05, 1), loc=2, borderaxespad=0.)
plt.xlabel("Latency (usec)")
plt.ylabel("Probability")
plt.grid(linestyle='dotted')

graph_filename = "../../benchmarking/graphs/%(type)s_Latency_cdf_%(clients)d.pdf" % {"type":type_arg, "clients":number_clients_arg}
plt.savefig(graph_filename, dpi=100, bbox_inches="tight")

