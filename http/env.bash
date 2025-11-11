#!/bin/bash
ARCH=${ARCH:-x86_64}
OS=${OS:-Linux}
export BTYPE="debug"
BDIR="build/${OS}/${ARCH}/${BTYPE}"
WS="../.."
export LD_LIBRARY_PATH="$WS/ferrybase/$BDIR:$WS/FFJSON/$BDIR:$WS/logger/$BDIR"
