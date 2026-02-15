#!/bin/bash
OS=Linux
#export BTYPE=release
export BTYPE=debug
BD="build/${OS}/$(uname -m)/${BTYPE}"
mkdir -p ${BD}
WS="../.."
export LD_LIBRARY_PATH="$WS/ferrybase/$BD:$WS/FFJSON/$BD:$WS/logger/$BD"
export TCPP=clang++
export LIBEXT=so
