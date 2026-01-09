ARCH?=x86_64
BTYPE?=debug
OS?=Linux
FFJINC=../../FFJSON
FFLINC=../../logger
FBINC=../../ferrybase
FFINC=../..
BDIR?=build${OS}${ARCH}${BTYPE}/
BD=build/${OS}/${ARCH}/${BTYPE}
FFJLIB=../../FFJSON/${BD}/libFFJSON.so.1.0
FFLLIB=../../logger/${BD}/liblogger.so.1.0
FBLIB=../../ferrybase/${BD}/libferrybase.so.1.0
TCPP?=/opt/rocm/llvm/bin/clang++
DBGF=-O3
DBG=_DEBUG0
ifeq ($(BTYPE),debug)
	DBG=_DEBUG
	DBGF=-g -O0
endif

${BD}/smtpClient.o: smtpClient.cpp
	${TCPP} -c -std=c++23 ${DBGF} -D${DBG} -I${FFINC} -I${FBINC} -I${FFLINC} \
	-I${FFJINC} $^ -o $@

${BD}/cap.o: ../cap.cpp
	${TCPP} -c ${DBGF} -D${DBG} -Dcimg_display=0 $^ -o $@

${BD}/spatialSearch.o: spatialSearch.cpp
	${TCPP} -c -std=c++23 ${DBGF} -D${DBG} \
   -Wno-unqualified-std-cast-call -I${FFINC} -I${FBINC} -I${FFLINC} \
	-I${FFJINC} -I.. $^ -o $@

${BD}/ferryfair.o: ferryfair.cpp
	${TCPP} -c -pthread -std=c++23 ${DBGF} -D${DBG} \
   -Wno-unqualified-std-cast-call -I${FFJINC} -I${FFLINC} -I${FBINC} \
	-I${FFINC} -I.. $^ -o $@

${BD}/https.o: https.cpp
	mkdir -p ${BD}; \
	${TCPP} -c -pthread -std=c++23 ${DBGF} -D${DBG} \
   -Wno-unqualified-std-cast-call -I${FFJINC} -I${FFLINC} -I${FBINC} \
	-I${FFINC} -I.. $^ -o $@

${BD}/https: ${BD}/https.o ${BD}/ferryfair.o ${BD}/cap.o ${BD}/smtpClient.o \
${BD}/spatialSearch.o
	${TCPP} -pthread -std=c++23 -fPIE ${DBGF} -D${DBG} \
   -Wno-unqualified-std-cast-call -I${FFJINC} -I${FFLINC} -I${FBINC} \
	-I${FFINC} -I.. $^ -o $@ ${FFJLIB} ${FFLLIB} ${FBLIB} \
	-lssl -lcrypto -ljpeg -lz -lrt -licui18n -licuuc -licudata \
	-lcurl -Wl,-unresolved-symbols=ignore-in-shared-libs; \

https: ${BD}/https htmlParser.cpp
	@echo "built $(word 1, $^)"

${BD}/critcalSection.o:
	${TCPP} -c -masm=intel criticalSection.s -o $@
${BD}/test: ${BD}/criticalSection.o
	${TCPP} -std=c++23 -g -O0 criticalSection.o test.cpp -o $@

testSpatialSearch: spatialSearch.o testSpatialSearch.cpp
	${TCPP} -pthread -std=c++23 -fPIE ${DBGF} -D${DBG} \
   -Wno-unqualified-std-cast-call -I${FFINC} -I${FFJINC} -I${FFLINC} \
	-I${FBINC} -I.. $^ -o ${BD}$@ ${FBLIB} ${FFLLIB} ${FFJLIB} \
	-lssl -lcrypto -ljpeg -lz -lrt -licui18n -licuuc -licudata \
	-lcurl -Wl,--no-as-needed -Wl,-unresolved-symbols=ignore-in-shared-libs

clean:
	rm -rf *.o https_*_* test*
