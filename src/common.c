#include "common.h"

struct request *allocate_request()
{
    struct request *r = malloc(sizeof(struct request));
    if (r == NULL) {
        pr_debug("error in memory allocation");
        return NULL;
    }
    r->key = malloc(KEY_SIZE);

    return r;
}
