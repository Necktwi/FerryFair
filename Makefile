FFJINC=../FFJSON
FFLINC=../logger
FBINC=../ferrybase
FFINC=..
FFJLIB=../FFJSON/build/Linux/x86_64/debug/libFFJSON.a
FFLLIB=../logger/build/Linux/x86_64/debug/liblogger.a
FBLIB=../ferrybase/build/Linux/x86_64/debug/libferrybase.a

all:
	/opt/rocm/llvm/bin/clang++ -std=c++23 -g -O0 -D_DEBUG \
   -Wno-unqualified-std-cast-call -I${FFJINC} -I${FFLINC} -I${FBINC} -I${FFINC}\
   https.cpp -lssl -lcrypto -lpthread ${FFJLIB} ${FFLLIB} ${FBLIB} -ljpeg\
   -o https

clean:
	rm -rf https
