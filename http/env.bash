#!/bin/bash

BDIR="build/Linux/aarch64/debug"
WS="$HOME/workspace"
export LD_LIBRARY_PATH="$WS/ferrybase/$BDIR:$WS/FFJSON/$BDIR:$WS/logger/$BDIR"
