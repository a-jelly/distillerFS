ulimit -n 4096

if [ -z $1 ]; then
    echo "Usage: start_distfs.sh <mount point>"
    exit
fi

./distillerfs -c config.toml -l access.log $1
