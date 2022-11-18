#!/bin/bash

echo "Test started"
HERE=`pwd`
./distillerfs -c dist_config.toml -l test.log $HERE/dist_test
sleep 1
cd dist_test
touch snafu.c

echo "bar" > ./include/exclude/foo.txt
echo "foo" > ./include/bar.txt

ln -s ./normal/punda.txt punda.txt
rm -f punda.txt
cd ..
sleep 1
umount $HERE/dist_test
echo "Done"
