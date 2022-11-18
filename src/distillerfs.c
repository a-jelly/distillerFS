/*****************************************************************************
 * Author:   Remi Flament <remipouak at gmail dot com>
 *           Andrew Jelly <ajelly at gmail dot com>
 *****************************************************************************
 * Copyright (c) 2005 - 2022, Remi Flament and contributors
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0

 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 */

#ifdef linux
/* For pread()/pwrite() */
#define _X_SOURCE 500
#endif

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/statfs.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif
#include <stdarg.h>
#include <getopt.h>
#include <sys/time.h>
#include <pwd.h>
#include <grp.h>

#include "utils.h"
#include "toml.h"

#define QN_FLAGS                26     //  Symbols:   GArdKMSuDNLmoTnsORWteFXxlv

#define LOG_SUCCESS   1
#define LOG_UNSUCCESS 2

enum FUSE_OPS {
    OP_GETATTR,
    OP_ACCESS,
    OP_READLINK,
    OP_READDIR,
    OP_MKNOD,
    OP_MKDIR,
    OP_SYMLINK,
    OP_UNLINK,
    OP_RMDIR,
    OP_RENAME,
    OP_LINK,
    OP_CHMOD,
    OP_CHOWN,
    OP_TRUNCATE,
    OP_UTIME,
    OP_UTIMENS,
    OP_OPEN,
    OP_READ,
    OP_WRITE,
    OP_STATFS,
    OP_RELEASE,
    OP_FSYNC,
    OP_SETXATTR,
    OP_GETXATTR,
    OP_LISTXATTR,
    OP_REMOVEXATTR
};

#define FLAG_GETATTR        (1<<OP_GETATTR)     //  G
#define FLAG_ACCESS         (1<<OP_ACCESS)      //  A
#define FLAG_READLINK       (1<<OP_READLINK)    //  r
#define FLAG_READDIR        (1<<OP_READDIR)     //  d
#define FLAG_MKNOD          (1<<OP_MKNOD)       //  K
#define FLAG_MKDIR          (1<<OP_MKDIR)       //  M
#define FLAG_SYMLINK        (1<<OP_SYMLINK)     //  S
#define FLAG_UNLINK         (1<<OP_UNLINK)      //  u
#define FLAG_RMDIR          (1<<OP_RMDIR)       //  D
#define FLAG_RENAME         (1<<OP_RENAME)      //  N
#define FLAG_LINK           (1<<OP_LINK)        //  L
#define FLAG_CHMOD          (1<<OP_CHMOD)       //  m
#define FLAG_CHOWN          (1<<OP_CHOWN)       //  o
#define FLAG_TRUNCATE       (1<<OP_TRUNCATE)    //  T
#define FLAG_UTIME          (1<<OP_UTIME)       //  n
#define FLAG_UTIMENS        (1<<OP_UTIMENS)     //  s
#define FLAG_OPEN           (1<<OP_OPEN)        //  O
#define FLAG_READ           (1<<OP_READ)        //  R
#define FLAG_WRITE          (1<<OP_WRITE)       //  W
#define FLAG_STATFS         (1<<OP_STATFS)      //  t
#define FLAG_RELEASE        (1<<OP_RELEASE)     //  e
#define FLAG_FSYNC          (1<<OP_FSYNC)       //  F
#define FLAG_SETXATTR       (1<<OP_SETXATTR)    //  X
#define FLAG_GETXATTR       (1<<OP_GETXATTR)    //  x
#define FLAG_LISTXATTR      (1<<OP_LISTXATTR)   //  l
#define FLAG_REMOVEXATTR    (1<<OP_REMOVEXATTR) //  v

const char *op_names[] = {
    "getattr",   "access",   "readlink", "readdir",
    "mknod",     "mkdir",    "symlink",  "unlink",
    "rmdir",     "rename",   "link",     "chmod",
    "chown",     "truncate", "utime",    "utimens",
    "open",      "read",     "write",    "statfs",
    "release",   "fsync",    "setxattr", "getxattr",
    "listxattr", "removexattr"
};

int op_flags[QN_FLAGS];

#define PUSHARG(ARG)                      \
    assert(out->fuseArgc < MaxFuseArgs); \
    out->fuseArgv[out->fuseArgc++] = ARG

