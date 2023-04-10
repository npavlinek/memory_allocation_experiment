#!/usr/bin/env sh

set -ex

rm -rf out
mkdir out
pushd out >/dev/null

g++ -Wall -Wextra -pedantic -std=c++20 -O2 -o main.exe ../main.cc

popd >/dev/null
