#!/bin/bash
#ARCH?=x86_64
export BTYPE="debug"
BDIR="build/Linux/${ARCH}/${BTYPE}"
WS="../.."
export LD_LIBRARY_PATH="$WS/ferrybase/$BDIR:$WS/FFJSON/$BDIR:$WS/logger/$BDIR"
