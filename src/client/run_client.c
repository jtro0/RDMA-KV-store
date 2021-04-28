//
// Created by jtroo on 28-04-21.
//

#include "client.h"

enum connection_type connectionType;
int debug = 0;
int verbose = 0;

void usage() {
    printf("Usage:\n");
    printf("rdma_client: [-a <server_addr>] [-p <server_port>] -k <key> [-r <rc,uc,ud>] -d -v\n");
    printf("(default IP is 127.0.0.1, using tcp, and port is %d)\n", PORT);
    exit(1);
}
int main(int argc, char *argv[]) {

    int ret;
    struct sockaddr_in server_sockaddr;
    bzero(&server_sockaddr, sizeof server_sockaddr);
    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    char *key;
    connectionType = RC;

    const struct option long_options[] = {
            {"help", no_argument,       NULL, 'h'},
            {"port", required_argument, NULL, 'p'},
            {"key",  required_argument, NULL, 'k'},
            {"rdma", required_argument, NULL, 'r'},
            {0, 0, 0,                         0}
    };

    for (;;) {
        int option_index = 0;
        int c;
        c = getopt_long(argc, argv, "a:p:k:r:hdv", long_options,
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
            case 'k':
                key = calloc(1, strlen(optarg));
                //check error
                strncpy(key, optarg, strlen(optarg));
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

    if (key == NULL) {
        usage();
        exit(EXIT_FAILURE);
    }

    switch (connectionType) {

        case TCP:
            break;
        case RC:
            rc_main(key, &server_sockaddr);
            break;
        case UC:
            break;
        case UD:
            break;
    }

    return 0;
}