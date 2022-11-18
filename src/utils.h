#ifndef utils_h
#define utils_h

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <inttypes.h>

#ifdef _WIN32
    #include <windows.h>
    #ifdef _MSC_VER
         #include <intrin.h>
         #include "stdint_win32.h"
    #endif
    #include "system_info.h"
#else
    #define __USE_GNU
    #include <fcntl.h>
    #include <errno.h>
    #include <malloc.h>
    #include <pthread.h>
    #include <sys/time.h>
    #include <semaphore.h>
    #include <sys/syscall.h>
    #include <sys/types.h>
#endif

#include "khash.h"

#ifdef __cplusplus
extern "C" {
#endif

KHASH_MAP_INIT_STR(text, void*)

#define Hash kh_text_t

kh_text_t *Hash_New(int initial_size);
int        Hash_Add(khash_t(text) *h, const char *key, void *value);
void       Hash_Free(khash_t(text) *h);
int        Hash_SoftAdd(khash_t(text) *h, const char *key, void *value);
void      *Hash_Find(khash_t(text) *h, const char *key);
void       Hash_Delete(khash_t(text) *h, const char *key);

#ifdef __cplusplus
}
#endif

#endif
