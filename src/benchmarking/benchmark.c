//
// Created by jtroo on 26-04-21.
//

#include "benchmark.h"
#include "client/client.h"
#include "instance.h"

enum connection_type connectionType;
int debug = 0;
int verbose = 0;

void usage() {
    printf("Usage:\n");
    printf("RDMA KV Store Benchmark: [-a <server_addr>] [-p <server_port>] [-n <number of clients>] [-r <rc,uc,ud>] [-d] [-v]\n");
    printf("(default uses 1 client and server with IP 127.0.0.1, using tcp, and port is %d)\n", PORT);
    exit(1);
}


int main(int argc, char *argv[]) {
    int ret, clients = 1, num_ops = 1000;
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
    struct thread_args args;
    args.conn_t = connectionType;
    args.server_addr = &server_sockaddr;
    args.num_ops = num_ops;

    pthread_t *threads = calloc(clients, sizeof(pthread_t));
    for (int i=0; i<clients; i++) {
        ret = pthread_create(&threads[i], NULL, &start_instance, &args);
        if (ret != 0) {
            pr_info("pthread create failed %d\n", ret);
            exit(EXIT_FAILURE);
        }
    }

    for (int i=0; i<clients; i++) {
        struct operation **ops;
        printf("before?\n");
        pthread_join(threads[i], (void**)&ops);
        printf("pointer returned %p\n", ops);

        if (ops != NULL) {
            printf("request %p response %p expected response %p start %p end %p", ops[0]->request, ops[0]->response, ops[0]->expected_response, ops[0]->start, ops[0]->end);
            printf("%d\n", ops[0]->request->method);
//            printf("type %d msg len %zu\n", ops[0]->start, ops[0]->request->msg_len);
            print_request(ops[0]->request);
            print_response(ops[0]->response);
        }
    }
    sleep(1);
}