// We need the "nonempty" option to mount the directory in recent FUSE's
// because it's non empty and contains the files that will be logged.
//
// We need "use_ino" so the files will use their original inode numbers,
// instead of all getting 0xFFFFFFFF . For example, this is required for
// logging the ~/.kde/share/config directory, in which hard links for lock
// files are verified by their inode equivalency.
#define COMMON_OPTS "nonempty,use_ino,attr_timeout=0,entry_timeout=0,negative_timeout=0"

static int savefd;
static const char *loggerId = "default";

FILE *hash_log;
static Hash *h;
static pthread_mutex_t prmutex = PTHREAD_MUTEX_INITIALIZER;

const char symbols[26]={'G','A','r','d','K','M','S','u','D','N','L','m','o','T','n','s','O','R','W','t','e','F','X','x','l','v'};

#define MaxFuseArgs 32

typedef struct LoggedFS_Args {
    char *mountPoint; // where the users read files
    char *configFilename;
    char *logFilename;
    int isDaemon; // true == spawn in background, log to syslog except if log file parameter is set
    int logToSyslog;
    const char *fuseArgv[MaxFuseArgs];
    int fuseArgc;
} LoggedFS_Args;

typedef struct lfs_count {
    char *path;
    int   count;
    int   flags;
} lfs_count_t;

typedef struct filter_desc {
    char **exclude_path;
    int    exclude_path_count;
    char **include_path;
    int    include_path_count;
} filter_desc_t;

static LoggedFS_Args *loggedfsArgs;
static filter_desc_t *g_filter;

static int is_Absolute_Path(const char *fileName)
{
    if (fileName && fileName[0] != '\0' && fileName[0] == '/') {
        return 1;
    }
    else {
        return 0;
    }
}

static int is_excluded(const char *path, char **exc_path, int qn_exc_path) {
    for (int i=0;i<qn_exc_path;i++) {
        if (strncmp(path, exc_path[i], strlen(exc_path[i]))==0) {
            return 1;
        }
    }
    return 0;
}

static int is_included(const char *path, char **inc_path, int qn_inc_path) {
    if (qn_inc_path==0) {
        // Empty include path equal "all included"
        return 1;
    }
    for (int i=0;i<qn_inc_path;i++) {
        if (strncmp(path, inc_path[i], strlen(inc_path[i]))==0) {
            return 1;
        }
    }
    return 0;
}


int Store_In_Hash(Hash *log_hash, filter_desc_t *filter, const char *path, int flag) {

    lfs_count_t *item;
    int rc = 0;

    if (path==NULL) {
        return 0;
    }

    if (is_included(path, filter->include_path, filter->include_path_count)!=1) {
        return 0;
    }

    if (is_excluded(path, filter->exclude_path, filter->exclude_path_count)==1) {
        return 0;
    }

    pthread_mutex_lock(&prmutex);                // Hash function is not reentrant

    item = Hash_Find(log_hash, path);
    if (item==NULL) {
        item = malloc(sizeof(lfs_count_t));
        item->count = 1;
        item->path = strdup(path);
        item->flags = flag;
        Hash_Add(log_hash, item->path, item);
    }
    else {
        item->count++;
        item->flags = item->flags|flag;
    }
    rc = item->count;
    pthread_mutex_unlock(&prmutex);

    return rc;
}

void Free_Hash(Hash *h) {

    int i=0;
    for (int k = 0; k < kh_end(h); ++k) {
        if (kh_exist(h, k)) {
            lfs_count_t *item =(lfs_count_t *) kh_value(h, k);
            if (item!=NULL && item->path!=NULL) {
                free(item->path);
                free(item);
            }
            i++;
        }
    }
    Hash_Free(h);
}

void Print_Hash(FILE *dest, Hash *h) {

    const char* k;
    lfs_count_t *v;
    char mask[27];

    if (h==NULL) {
        fprintf(dest, "!!! Empty hash!!!\n");
        return;
    }

    kh_foreach(h, k, v, {
        for (int i=0;i<QN_FLAGS;i++) {
            int cur_flag=1<<i;
            if ((v->flags&cur_flag)!=0) {
                mask[i]=symbols[i];
            }
            else {
                mask[i]='.';
            }
        }
        mask[26]=0;

        fprintf(dest, "[%s]:%010d:%s\n", mask, v->count, k);
    });
    return;
}

