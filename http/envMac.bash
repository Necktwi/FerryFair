#!/bin/bash
export ARCH=${ARCH:-x86_64}
export OS=${OS:-macOS}
export BTYPE=${BTYPE:-debug}
BDIR="build/${OS}/${ARCH}/${BTYPE}"
WS="../.."
export LD_LIBRARY_PATH="$WS/ferrybase/$BDIR:$WS/FFJSON/$BDIR:$WS/logger/$BDIR"
export TCPP=${TCPP:-clang++}
export CXXFLAGS="-isysroot $(xcrun --show-sdk-path)"
export CXXFLAGS="$CXXFLAGS -I$(xcrun --show-sdk-path)/usr/include"
export CXXFLAGS="$CXXFLAGS -I/opt/local/include -L/opt/local/lib"
export CXXFLAGS="$CXXFLAGS -I/opt/local/include/libepoll-shim"
export LIBEXT="1.0.dylib"
export MACOSX_DEPLOYMENT_TARGET=12.7
