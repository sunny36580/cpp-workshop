#!/bin/bash

# exit on error and print each command
set -ex

# cmake
cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=./build/install \
    -DLOGGER_BUILD_TESTS=OFF \
    $@

if [ -d ./build/install ]; then
    rm -rf ./build/install
fi

cmake --build build --config Release --target install --parallel $(nproc)