int should_log(int fuse_op, int state) {
    if ((op_flags[fuse_op] & state)!=0) {
        return 1;
    }
    else {
        return 0;
    }
}

static char *getRelativePath(const char *path) {
    if (path[0] == '/') {
        if (strlen(path) == 1) {
            return strdup(".");
        }
        const char *substr = &path[1];
        return strdup(substr);
    }
    return strdup(path);
}

static void *loggedFS_init(struct fuse_conn_info *info) {
    fchdir(savefd);
    close(savefd);
    return NULL;
}

static int loggedFS_getattr(const char *orig_path, struct stat *stbuf) {
    int res;

    char *path = getRelativePath(orig_path);
    res = lstat(path, stbuf);
    free(path);
    if (res == -1) {
        if (should_log(OP_GETATTR, LOG_UNSUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_GETATTR);
        }
        return -errno;
    }
    else {
        if (should_log(OP_GETATTR, LOG_SUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_GETATTR);
        }
    }

    return 0;
}

static int loggedFS_access(const char *orig_path, int mask) {
    int res;

    char *path = getRelativePath(orig_path);
    res = access(path, mask);
    free(path);
    if (res == -1) {
        if (should_log(OP_ACCESS, LOG_UNSUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_ACCESS);
        }
        return -errno;
    }
    else {
        if (should_log(OP_ACCESS, LOG_SUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_ACCESS);
        }
    }

    return 0;
}

static int loggedFS_readlink(const char *orig_path, char *buf, size_t size) {
    int res;

    char *path = getRelativePath(orig_path);
    res = readlink(path, buf, size - 1);
    free(path);

    if (res == -1) {
        if (should_log(OP_READLINK, LOG_UNSUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_READLINK);
        }
        return -errno;
    }
    else {
        if (should_log(OP_READLINK, LOG_SUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_READLINK);
        }
    }
    buf[res] = '\0';

    return 0;
}

static int loggedFS_readdir(const char *orig_path, void *buf, fuse_fill_dir_t filler,
                            off_t offset, struct fuse_file_info *fi) {
    DIR *dp;
    struct dirent *de;
    int res;

    (void)offset;
    (void)fi;

    char *path = getRelativePath(orig_path);
    dp = opendir(path);
    if (dp == NULL) {
        res = -errno;
        free(path);
        if (should_log(OP_READDIR, LOG_UNSUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_READDIR);
        }
        return res;
    }

    while ((de = readdir(dp)) != NULL) {
        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino = de->d_ino;
        st.st_mode = de->d_type << 12;
        if (filler(buf, de->d_name, &st, 0)) {
            break;
        }
    }
    closedir(dp);
    free(path);

    if (should_log(OP_READDIR, LOG_SUCCESS) == 1) {
        Store_In_Hash(h, g_filter, orig_path, FLAG_READDIR);
    }

    return 0;
}

static int loggedFS_mknod(const char *orig_path, mode_t mode, dev_t rdev) {
    int res;
    char *path = getRelativePath(orig_path);

    if (S_ISREG(mode)) {
        res = open(path, O_CREAT | O_EXCL | O_WRONLY, mode);
        if (res >= 0) {
            res = close(res);
        }
    }
    else if (S_ISFIFO(mode)) {
        res = mkfifo(path, mode);
    }
    else {
        res = mknod(path, mode, rdev);
    }

    if (res == -1) {
        free(path);
        if (should_log(OP_MKNOD, LOG_UNSUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_MKNOD);
        }
        return -errno;
    }
    else {
        lchown(path, fuse_get_context()->uid, fuse_get_context()->gid);
    }
    free(path);

    if (should_log(OP_MKNOD, LOG_SUCCESS) == 1) {
        Store_In_Hash(h, g_filter, orig_path, FLAG_MKNOD);
    }

    return 0;
}

