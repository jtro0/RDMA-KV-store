import util
import sys
import pandas as pd
import matplotlib.pyplot as plt
from operator import add, sub

filenames = util.get_all_csv()

types = ["TCP", "RC", "UC", "UD"]

max_clients = int(sys.argv[1])

fig = plt.figure()
ax = fig.add_axes([0.1, 0.1, 0.6, 0.75])


for type in types:    
    per_number_client = []
    five_quantile = []
    ninety_five_quantile = []
    std_deviation = []
    
    x_values = list(range(1, 11))
    if max_clients > 10:
        x_values = x_values + list(range(15, max_clients+1, 5))

    for current_number_clients in x_values:
        current_start_sec = sys.maxsize
        current_start_usec = sys.maxsize
        
        current_end_sec = 0
        current_end_usec = 0
        
        all_latencies = []
        
        for filename in filenames:
            type_file, number_clients, client_number, number_ops = util.get_parts(filename)
            
            if (type != type_file) or number_clients != current_number_clients or number_ops != 10000000:
                continue

            df = pd.read_csv(filename)
            #ms = df.apply(lambda x:  util.time_in_msec(x[" diff_sec"], x[" diff_usec"]), axis=1)
            ms = df['latency']
            if len(ms) > 0:
                all_latencies.append(ms)
            
        if len(all_latencies) > 0:
            concat = pd.concat(all_latencies)
            five_quantile.append(concat.quantile(.05))
            ninety_five_quantile.append(concat.quantile(.95))
            per_number_client.append(concat.mean())
            std_deviation.append(concat.std())
        # per_number_client[current_number_clients-1] = ops_per_sec

        
    if per_number_client:
        plot_label = type
        #x_plus_std = list(map(add, x_values, std_deviation))
        #x_min_std = list(map(sub, x_values, std_deviation))
        ax.plot(x_values, per_number_client, label=plot_label)
        ax.fill_between(x_values, ninety_five_quantile, five_quantile, alpha=0.5, interpolate=True)
    
plt.title("Overall latency per Transport Type")
plt.legend(loc="upper left", bbox_to_anchor=(1, 0.5))
plt.xlabel("Number of clients")
plt.ylabel("Latency (ms)")

graph_filename = "../../benchmarking/graphs/Latency_avg_%d.png" % max_clients
plt.savefig(graph_filename, dpi=100)
#plt.show

     