//
// Created by jtroo on 26-04-21.
//

#include "client/client.h"
#include "instance.h"
#include <sys/time.h>

enum connection_type connectionType = TCP;
int debug = 0;
int verbose = 0;
int num_ops = 10000000;
int override = 0;
int clients = 1;
int save = 1;
int blocking = 0;

void usage() {
    printf("Usage:\n");
    printf("RDMA KV Store Benchmark: [-a <server_addr>] [-p <server_port>] [-n <number of clients>] [-r <rc,uc,ud>] [-d] [-v] [-o <number of operations per client>] [-b] [--no-save]\n");
    printf("(default uses 1 client and server with IP 127.0.0.1, using tcp, and port is %d)\n", PORT);
    exit(1);
}

/*
 * Returns throughput in ops/sec between two time intervals, with a given number of operations completed
 */
double process_ops_sec(struct timeval *start, struct timeval *end, unsigned int num_ops_in_time_frame) {
    struct timeval *time_taken = malloc(sizeof(struct timeval));
    timersub(end, start, time_taken);

    double time_taken_sec = time_taken->tv_sec + time_taken->tv_usec / 1000000.0;
    printf("Time taken in seconds: %f seconds\n", time_taken_sec);
    double ops_per_sec = 0.0;
    if (time_taken_sec > 0) {
        ops_per_sec = num_ops_in_time_frame / time_taken_sec;
    } else {
        printf("Something went wrong, time take sec: %ld usec: %ld\n", time_taken->tv_sec, time_taken->tv_usec);
    }

    return ops_per_sec;
}

/*
 * Convert timeval to msec
 */
double time_in_msec(struct timeval *time) {
    return time->tv_sec * 1000.0 + time->tv_usec / 1000.0;
}

/*
 * Convert timeval to usec
 */
double time_in_usec(struct timeval *time) {
    return time->tv_sec * 1000000.0 + time->tv_usec;
}

/*
 * Process through the returned array from client instance. Save timeval's and latency in usec in correct file.
 */
int data_processing(struct operation *ops, int client_number) {
    int count = 0;
    struct operation first = ops[count];
    struct operation last = first;
    FILE *file = NULL;
    if (save) {
        char *file_name = malloc(255 * sizeof(char)); // Hard coded DAS-5 user name. This is symbolicly linked with scratch folder
        if (blocking)
            snprintf(file_name, 255, "./benchmarking/data/blocking/%s_%d_client_%d_%d.csv",
                     connection_type_to_str(connectionType), clients, client_number, num_ops / clients);
        else
            snprintf(file_name, 255, "./benchmarking/data/non_blocking/%s_%d_client_%d_%d.csv",
                    connection_type_to_str(connectionType), clients, client_number, num_ops / clients);
        char *suffix = &file_name[strlen(file_name) - 4];
        if (strncmp(".csv", suffix, 4) != 0) {
            fprintf(stderr, "File name is not correct!\n");
            exit(EXIT_FAILURE);
        }

        file = fopen(file_name, "w+");
        if (file == NULL) {
            fprintf(stderr, "Could not open file!\n");
            exit(EXIT_FAILURE);
        }
        fprintf(file, "start_sec,start_usec,end_sec,end_usec,latency\n"); // Columns for csv
    }
    while (count < num_ops / clients) {
        struct operation op = ops[count];
        if (op.start == NULL || op.end == NULL) {
            break;
        }
        struct timeval *time_taken = malloc(sizeof(struct timeval));
        timersub(op.end, op.start, time_taken);
        double latency = time_in_usec(time_taken);

        if (save) {
            // Save data in csv
            fprintf(file, "%ld,%ld,%ld,%ld,%f\n", op.start->tv_sec, op.start->tv_usec, op.end->tv_sec,
                    op.end->tv_usec, latency);
        }
        count++;
        last = op;
    }

    // Print out estimated throughput
    double ops_sec = process_ops_sec(first.start, last.end, count);
    printf("Client %d did %d operations at an estimated speed of %f ops/sec\n", client_number, count, ops_sec);

    if (save) {
        fclose(file);
    }
    return 0;
}

int main(int argc, char *argv[]) {
    int ret;
    struct sockaddr_in server_sockaddr;
    bzero(&server_sockaddr, sizeof server_sockaddr);
    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    const struct option long_options[] = {
            {"help",     no_argument,       NULL, 'h'},
            {"port",     required_argument, NULL, 'p'},
            {"clients",  required_argument, NULL, 'n'},
            {"rdma",     required_argument, NULL, 'r'},
            {"ops",      required_argument, NULL, 'o'},
            {"no-save",  no_argument,       NULL, 's'},
            {"blocking", no_argument,       NULL, 'b'},
            {0, 0, 0,                             0}
    };

    for (;;) {
        int option_index = 0;
        int c;
        c = getopt_long(argc, argv, "a:p:n:r:hdvobs", long_options,
                        &option_index);
        if (c == -1)
            break;

        switch (c) {
            case 'a':
                /* remember, this overwrites the port info */
                ret = get_addr(optarg, (struct sockaddr *) &server_sockaddr);
                if (ret) {
                    error("Invalid IP \n");
                    return ret;
                }
                break;
            case 'p':
                /* passed port to listen on */
                server_sockaddr.sin_port = htons(strtol(optarg, NULL, 0));
                break;
            case 'n':
                clients = strtol(optarg, NULL, 0);
                break;
            case 'r':
                if (strncmp(optarg, "rc", 2) == 0) {
                    connectionType = RC;
                    break;
                } else if (strncmp(optarg, "uc", 2) == 0) {
                    connectionType = UC;
                    break;
                } else if (strncmp(optarg, "ud", 2) == 0) {
                    connectionType = UD;
                    break;
                }
                break;
            case 'v':
                verbose = 1;
                break;
            case 'd':
                debug = 1;
                break;
            case 'o':
                num_ops = strtol(optarg, NULL, 0);
                break;
            case 's':
                save = 0;
                break;
            case 'b':
                blocking = 1;
                break;
            case 'h':
            default:
                usage();
                exit(EXIT_SUCCESS);
        }
    }

    if (!server_sockaddr.sin_port) {
        /* no port provided, use the default port */
        server_sockaddr.sin_port = htons(PORT);
    }


    pthread_t *threads = calloc(clients, sizeof(pthread_t)); // Allocate array of threads
    struct timeval start, end;

    gettimeofday(&start, NULL); // start overall timer
    for (int i = 0; i < clients; i++) {
        // Pass needed arguments
        struct thread_args args;
        args.conn_t = connectionType;
        args.server_addr = &server_sockaddr;
        args.num_ops = num_ops / clients;
        if (override)
            args.instance_nr = 1;
        else
            args.instance_nr = i;
        args.blocking = blocking;

        ret = pthread_create(&threads[i], NULL, &start_instance, &args); // Create client thread
        if (ret != 0) {
            pr_info("pthread create failed %d\n", ret);
            exit(EXIT_FAILURE);
        }
        usleep(100000); // Sleep for 0.1 sec, this is to not interfere with connection establishments
    }

    for (int i = 0; i < clients; i++) {
        struct operation *ops;
        pthread_join(threads[i], (void **) &ops); // Wait until thread is done

        if (i == clients - 1) {
            gettimeofday(&end, NULL); // If it was last thread, stop the timer
        }
        if (ops != NULL) {
            data_processing(ops, i); // Process/save individual client performance
        }
    }

    double tot_ops_sec = process_ops_sec(&start, &end, num_ops);
    printf("In total %d clients did %d operations at an estimated speed of %f ops/sec\n", clients, num_ops, tot_ops_sec);

    sleep(1);
}