static int loggedFS_mkdir(const char *orig_path, mode_t mode) {
    int res;
    char *path = getRelativePath(orig_path);
    res = mkdir(path, mode);
    if (res == -1) {
        free(path);
        if (should_log(OP_MKDIR, LOG_UNSUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_MKDIR);
        }
        return -errno;
    }
    else {
        lchown(path, fuse_get_context()->uid, fuse_get_context()->gid);
    }
    free(path);

    if (should_log(OP_MKDIR, LOG_SUCCESS) == 1) {
        Store_In_Hash(h, g_filter, orig_path, FLAG_MKDIR);
    }

    return 0;
}

static int loggedFS_unlink(const char *orig_path) {
    int res;

    char *path = getRelativePath(orig_path);
    res = unlink(path);
    free(path);

    if (res == -1) {
        if (should_log(OP_UNLINK, LOG_UNSUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_UNLINK);
        }
        return -errno;
    }
    else {
        if (should_log(OP_UNLINK, LOG_SUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_UNLINK);
        }
    }

    return 0;
}

static int loggedFS_rmdir(const char *orig_path)
{
    int res;
    char *path = getRelativePath(orig_path);
    res = rmdir(path);
    free(path);

    if (res == -1) {
        if (should_log(OP_RMDIR, LOG_UNSUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_RMDIR);
        }
        return -errno;
    }
    else {
        if (should_log(OP_RMDIR, LOG_SUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_RMDIR);
        }
    }
    return 0;
}

static int loggedFS_symlink(const char *from, const char *orig_to) {
    int res;

    char *to = getRelativePath(orig_to);
    res = symlink(from, to);

    if (res == -1) {
        free(to);
        if (should_log(OP_SYMLINK, LOG_UNSUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_to, FLAG_SYMLINK);
            Store_In_Hash(h, g_filter, from, FLAG_SYMLINK);
        }
        return -errno;
    }
    else {
        lchown(to, fuse_get_context()->uid, fuse_get_context()->gid);
        if (should_log(OP_SYMLINK, LOG_SUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_to, FLAG_SYMLINK);
            Store_In_Hash(h, g_filter, from, FLAG_SYMLINK);
        }
    }

    free(to);
    return 0;
}

static int loggedFS_rename(const char *orig_from, const char *orig_to) {
    int res;

    char *from = getRelativePath(orig_from);
    char *to = getRelativePath(orig_to);
    res = rename(from, to);

    free(from);
    free(to);

    if (res == -1) {
        if (should_log(OP_RENAME, LOG_UNSUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_from, FLAG_RENAME);
            Store_In_Hash(h, g_filter, orig_to, FLAG_RENAME);
        }
        return -errno;
    }
    else {
        if (should_log(OP_RENAME, LOG_SUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_from, FLAG_RENAME);
            Store_In_Hash(h, g_filter, orig_to, FLAG_RENAME);
        }
    }

    return 0;
}

static int loggedFS_link(const char *orig_from, const char *orig_to) {
    int res;

    char *from = getRelativePath(orig_from);
    char *to = getRelativePath(orig_to);

    res = link(from, to);
    free(from);

    if (res == -1) {
        free(to);
        if (should_log(OP_LINK, LOG_UNSUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_from, FLAG_LINK);
            Store_In_Hash(h, g_filter, orig_to, FLAG_LINK);
        }
        return -errno;
    }
    else {
        lchown(to, fuse_get_context()->uid, fuse_get_context()->gid);
        if (should_log(OP_LINK, LOG_SUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_from, FLAG_LINK);
            Store_In_Hash(h, g_filter, orig_to, FLAG_LINK);
        }
    }

    free(to);

    return 0;
}

static int loggedFS_chmod(const char *orig_path, mode_t mode) {
    int res;

    char *path = getRelativePath(orig_path);
    res = chmod(path, mode);
    free(path);

    if (res == -1) {
        if (should_log(OP_CHMOD, LOG_UNSUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_CHMOD);
        }
        return -errno;
    }
    else {
        if (should_log(OP_CHMOD, LOG_SUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_CHMOD);
        }
    }

    return 0;
}

static int loggedFS_chown(const char *orig_path, uid_t uid, gid_t gid) {
    int res;

    char *path = getRelativePath(orig_path);
    res = lchown(path, uid, gid);
    free(path);

    if (res == -1) {
        if (should_log(OP_CHOWN, LOG_UNSUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_CHOWN);
        }
        return -errno;
    }
    else {
        if (should_log(OP_CHOWN, LOG_SUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_CHOWN);
        }
    }

    return 0;
}

