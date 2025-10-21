ARCH?=x86_64
FFJINC=../../FFJSON
FFLINC=../../logger
FBINC=../../ferrybase
FFINC=../..
FFJLIB=../../FFJSON/build/Linux/${ARCH}/debug/libFFJSON.a
FFLLIB=../../logger/build/Linux/${ARCH}/debug/liblogger.a
FBLIB=../../ferrybase/build/Linux/${ARCH}/debug/libferrybase.a
TCPP?=/opt/rocm/llvm/bin/clang++
DBG=_DEBUG
DBGF=-g -O0

smtpClient.o: smtpClient.cpp
	${TCPP} -c ${DBGF} -D${DBG} $^ -o $@

cap.o: ../cap.cpp
	${TCPP} -c ${DBGF} -D${DBG} -Dcimg_display=0 $^ -o $@

spatialSearch.o: spatialSearch.cpp
	${TCPP} -c -std=c++23 ${DBGF} -D${DBG}\
   -Wno-unqualified-std-cast-call -I${FFJINC} -I${FFLINC} -I${FBINC} -I${FFINC}\
   -I.. $^ -o $@

ferryfair.o: ferryfair.cpp
	${TCPP} -c -std=c++23 ${DBGF} -D${DBG}\
   -Wno-unqualified-std-cast-call -I${FFJINC} -I${FFLINC} -I${FBINC} -I${FFINC}\
   -I.. $^ -o $@

all: https.cpp ferryfair.o cap.o smtpClient.o spatialSearch.o
	${TCPP} -std=c++23 ${DBGF} -D${DBG}\
   -Wno-unqualified-std-cast-call -I${FFJINC} -I${FFLINC} -I${FBINC} -I${FFINC}\
   -I.. $^ ${FFJLIB} ${FFLLIB} ${FBLIB} -lssl -lcrypto -lpthread -ljpeg -lz -lrt -licui18n -licuuc -licudata\
   -o https_${ARCH}

critcalSection.o:
	${TCPP} -c -masm=intel criticalSection.s -o criticalSection.o
test: criticalSection.o
	${TCPP} -std=c++23 -g -O0 criticalSection.o test.cpp -o test

clean:
	rm -rf *.o https_${ARCH} test
