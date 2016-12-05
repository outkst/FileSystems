#!/bin/bash
echo "Resetting environment"
fusermount -u testmount
make
rm .disk
rm -r testmount
dd bs=1K count=5K if=/dev/zero of=.disk
mkdir testmount
./cs1550 -d testmount