static int loggedFS_truncate(const char *orig_path, off_t size) {
    int res;

    char *path = getRelativePath(orig_path);
    res = truncate(path, size);
    free(path);

    if (res == -1) {
        if (should_log(OP_TRUNCATE, LOG_UNSUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_TRUNCATE);
        }
        return -errno;
    }
    else {
        if (should_log(OP_TRUNCATE, LOG_SUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_TRUNCATE);
        }
    }

    return 0;
}

#if (FUSE_USE_VERSION == 25)
static int loggedFS_utime(const char *orig_path, struct utimbuf *buf) {
    int res;
    char *path = getRelativePath(orig_path);
    res = utime(path, buf);
    free(path);

    if (res == -1) {
        if (should_log(OP_UTIME, LOG_UNSUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_UTIME);
        }
        return -errno;
    }
    else {
        if (should_log(OP_UTIME, LOG_SUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_UTIME);
        }
    }

    return 0;
}

#else

static int loggedFS_utimens(const char *orig_path, const struct timespec ts[2]) {
    int res;

    char *path = getRelativePath(orig_path);
    res = utimensat(AT_FDCWD, path, ts, AT_SYMLINK_NOFOLLOW);
    free(path);

    if (res == -1) {
        if (should_log(OP_UTIMENS, LOG_UNSUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_UTIMENS);
        }
        return -errno;
    }
    else {
        if (should_log(OP_UTIMENS, LOG_SUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_UTIMENS);
        }
    }
    return 0;
}

#endif

static int loggedFS_open(const char *orig_path, struct fuse_file_info *fi) {
    int res;
    char *path = getRelativePath(orig_path);
    res = open(path, fi->flags);
    free(path);

    if (res == -1) {
        if (should_log(OP_OPEN, LOG_UNSUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_OPEN);
        }
        return -errno;
    }
    else {
        if (should_log(OP_OPEN, LOG_SUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_OPEN);
        }
    }

    fi->fh = res;
    return 0;
}

static int loggedFS_read(const char *orig_path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {

    int res;
    res = pread(fi->fh, buf, size, offset);

    if (res == -1) {
        if (should_log(OP_READ, LOG_UNSUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_READ);
        }
        res = -errno;
    }
    else {
        if (should_log(OP_READ, LOG_SUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_READ);
        }
    }

    return res;
}

static int loggedFS_write(const char *orig_path, const char *buf, size_t size,
                          off_t offset, struct fuse_file_info *fi) {
    int fd;
    int res;

    char *path = getRelativePath(orig_path);
    (void)fi;

    fd = open(path, O_WRONLY);
    if (fd == -1) {
        if (should_log(OP_WRITE, LOG_UNSUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_WRITE);
        }
        res = -errno;
        return res;
    }

    res = pwrite(fd, buf, size, offset);
    if (res == -1) {
        res = -errno;
        if (should_log(OP_WRITE, LOG_UNSUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_WRITE);
        }
    }
    else {
        if (should_log(OP_WRITE, LOG_SUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_WRITE);
        }
    }

    close(fd);
    free(path);

    return res;
}

static int loggedFS_statfs(const char *orig_path, struct statvfs *stbuf) {
    int res;

    char *path = getRelativePath(orig_path);
    res = statvfs(path, stbuf);
    free(path);
    if (res == -1) {
        if (should_log(OP_STATFS, LOG_UNSUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_STATFS);
        }
        return -errno;
    }
    else {
        if (should_log(OP_STATFS, LOG_SUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_STATFS);
        }
    }

    return 0;
}

static int loggedFS_release(const char *orig_path, struct fuse_file_info *fi) {

    (void)orig_path;
    Store_In_Hash(h, g_filter, orig_path, FLAG_RELEASE);
    close(fi->fh);
    return 0;
}

static int loggedFS_fsync(const char *orig_path, int isdatasync,
                          struct fuse_file_info *fi) {
    (void)orig_path;
    (void)isdatasync;
    (void)fi;

    Store_In_Hash(h, g_filter, orig_path, FLAG_FSYNC);

    return 0;
}

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int loggedFS_setxattr(const char *orig_path, const char *name, const char *value,
                             size_t size, int flags) {
    int res = lsetxattr(orig_path, name, value, size, flags);

    if (res == -1) {
        if (should_log(OP_SETXATTR, LOG_UNSUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_SETXATTR);
        }
        return -errno;
    }
    else {
        if (should_log(OP_SETXATTR, LOG_SUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_SETXATTR);
        }
    }
    return 0;
}

static int loggedFS_getxattr(const char *orig_path, const char *name, char *value,
                             size_t size) {
    int res = lgetxattr(orig_path, name, value, size);
    if (res == -1) {
        if (should_log(OP_GETXATTR, LOG_UNSUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_GETXATTR);
        }
        return -errno;
    }
    else {
        if (should_log(OP_GETXATTR, LOG_SUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_GETXATTR);
        }
    }
    return res;
}

static int loggedFS_listxattr(const char *orig_path, char *list, size_t size) {
    int res = llistxattr(orig_path, list, size);

    if (res == -1) {
        if (should_log(OP_LISTXATTR, LOG_UNSUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_LISTXATTR);
        }
        return -errno;
    }
    else {
        if (should_log(OP_LISTXATTR, LOG_SUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_LISTXATTR);
        }
    }
    return res;
}

static int loggedFS_removexattr(const char *orig_path, const char *name) {
    int res = lremovexattr(orig_path, name);
    if (res == -1) {
        if (should_log(OP_REMOVEXATTR, LOG_UNSUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_REMOVEXATTR);
        }

        return -errno;
    }
    else {
        if (should_log(OP_REMOVEXATTR, LOG_SUCCESS) == 1) {
            Store_In_Hash(h, g_filter, orig_path, FLAG_REMOVEXATTR);
        }
    }
    return 0;
}
#endif /* HAVE_SETXATTR */

static void usage(char *name)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "%s [-h] | [-l log-file] [-c config-file] [-f] [-p] [-e] /directory-mountpoint\n", name);
    fprintf(stderr, "Type 'man loggedfs' for more details\n");
    return;
}

