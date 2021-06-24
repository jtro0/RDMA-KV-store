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
    filenames = util.get_all_csv_blocking()
else:
    filenames = util.get_all_csv()

fig = plt.figure()
ax = fig.add_axes([0.1, 0.1, 0.6, 0.75])
for type in types:    
    per_number_client = []

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

        op_count = 0
        
        for filename in filenames:
            type_file, number_clients, client_number, number_ops = util.get_parts(filename)
            
            if (type[0] != type_file) or number_clients != current_number_clients:
                continue
            op_count += number_ops

            df = pd.read_csv(filename)

            current_first_sec = df.head(1)["start_sec"].at[0]
            current_first_usec = df.head(1)["start_usec"].at[0]
            current_last_sec = df.tail(1)["end_sec"].at[len(df.index)-1]
            current_last_usec = df.tail(1)["end_usec"].at[len(df.index)-1]
            
            if current_first_sec < current_start_sec:
                current_start_sec = current_first_sec
                current_start_usec = current_first_usec
            elif current_first_sec == current_start_sec:
                if current_first_usec < current_start_usec:
                    current_start_usec = current_first_usec
                    
            if current_last_sec > current_end_sec:
                current_end_sec = current_last_sec
                current_end_usec = current_last_usec
            elif current_last_sec == current_end_sec:
                if current_last_usec > current_end_usec:
                    current_end_usec = current_last_usec

        time_taken_sec = util.calc_time_difference_sec(current_start_sec, current_start_usec, current_end_sec, current_end_usec)
        ops_per_sec = op_count / time_taken_sec
        if ops_per_sec > 0:
            print(type[0] + ', ' + str(current_number_clients) + ': ' + str(ops_per_sec))
            per_number_client.append(ops_per_sec)
        else:
            per_number_client.append(0)

        # per_number_client[current_number_clients-1] = ops_per_sec

        
    if per_number_client:
        plot_label = type[0]
        plt.plot(x_values, per_number_client, label=plot_label, marker=type[1], color=type[2])

if blocking_arg == "blocking":
    plot_title = "Overall throughput per Transport Type while Blocking for WC"
    filename_addition = "_blocking"
else:
    plot_title = "Overall throughput per Transport Type"
    filename_addition = ""

plt.title(plot_title)
plt.legend(loc="upper left", bbox_to_anchor=(1, 0.5))
plt.xlabel("Number of clients")
plt.ylabel("Throughput (ops/sec)")
plt.grid(linestyle='dotted')
graph_filename = f"../../benchmarking/graphs/Throughput_{max_clients}{filename_addition}.pdf"
plt.savefig(graph_filename, dpi=100, bbox_inches="tight")

     
