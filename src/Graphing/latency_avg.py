import util
import sys
import pandas as pd
import matplotlib.pyplot as plt

types = [["TCP", "o", "tab:blue"], ["RC", "^", "tab:orange"], ["UC", "P", "tab:red"], ["UD", "x", "tab:green"]]

# Change to use getopt?
max_clients = int(sys.argv[1])
blocking_arg = ""
if len(sys.argv) > 2:
    blocking_arg = str(sys.argv[2])

if blocking_arg == "blocking":
    filenames = util.get_all_csv_blocking() # filenames in blocking folder
else:
    filenames = util.get_all_csv() # filenames in non-blocking folder

fig = plt.figure()
ax = fig.add_axes([0.1, 0.1, 0.6, 0.75])


for type in types:    
    per_number_client = []
    first_quartile = []
    third_quartile = []
    std_deviation = []

    # Incremental values. Up to 45 look like this [1,2,3,4,5,6,7,8,9,10,15,20,25,30,31,32,33,34,35,40,45]
    x_values = list(range(1, 11))
    if max_clients >= 35:
        x_values = x_values + list(range(15, 31, 5)) + list(range(30, 35)) + list(range(35, max_clients+1, 5))
    elif max_clients > 10:
        x_values = x_values + list(range(15, max_clients+1, 5))

    for current_number_clients in x_values:
        current_start_sec = sys.maxsize
        current_start_usec = sys.maxsize
        
        current_end_sec = 0
        current_end_usec = 0
        
        all_latencies = []
        
        for filename in filenames:
            type_file, number_clients, client_number, number_ops = util.get_parts(filename)
            
            if (type[0] != type_file) or number_clients != current_number_clients:
                continue

            df = pd.read_csv(filename)
            latency = df['latency']
            if len(latency) > 0:
                all_latencies.append(latency) # append all latencies
            
        if len(all_latencies) > 0:
            # concatenate all latencies into a single series
            concated = pd.concat(all_latencies)

            # Get statistical data
            mean = concated.mean()
            median = concated.median()
            min = concated.min()
            max = concated.max()
            q_one = concated.quantile(0.25)
            q_three = concated.quantile(0.75)
            q_inner = q_three - q_one
            q_ninefive = concated.quantile(0.95)
            q_ninenine = concated.quantile(0.99)
            std = concated.std()

            # Print these stats out for further investigation
            print(f"{type[0]} {current_number_clients}: mean: {mean}, median: {median}, min: {min}, max: {max}, q1: {q_one}, q3: {q_three}, inner quartile: {q_inner}, 95th percentile: {q_ninefive}, 99th percentile: {q_ninenine}, std: {std}")
            per_number_client.append(mean)
            # std_deviation.append(concat.std()/1000)
        # per_number_client[current_number_clients-1] = ops_per_sec
        else:
            per_number_client.append(0)
            first_quartile.append(0)
            third_quartile.append(0)
        
    if per_number_client:
        plot_label = type[0]
        first_quartile_label = "First and third quartile %s" % type[0]

        ax.plot(x_values, per_number_client, label=plot_label, marker=type[1], color=type[2])

if blocking_arg == "blocking":
    plot_title = "Overall latency per Transport Type while Blocking for WC"
    filename_addition = "_blocking"
else:
    plot_title = "Overall latency per Transport Type"
    filename_addition = ""

plt.title(plot_title)
plt.legend(loc="upper left", bbox_to_anchor=(1, 0.5))
plt.xlabel("Number of clients")
plt.ylabel("Latency (usec)")
plt.grid(linestyle='dotted')

graph_filename = f"../../benchmarking/graphs/Latency_avg_{max_clients}{filename_addition}.pdf"
plt.savefig(graph_filename, dpi=100, bbox_inches="tight")
#plt.show

     