static int processArgs(int argc, char *argv[], LoggedFS_Args *out)
{
    // set defaults
    out->isDaemon = 1;
    out->logToSyslog = 1;

    out->fuseArgc = 0;
    out->configFilename = NULL;

    // pass executable name through
    out->fuseArgv[0] = argv[0];
    ++out->fuseArgc;

    // leave a space for mount point, as FUSE expects the mount point before
    // any flags
    out->fuseArgv[1] = NULL;
    ++out->fuseArgc;
    opterr = 0;

    int res;

    int got_p = 0;

    while ((res = getopt(argc, argv, "hpfec:l:")) != -1) {
        switch (res)
        {
        case 'h':
            usage(argv[0]);
            return 0;
        case 'f':
            out->isDaemon = 0;
            out->logToSyslog = 0;
            // this option was added in fuse 2.x
            PUSHARG("-f");
            fprintf(stderr,"LoggedFS not running as a daemon\n");
            break;
        case 'p':
            PUSHARG("-o");
            PUSHARG("allow_other,default_permissions," COMMON_OPTS);
            got_p = 1;
            fprintf(stderr,"LoggedFS running as a public filesystem\n");
            break;
        case 'e':
            PUSHARG("-o");
            PUSHARG("nonempty");
            fprintf(stderr,"Using existing directory\n");
            break;
        case 'c':
            out->configFilename = optarg;
            fprintf(stderr,"Configuration file : %s\n", optarg);
            break;
        case 'l':
        {
            fprintf(stderr,"LoggedFS log file : %s, no syslog logs\n", optarg);
            out->logToSyslog = 0;
            out->logFilename = optarg;
            break;
        }
        default:
            break;
        }
    }

    if (!got_p) {
        PUSHARG("-o");
        PUSHARG(COMMON_OPTS);
    }
#undef COMMON_OPTS

    if (optind + 1 <= argc) {
        out->mountPoint = argv[optind++];
        out->fuseArgv[1] = out->mountPoint;
    }
    else {
        fprintf(stderr, "Missing mountpoint\n");
        usage(argv[0]);
        return 0;
    }

    // If there are still extra unparsed arguments, pass them onto FUSE..
    if (optind < argc) {
        assert(out->fuseArgc < MaxFuseArgs);

        while (optind < argc) {
            assert(out->fuseArgc < MaxFuseArgs);
            out->fuseArgv[out->fuseArgc++] = argv[optind];
            ++optind;
        }
    }

    if (!is_Absolute_Path(out->mountPoint)) {
        fprintf(stderr, "You must use absolute paths "
                        "(beginning with '/') for %s\n",
                out->mountPoint);
        return 0;
    }
    return 1;
}

