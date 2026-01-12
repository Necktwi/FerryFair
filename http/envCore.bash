#!/bin/bash
ARCH=aarch64
OS=Linux
#export BTYPE=release
export BTYPE=debug
BD="build/${OS}/${ARCH}/${BTYPE}"
mkdir -p ${BD}
WS="../.."
export LD_LIBRARY_PATH="$WS/ferrybase/$BD:$WS/FFJSON/$BD:$WS/logger/$BD"
export TCPP=clang++
