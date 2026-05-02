BTYPE?=debug
OS?=Linux
FFJINC=../../FFJSON
FFLINC=../../logger
FBINC=../../ferrybase
FFINC=../..
BD?=build/${OS}/$(shell uname -m)/${BTYPE}/
FFJLIB=../../FFJSON/${BD}libFFJSON.${LIBEXT}
FFLLIB=../../logger/${BD}liblogger.${LIBEXT}
FBLIB=../../ferrybase/${BD}libferrybase.${LIBEXT}
TCPP?=/opt/rocm/llvm/bin/clang++
DBGF=-O3
DBG=_DEBUG0
ifeq ($(BTYPE),debug)
	DBG=_DEBUG
#	DBGF=-fsanitize=address -fno-sanitize-recover=all -fsanitize-address-use-after-scope -g -O0 -fno-omit-frame-pointer
	DBGF=-g -O0
endif

https: ${BD}https htmlParser.cpp
	@echo "built $(word 1, $^)"

${BD}https: ${BD}https.o ${BD}ferryfair.o ${BD}cap.o ${BD}smtpClient.o \
${BD}spatialSearch.o ${BD}htmlParser.o
	${TCPP} -std=c++23 -fPIE ${CXXFLAGS} ${DBGF} -D${DBG} -pthread \
   -Wno-unqualified-std-cast-call -I${FFJINC} -I${FFLINC} -I${FBINC} \
	-I${FFINC} -I.. $^ -o $@ ${FFJLIB} ${FFLLIB} ${FBLIB} \
	-lssl -lcrypto -ljpeg -lz -licui18n -licuuc -licudata \
	-lcurl -Wl,-unresolved-symbols=ignore-in-shared-libs

${BD}smtpClient.o: smtpClient.cpp smtpClient.h
	${TCPP} -c -std=c++23 ${CXXFLAGS} ${DBGF} -D${DBG} -I${FFINC} -I${FBINC} \
	-I${FFLINC} -I${FFJINC} ${CXXFLAGS} $< -o $@

${BD}htmlParser.o: htmlParser.cpp htmlParser.h
	${TCPP} -c -std=c++23 ${CXXFLAGS} ${DBGF} -D${DBG} \
	${CXXFLAGS} $< -o $@

${BD}cap.o: ../cap.cpp ../cap.h
	${TCPP} ${CXXFLAGS} -c ${DBGF} -D${DBG} -Dcimg_display=0 $< -o $@

${BD}spatialSearch.o: spatialSearch.cpp spatialSearch.h
	${TCPP} -c -std=c++23 ${CXXFLAGS} ${DBGF} -D${DBG} \
   -Wno-unqualified-std-cast-call -I${FFINC} -I${FBINC} -I${FFLINC} \
	-I${FFJINC} -I.. $< -o $@

${BD}ferryfair.o: ferryfair.cpp ferryfair.h
	${TCPP} -c -std=c++23 ${CXXFLAGS} ${DBGF} -D${DBG} -pthread \
   -Wno-unqualified-std-cast-call -I${FFJINC} -I${FFLINC} -I${FBINC} \
	-I${FFINC} -I.. $< -o $@

${BD}https.o: https.cpp https.h
	mkdir -p ${BD}; \
	${TCPP} -c -std=c++23 ${CXXFLAGS} ${DBGF} -D${DBG} -pthread \
   -Wno-unqualified-std-cast-call -I${FFJINC} -I${FFLINC} -I${FBINC} \
	-I${FFINC} -I.. $< -o $@



${BD}critcalSection.o:
	${TCPP} -c -masm=intel criticalSection.s -o $@
${BD}test: ${BD}criticalSection.o
	${TCPP} -std=c++23 -g -O0 criticalSection.o test.cpp -o $@

testSpatialSearch: ${BD}spatialSearch.o testSpatialSearch.cpp
	${TCPP} -pthread -std=c++23 -fPIE ${DBGF} -D${DBG} \
   -Wno-unqualified-std-cast-call -I${FFINC} -I${FFJINC} -I${FFLINC} \
	-I${FBINC} -I.. $^ -o ${BD}$@ ${FBLIB} ${FFLLIB} ${FFJLIB} \
	-lssl -lcrypto -ljpeg -lz -licui18n -licuuc -licudata \
	-lcurl -Wl,--no-as-needed -Wl,-unresolved-symbols=ignore-in-shared-libs

testHtmlParser: ${BD}htmlParser.o testHtmlParser.cpp
	${TCPP} -std=c++23 -fPIE ${DBGF} -D${DBG} $^ -o ${BD}$@

clean:
	/bin/bash -c "rm -rf ${BD}{*.o,https,test*}"