void init_fuse_oper(struct fuse_operations *loggedFS_oper) {
    // in case this code is compiled against a newer FUSE library and new
    // members have been added to fuse_operations, make sure they get set to
    // 0..
    memset(loggedFS_oper, 0, sizeof(struct fuse_operations));
    loggedFS_oper->init = loggedFS_init;
    loggedFS_oper->getattr = loggedFS_getattr;
    loggedFS_oper->access = loggedFS_access;
    loggedFS_oper->readlink = loggedFS_readlink;
    loggedFS_oper->readdir = loggedFS_readdir;
    loggedFS_oper->mknod = loggedFS_mknod;
    loggedFS_oper->mkdir = loggedFS_mkdir;
    loggedFS_oper->symlink = loggedFS_symlink;
    loggedFS_oper->unlink = loggedFS_unlink;
    loggedFS_oper->rmdir = loggedFS_rmdir;
    loggedFS_oper->rename = loggedFS_rename;
    loggedFS_oper->link = loggedFS_link;
    loggedFS_oper->chmod = loggedFS_chmod;
    loggedFS_oper->chown = loggedFS_chown;
    loggedFS_oper->truncate = loggedFS_truncate;
#if (FUSE_USE_VERSION == 25)
    loggedFS_oper->utime = loggedFS_utime;
#else
    loggedFS_oper->utimens = loggedFS_utimens;
    loggedFS_oper->flag_utime_omit_ok = 1;
#endif
    loggedFS_oper->open = loggedFS_open;
    loggedFS_oper->read = loggedFS_read;
    loggedFS_oper->write = loggedFS_write;
    loggedFS_oper->statfs = loggedFS_statfs;
    loggedFS_oper->release = loggedFS_release;
    loggedFS_oper->fsync = loggedFS_fsync;
#ifdef HAVE_SETXATTR
    loggedFS_oper->setxattr = loggedFS_setxattr;
    loggedFS_oper->getxattr = loggedFS_getxattr;
    loggedFS_oper->listxattr = loggedFS_listxattr;
    loggedFS_oper->removexattr = loggedFS_removexattr;
#endif
}

int parse_config(const char *config_file) {
    int rc=0;
    FILE* fp;
    char errbuf[256];

    fp = fopen(config_file, "r");
    if (!fp) {
        fprintf(stderr, "Config file not found!\n");
        return rc;
    }

    toml_table_t* conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
    toml_table_t* exclude = toml_table_in(conf, "exclude");

    if (exclude!=NULL) {
         toml_array_t* path_array = toml_array_in(exclude, "paths");
         if (path_array!=NULL) {
             g_filter->exclude_path_count = toml_array_nelem(path_array);
             if (g_filter->exclude_path_count>0) {
                 g_filter->exclude_path=malloc(g_filter->exclude_path_count*sizeof(char*));
                 for (int i = 0; i<g_filter->exclude_path_count; i++) {
                     toml_datum_t path = toml_string_at(path_array, i);
                     if (path.ok>0) {
                         g_filter->exclude_path[i]=strdup(path.u.s);
                         fprintf(stderr, "Exclude Path: %s\n", path.u.s);
                     }
                 }
             }
         }
    }

    toml_table_t* include = toml_table_in(conf, "include_only");
    if (include!=NULL) {
         toml_array_t* path_array = toml_array_in(include, "paths");
         if (path_array!=NULL) {
             g_filter->include_path_count = toml_array_nelem(path_array);
             if (g_filter->include_path_count>0) {
                 g_filter->include_path=malloc(g_filter->include_path_count*sizeof(char*));
                 for (int i = 0; i<g_filter->include_path_count; i++) {
                     toml_datum_t path = toml_string_at(path_array, i);
                     if (path.ok>0) {
                         g_filter->include_path[i]=strdup(path.u.s);
                         fprintf(stderr, "Include Path: %s\n", path.u.s);
                     }
                 }
             }
         }
    }


    toml_table_t* filter = toml_table_in(conf, "filter");
    for (int i=0;i<QN_FLAGS;i++) {
        toml_datum_t filter_value = toml_string_in(filter, op_names[i]);
        if (filter_value.ok) {
            if (strcmp(filter_value.u.s, "success")==0) {
                op_flags[i]=LOG_SUCCESS;
            }
            else if (strcmp(filter_value.u.s, "unsuccess")==0) {
                op_flags[i]=LOG_UNSUCCESS;
            }
            else if (strcmp(filter_value.u.s, "all")==0) {
                op_flags[i]= LOG_UNSUCCESS | LOG_UNSUCCESS;
            }
            else if (strcmp(filter_value.u.s, "never")==0) {
                op_flags[i]=0;
            }
            else {
                fprintf(stderr, "Wrong value [%s] for operation: [%s]\n", filter_value.u.s, op_names[i]);
                rc=3;
                goto close;
            }
        }
    }
close:
    toml_free(conf);
    fclose(fp);
    return rc;
}


