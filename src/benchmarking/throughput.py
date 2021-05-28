import util
import sys
import pandas as pd
import matplotlib.pyplot as plt


filenames = util.get_all_csv()

types = ["TCP", "RC", "UC", "UD"]


max_clients = int(sys.argv[1])

for type in types:    
    per_number_client = []

    x_values = list(range(1, 11))
    if max_clients > 10:
        x_values = x_values + list(range(15, max_clients, 5))


    for current_number_clients in x_values:
        current_start_sec = sys.maxsize
        current_start_usec = sys.maxsize
        
        current_end_sec = 0
        current_end_usec = 0
        
        for filename in filenames:
            type_file, number_clients, client_number, number_ops = util.get_parts(filename)
            
            if (type != type_file) or number_clients != current_number_clients:
                continue

            df = pd.read_csv(filename)
                    
            current_first_sec = df.head(1)["start_sec"].at[0]
            current_first_usec = df.head(1)[" start_usec"].at[0]
            current_last_sec = df.tail(1)[" end_sec"].at[len(df.index)-1]
            current_last_usec = df.tail(1)[" end_usec"].at[len(df.index)-1]
            
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
        ops_per_sec = (number_ops*current_number_clients) / time_taken_sec
        print(ops_per_sec)
        if ops_per_sec > 0:
            per_number_client.append(ops_per_sec)
        # per_number_client[current_number_clients-1] = ops_per_sec

        
    if per_number_client:
        plot_label = type
        plt.plot(x_values, per_number_client, label=plot_label)
    
plt.title("Overall throughput per Transport Type")
plt.legend(loc="upper left", bbox_to_anchor=(1, 0.5))
plt.xlabel("Number of clients")
plt.ylabel("Throughput (ops/sec)")

graph_filename = "../../benchmarking/graphs/Throughput_%d.png" % max_clients
plt.savefig(graph_filename, dpi=100)

     