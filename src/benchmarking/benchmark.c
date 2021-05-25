//
// Created by jtroo on 26-04-21.
//

#include "benchmark.h"
#include "client/client.h"
#include "instance.h"
#include <sys/time.h>

enum connection_type connectionType;
int debug = 0;
int verbose = 0;
int num_ops = 1030;

void usage() {
    printf("Usage:\n");
    printf("RDMA KV Store Benchmark: [-a <server_addr>] [-p <server_port>] [-n <number of clients>] [-r <rc,uc,ud>] [-d] [-v]\n");
    printf("(default uses 1 client and server with IP 127.0.0.1, using tcp, and port is %d)\n", PORT);
    exit(1);
}

double process_ops_sec(struct timeval *start, struct timeval *end, unsigned int num_ops_in_time_frame) {
//    double time_taken_usec = ((end->tv_sec - start->tv_sec)*1000000.0+end->tv_usec) - start->tv_usec;
//    printf("Time in microseconds: %f microseconds\n", time_taken_usec);
    struct timeval *time_taken = malloc(sizeof(struct timeval));
    timersub(end, start, time_taken);

    double time_taken_sec = time_taken->tv_sec + time_taken->tv_usec/1000000.0;
    printf("Time taken in seconds: %f seconds\n", time_taken_sec);
    double ops_per_sec = 0.0;
    if (time_taken_sec > 0) {
        ops_per_sec = num_ops_in_time_frame / time_taken_sec;
    }
    else {
        printf("Something went wrong, time take sec: %ld usec: %ld\n", time_taken->tv_sec, time_taken->tv_usec);
    }

    return ops_per_sec;
}

int data_processing(struct operation *ops, int client_number) {
    int count = 0;
    struct operation first = ops[count];
    struct operation last = first;
    while (count < num_ops) {
        struct operation op = ops[count++];
        last = op;
    }

    double ops_sec = process_ops_sec(first.start, last.end, num_ops);
    printf("Client %d did %d operations at a speed of %f ops/sec\n", client_number, num_ops, ops_sec);

    return 0;
}

int main(int argc, char *argv[]) {
    int ret, clients = 1;
    struct sockaddr_in server_sockaddr;
    bzero(&server_sockaddr, sizeof server_sockaddr);
    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    connectionType = RC;

    const struct option long_options[] = {
            {"help", no_argument,       NULL, 'h'},
            {"port", required_argument, NULL, 'p'},
            {"clients",  required_argument, NULL, 'n'},
            {"rdma", required_argument, NULL, 'r'},
            {0, 0, 0,                         0}
    };

    for (;;) {
        int option_index = 0;
        int c;
        c = getopt_long(argc, argv, "a:p:n:r:hdv", long_options,
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


    pthread_t *threads = calloc(clients, sizeof(pthread_t));
    struct timeval start, end;

    gettimeofday(&start, NULL);
    for (int i=0; i<clients; i++) {
        struct thread_args args;
        args.conn_t = connectionType;
        args.server_addr = &server_sockaddr;
        args.num_ops = num_ops;
        args.instance_nr = i;

        ret = pthread_create(&threads[i], NULL, &start_instance, &args);
        if (ret != 0) {
            pr_info("pthread create failed %d\n", ret);
            exit(EXIT_FAILURE);
        }
        usleep(20000);
    }

    for (int i=0; i<clients; i++) {
        struct operation *ops;
        pthread_join(threads[i], (void**)&ops);

        if (i == clients-1) {
            gettimeofday(&end, NULL);
        }
        if (ops != NULL) {
            data_processing(ops, i);
        }
    }

    double tot_ops_sec = process_ops_sec(&start, &end, clients*num_ops);
    printf("In total %d clients did %d operations at a speed of %f ops/sec\n", clients, clients*num_ops, tot_ops_sec);

    sleep(1);
}
