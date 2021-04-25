#ifndef RDMA_KV_STORE_COMMON_H
#define RDMA_KV_STORE_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

#define SERVER                          "EDU_OS_SERVER"

#define PORT        35304
#define MAXLINE     128
#define MSG_SIZE    4096
#define KEY_SIZE 256
#define DUMP_FILE   "dump.dat"

// Request protocol methods
enum method {
    UNK, SET, GET, DEL, PING, DUMP, RST, EXIT, SETOPT
};
enum connection_type {
    TCP, RC, UC, UD
};

static const struct {
    enum method val;
    const char *str;
} method_conversion[] = {
        {
                UNK,    "UNK"},
        {
                SET,    "SET"},
        {
                GET,    "GET"},
        {
                DEL,    "DEL"},
        {
                PING,   "PING"},
        {
                DUMP,   "DUMP"},
        {
                RST,    "RESET"},
        {
                EXIT,   "EXIT"},
        {
                SETOPT, "SETOPT"},};

// Error codes
#define RESPONSE_CODES(X)                   \
    X(0,    OK,            "OK")            \
    X(1,    KEY_ERROR,     "KEY_ERROR")     \
    X(2,    PARSING_ERROR, "PARSING_ERROR") \
    X(3,    STORE_ERROR,   "STORE_ERROR")   \
    X(4,    SETOPT_ERROR,  "SETOPT_ERROR")  \
    X(5,    UNK_ERROR,     "UNK_ERROR")

#define RESPONSE_ENUM(ID, NAME, TEXT) NAME = ID,
#define RESPONSE_TEXT(ID, NAME, TEXT) case ID: return TEXT;

enum response_code {
    RESPONSE_CODES(RESPONSE_ENUM)
};

// arguments
extern int verbose;
extern int debug;

struct request {
    enum method method;
    char key[KEY_SIZE];
    char msg[MSG_SIZE];
    size_t key_len;
    size_t msg_len;
    int connection_close;
};

struct response {
    enum response_code code;
    char msg[MSG_SIZE];
    size_t msg_len;
};

#if !defined(_GNU_SOURCE) || !defined(__GLIBC__) || __GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 30)

#include <sys/syscall.h>

static inline pid_t gettid(void) {
    return syscall(SYS_gettid);
}

#endif

#define error(fmt, ...) \
do { \
    if (verbose) \
        fprintf(stderr, "{%d} [%s:%d] " fmt "\n", gettid(), __FILE__, __LINE__, \
                ##__VA_ARGS__); \
} while (0)

#define pr_info(fmt, ...) \
do { \
    if (verbose) \
        fprintf(stderr, "{%d} [%s:%d] " fmt, gettid(), __FILE__, __LINE__, \
                ##__VA_ARGS__); \
} while (0)

#define pr_debug(fmt, ...) \
do { \
    if (debug) \
        fprintf(stderr, "{%d} [%s:%d] " fmt, gettid(), __FILE__, __LINE__, \
                ##__VA_ARGS__); \
} while (0)

#define check(bool, ret, msg, args...)  \
    if (bool) {                                   \
        error(msg, args);                \
        return ret;                      \
    }
struct request *allocate_request();

enum method method_to_enum(const char *str);

const char *method_to_str(enum method code);

void print_request(struct request *request);

#endif //RDMA_KV_STORE_COMMON_H
