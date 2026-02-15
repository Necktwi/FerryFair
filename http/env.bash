#!/bin/bash
export OS=Linux
#export BTYPE=release
export BTYPE=debug
export BD="build/${OS}/$(uname -m)/${BTYPE}/"
mkdir -p ${BD}
WS="../../"
export LD_LIBRARY_PATH="${WS}ferrybase/$BD:${WS}FFJSON/$BD:${WS}logger/$BD"
export TCPP=g++
export LIBEXT=so
