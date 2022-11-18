#include <stdarg.h>
#include "utils.h"

kh_text_t *Hash_New(int initial_size) {
    return kh_init(text);
}

void Hash_Free(khash_t(text) *h) {
    kh_destroy(text, h);
}

int Hash_Add(khash_t(text) *h, const char *key, void *value) {
    int ret;
    khiter_t k=0;
    {
        k = kh_put(text, h, key, &ret);
        kh_value(h, k) = value;
    }
    return k;
}

int Hash_SoftAdd(khash_t(text) *h, const char *key, void *value) {
    int ret;
    khiter_t k=0;

    {
        k = kh_put(text, h, key, &ret);
        kh_value(h, k) = value;
    }

    return k;
}

void *Hash_Find(khash_t(text) *h, const char *key) {

    khiter_t k=0;
    void *value;

    {
        k = kh_get(text, h, key);
        if (k!= kh_end(h)) {
            value=kh_value(h, k);
        }
        else {
            value=NULL;
        }
    }
    return value;
}

void Hash_Delete(khash_t(text) *h, const char *key) {

    khiter_t k=0;
    
    {
        k = kh_get(text, h, key);
        kh_del(text, h, k);
    }
}