int main(int argc, char *argv[]) {

    struct fuse_operations loggedFS_oper;

    h = Hash_New(32);
    loggedfsArgs = (LoggedFS_Args *) malloc(sizeof(LoggedFS_Args));

    umask(0);
    init_fuse_oper(&loggedFS_oper);

    for (int i=0;i<QN_FLAGS;i++) {
        op_flags[i]= LOG_SUCCESS | LOG_UNSUCCESS;
    }

    for (int i = 0; i < MaxFuseArgs; ++i) {
        loggedfsArgs->fuseArgv[i] = NULL; // libfuse expects null args..
    }

    if (processArgs(argc, argv, loggedfsArgs)) {
        if (loggedfsArgs->logToSyslog) {
            loggerId = "syslog";
        }

        if (loggedfsArgs->isDaemon==1) {
            if (loggedfsArgs->logFilename!=NULL) {
                hash_log = fopen(loggedfsArgs->logFilename, "w");
                fprintf(stderr, "Log file: %s\n", loggedfsArgs->logFilename);
            }
            else {
                fprintf(stderr, "Missing log file name!\n");
            }
        }
        else {
            hash_log = stderr;
        }


        if (loggedfsArgs->configFilename!=NULL) {
            g_filter=(filter_desc_t*) malloc(sizeof(filter_desc_t));
            memset(g_filter,0, sizeof(filter_desc_t));

            int rc=parse_config(loggedfsArgs->configFilename);         // this function modify g_filter & op_flags
            if (rc!=0) {
                return rc;
            }
        }

        fprintf(stderr, "LoggedFS starting at %s.\n", loggedfsArgs->mountPoint);
        fprintf(stderr, "Chdir to %s\n", loggedfsArgs->mountPoint);
        chdir(loggedfsArgs->mountPoint);
        savefd = open(".", 0);

#if (FUSE_USE_VERSION == 25)
        fuse_main(loggedfsArgs->fuseArgc, (char **)(loggedfsArgs->fuseArgv), &loggedFS_oper);
#else
        fuse_main(loggedfsArgs->fuseArgc, (char **)(loggedfsArgs->fuseArgv), &loggedFS_oper, NULL);
#endif
        fprintf(hash_log, "#### Hash size: [%d] ####\n", kh_size(h));
        fprintf(hash_log, "#### Log mask/legend:\n#");

        for (int i=0;i<QN_FLAGS;i++) {
            if (op_flags[i]==(LOG_UNSUCCESS | LOG_UNSUCCESS)) {
                fprintf(hash_log, "a");
            }
            else if (op_flags[i] == LOG_SUCCESS) {
                fprintf(hash_log, "s");
            }
            else if (op_flags[i] == LOG_UNSUCCESS) {
                fprintf(hash_log, "u");
            }
            else {
                fprintf(hash_log, ".");
            }
        }
        fprintf(hash_log, "####\n");
        fprintf(hash_log, "#GArdKMSuDNLmoTnsORWteFXxlv\n");
        Print_Hash(hash_log, h);
        Free_Hash(h);
        fclose(hash_log);
        fprintf(stderr, "LoggedFS closing.\n");
    }
}
