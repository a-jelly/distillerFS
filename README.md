# DistillerFS - Filesystem monitoring with Fuse

## Description

DistillerFS is a [FUSE](https://en.wikipedia.org/wiki/Filesystem_in_Userspace)-based filesystem which can log operations that happens in it.
Based on [loggedFS](https://github.com/rflament/loggedfs) code.

The main goal of creating this FS was to reduce the size of large repositories, such as [Android AOSP](http://androidxref.com) and others like it. A modern repository for building Android 11 takes over 100 gigabytes, with only a fraction of it being needed to build a specific project. DistillerFS allows you to track which files were actually accessed and thus select only the files needed for your particular build from the complete repository.

The general method to "distill" the necessary files is as follows:
```
start_distfs.sh
cd OriginalRepo
make
stop_distfs.sh
process_log.lua access.log /home/user/OriginalRepo /home/user/DistilledRepo > copy_distist.sh
sh copy_dist.sh
```
In the configuration file, you have to specify the list of logged directories and (if needed) the list of directories excluded from logging (in case of Android it is "/out").

### How does it work ?

FUSE does almost everything. DistillerFS only store info, when called by FUSE and then let the real filesystem do the rest of the job.

## Simplest usage

To record access to `/tmp/TEST` into `~/log.txt`, just do:

    distillerfs -l ~/log.txt /tmp/TEST

To stop recording, just `unmount` as usual:

    sudo umount /tmp/TEST

The `~/log.txt` file will need to be changed to readable by setting permissions:

    chmod 0666 ~/log.txt

## Installation from source

First you have to make sure that FUSE is installed on your computer.
If you have a recent distribution it should be. FUSE can be downloaded here: [github.com/libfuse/libfuse](https://github.com/libfuse/libfuse).

Then you should download the DistillerFS source code archive and install it with the `make` command:

    sudo apt-get install libfuse-dev
    git clone https://github.com/a-jelly/distillerFS
    cd distillerFS
    make
    make install

DistillerFS has the following dependencies:

    fuse

## Configuration

DistillerFS can use an TOML configuration file if you want it to log operations only for certain files, for certain users, or for certain operations.

Here is a sample configuration file :
```
# list of available FUSE operations:
# access, chmod, chown, fsync, getattr, getxattr, link, listxattr, mkdir,
# mknod, open, read, readdir, readlink, release, removexattr, rename, rmdir,
# setxattr, statfs, symlink, truncate, unlink, utime, utimens, write.

[filter]
    # By default, logged all success operations
    open="success"
    write="success"
    read="all"
    getattr="never"
    unlink="unsuccess"

[exclude]
    # Paths (prefixes) excluded from logging
    paths=["/include/exclude"]

[include_only]
    # Paths (prefixes) included into logging
    # If empty, all subdirs of mount point are included
    # paths=["/include"]
```

## Launching DistillerFS

If you just want to test DistillerFS you don't need any configuration file.

Just run that script:

    ./test_dist.sh

You should see logs like these :

```
#### Hash size: [5] ####
#### Log mask/legend:
#.......a........sas.......####
#GArdKMSuDNLmoTnsORWteFXxlv
[.............T..O.W.e.....]:0000000004:/include/bar.txt
[..r...S...................]:0000000005:/punda.txt
[......S...................]:0000000001:./normal/punda.txt
[...............sO...e.....]:0000000003:/snafu.c
[.A.d......................]:0000000029:/
```

If you have a configuration file to use you should use this command:

    ./distillerfs -c config.toml -p /var

If you want to log what other users do on your filesystem, you should use the `-p` option to allow them to see your mounted files. For a complete documentation see the manual page.

Andrew Jelly - ajelly at gmail.com
