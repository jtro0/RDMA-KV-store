import util
import sys
import pandas as pd
import matplotlib.pyplot as plt
from operator import add, sub

filenames = util.get_all_csv()

types = [["TCP", "o", "tab:blue"], ["RC", "^", "tab:orange"], ["UC", "P", "tab:red"], ["UD", "x", "tab:green"]]

max_clients = int(sys.argv[1])

fig = plt.figure()
ax = fig.add_axes([0.1, 0.1, 0.6, 0.75])


for type in types:    
    per_number_client = []
    first_quartile = []
    third_quartile = []
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
            
            if (type[0] != type_file) or number_clients != current_number_clients or number_ops != 10000000:
                continue

            df = pd.read_csv(filename)
            #ms = df.apply(lambda x:  util.time_in_msec(x[" diff_sec"], x[" diff_usec"]), axis=1)
            ms = df['latency']
            if len(ms) > 0:
                # print(ms)
                all_latencies.append(ms)
            
        if len(all_latencies) > 0:
            concat = pd.concat(all_latencies)

            if (type[0] == "TCP" and not (current_number_clients == 10 or current_number_clients > 30)) or (type[0] == "RC" and current_number_clients == 1):
                data = [concat.quantile(.25) / 1000, concat.quantile(.75) / 1000, concat.mean()/1000]
            else:
                data = [concat.quantile(.25) * 1000, concat.quantile(.75) * 1000, concat.mean()*1000]

            print(type[0] + ', ' + str(current_number_clients) + ': ' + str(data))
            first_quartile.append(data[0])
            third_quartile.append(data[1])
            per_number_client.append(data[2])
            # std_deviation.append(concat.std()/1000)
        # per_number_client[current_number_clients-1] = ops_per_sec
        else:
            per_number_client.append(0)
            first_quartile.append(0)
            third_quartile.append(0)
        
    if per_number_client:
        plot_label = type[0]
        first_quartile_label = "First and third quartile %s" % type[0]

        # x_plus_std = list(map(add, x_values, std_deviation))
        # x_min_std = list(map(sub, x_values, std_deviation))
        ax.plot(x_values, per_number_client, label=plot_label, marker=type[1], color=type[2])
        ax.plot(x_values, first_quartile, label=first_quartile_label, linestyle='--', color=type[2])
        ax.plot(x_values, third_quartile, linestyle='--', color=type[2])
        ax.fill_between(x_values, first_quartile, third_quartile, alpha=0.1, interpolate=True)
    
plt.title("Overall latency per Transport Type")
plt.legend(loc="upper left", bbox_to_anchor=(1, 0.5))
plt.xlabel("Number of clients")
plt.ylabel("Latency (usec)")
plt.grid(linestyle='dotted')

graph_filename = "../../benchmarking/graphs/Latency_avg_%d.pdf" % max_clients
plt.savefig(graph_filename, dpi=100, bbox_inches="tight")
#plt.show

     