ARCH?=x86_64
FFJINC=../../FFJSON
FFLINC=../../logger
FBINC=../../ferrybase
FFINC=../..
FFJLIB=../../FFJSON/build/Linux/${ARCH}/debug/libFFJSON.a
FFLLIB=../../logger/build/Linux/${ARCH}/debug/liblogger.a
FBLIB=../../ferrybase/build/Linux/${ARCH}/debug/libferrybase.a
TCPP?=/opt/rocm/llvm/bin/clang++
all:
	${TCPP} -std=c++23 -g -O0 -D_DEBUG0 -Dcimg_display=0\
   -Wno-unqualified-std-cast-call -I${FFJINC} -I${FFLINC} -I${FBINC} -I${FFINC}\
   -I.. smtpClient.cpp ferryfair.cpp spatialSearch.cpp https.cpp ../cap.cpp -lssl -lcrypto -lpthread -lxml2 -lz -lrt -licuuc -licudata -licui18n ${FFJLIB} ${FFLLIB} ${FBLIB} -ljpeg\
   -o https_${ARCH}

critcalSection.o:
	${TCPP} -c -masm=intel criticalSection.s -o criticalSection.o
test: criticalSection.o
	${TCPP} -std=c++23 -g -O0 criticalSection.o test.cpp -o test

clean:
	rm -rf https test
