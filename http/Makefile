ARCH?=x86_64
BTYPE?=debug
OS?=Linux
FFJINC=../../FFJSON
FFLINC=../../logger
FBINC=../../ferrybase
FFINC=../..
BD=${OS}/${ARCH}/${BTYPE}/
FFJLIB=-L../../FFJSON/build/${BD}
FFLLIB=-L../../logger/build/${BD}
FBLIB=-L../../ferrybase/build/${BD}
TCPP?=/opt/rocm/llvm/bin/clang++
DBG?=_DEBUG
DBGF?=-g -O0
#DBGF=-O3

smtpClient.o: smtpClient.cpp
	${TCPP} -c -std=c++23 ${DBGF} -D${DBG} -I${FFINC} -I${FBINC} -I${FFLINC} -I${FFJINC} $^ -o $@

cap.o: ../cap.cpp
	${TCPP} -c ${DBGF} -D${DBG} -Dcimg_display=0 $^ -o $@

spatialSearch.o: spatialSearch.cpp
	${TCPP} -c -std=c++23 ${DBGF} -D${DBG}\
   -Wno-unqualified-std-cast-call -I${FFINC} -I${FBINC} -I${FFLINC} -I${FFJINC}\
   -I.. $^ -o $@

ferryfair.o: ferryfair.cpp
	${TCPP} -c -pthread -std=c++23 ${DBGF} -D${DBG}\
   -Wno-unqualified-std-cast-call -I${FFJINC} -I${FFLINC} -I${FBINC} -I${FFINC}\
   -I.. $^ -o $@

https.o: https.cpp
	${TCPP} -c -pthread -std=c++23 ${DBGF} -D${DBG}\
   -Wno-unqualified-std-cast-call -I${FFJINC} -I${FFLINC} -I${FBINC} -I${FFINC}\
   -I.. $^ -o $@

all: https.o ferryfair.o cap.o smtpClient.o spatialSearch.o
	${TCPP} -pthread -std=c++23 -fPIE ${DBGF} -D${DBG}\
   -Wno-unqualified-std-cast-call -I${FFJINC} -I${FFLINC} -I${FBINC} -I${FFINC}\
   -I.. $^ ${FFJLIB} ${FFLLIB} ${FBLIB} -lFFJSON -llogger -lferrybase -lssl -lcrypto -ljpeg -lz -lrt -licui18n -licuuc -licudata -lcurl\
   -o https_${OS}_${ARCH}

critcalSection.o:
	${TCPP} -c -masm=intel criticalSection.s -o criticalSection.o
test: criticalSection.o
	${TCPP} -std=c++23 -g -O0 criticalSection.o test.cpp -o test

clean:
	rm -rf *.o https_${OS}_${ARCH} test
