#!/bin/bash

(
cd $(dirname "${BASH_SOURCE[0]}")
git submodule update --init --recursive

(
cd deps/dpdk
make config T=x86_64-native-linuxapp-gcc
)

(
cd deps/dpdk
make
)

echo Trying to bind interfaces, this will fail if you are not root
echo Try "sudo ./bind-interfaces.sh" if this step fails
./bind-interfaces.sh
)

