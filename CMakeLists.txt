# Gowtham Kudupudi 23/06/2018
# 
# Further authors should be added above following a new line.
# Tab size is 3. Right margin is 80 characters away.
# 
# ©FerryFair
                                                                                
cmake_minimum_required (VERSION 3.5)
project (FerryFair CXX C)
include (GNUInstallDirs)
message ("Generating build procedure for ${PROJECT_NAME} ...")

message ("Extropolating build type ...")
string (
   REPLACE ${CMAKE_CURRENT_SOURCE_DIR} "" SRC_RLTV_BLD_DIR
   ${PROJECT_BINARY_DIR}
)
message ("SRC_RLTV_BLD_DIR: ${SRC_RLTV_BLD_DIR}")
string (REPLACE "/Build" "" BUILD_TARGET ${SRC_RLTV_BLD_DIR})
if (${SRC_RLTV_BLD_DIR})
   string (REPLACE "/build" "" BUILD_TARGET ${BUILD_TARGET})
endif ()
message ("BUILD_TARGET: ${BUILD_TARGET}")

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../FFJSON")
   message ("Using development sources.")
   if (NOT TARGET FFJSON)
      add_subdirectory (
         ${CMAKE_CURRENT_SOURCE_DIR}/../FFJSON
         ${CMAKE_CURRENT_SOURCE_DIR}/../FFJSON/${SRC_RLTV_BLD_DIR}
      )
   endif ()
   if (NOT TARGET logger)
      add_subdirectory (
         ${CMAKE_CURRENT_SOURCE_DIR}/../logger
         ${CMAKE_CURRENT_SOURCE_DIR}/../logger/${SRC_RLTV_BLD_DIR}
      )
   endif ()
   if (NOT TARGET ferrybase)
      add_subdirectory (
         ${CMAKE_CURRENT_SOURCE_DIR}/../ferrybase
         ${CMAKE_CURRENT_SOURCE_DIR}/../ferrybase/${SRC_RLTV_BLD_DIR}
      )
   endif ()
endif ()
if (DEFINED _DEBUG)
   add_definitions (-D_DEBUG=${_DEBUG})
endif ()
add_definitions (-DMG_ENABLE_OPENSSL=1)

set (VERSION_MAJOR 1)
set (VERSION_MINOR 0)
set (Umbrella "ferryfair")
configure_file (
   "${PROJECT_SOURCE_DIR}/config.h.in"
   "${PROJECT_BINARY_DIR}/config.h"
)
configure_file (
   "${PROJECT_SOURCE_DIR}/config.ffjson"
   "${PROJECT_BINARY_DIR}/config.ffjson"
)
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
   set (macOS ON)
endif ()

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (
   GCC_COVERAGE_LINK_FLAGS
   "-fPIC -Wl,-unresolved-symbols=ignore-in-shared-libs"
)
if (DEFINED _DEBUG)
   set (GCC_COVERAGE_COMPILE_FLAGS "${GCC_COVERAGE_COMPILE_FLAGS} -g")
   set (GCC_COVERAGE_LINK_FLAGS "${GCC_COVERAGE_LINK_FLAGS} -g")
endif ()
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${GCC_COVERAGE_COMPILE_FLAGS}")
set (
   CMAKE_EXE_LINKER_FLAGS
   "${CMAKE_EXE_LINKER_FLAGS} ${GCC_COVERAGE_LINK_FLAGS}"
)
file (GLOB HEADERS [a-zA-Z0-9_]*.h)
file (GLOB SOURCES [a-zA-Z0-9_]*.cpp)
list (APPEND HEADERS "config.h.in" ${PROJECT_BINARY_DIR}/config.h)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../ferrybase")
   include_directories (${PROJECT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
   include_directories (
      ${CMAKE_CURRENT_SOURCE_DIR}/../ferrybase
      ${CMAKE_CURRENT_SOURCE_DIR}/../ferrybase${BUILD_TARGET} 
      ${CMAKE_CURRENT_SOURCE_DIR}/..
   )
   include_directories (
      ${CMAKE_CURRENT_SOURCE_DIR}/../logger
      ${CMAKE_CURRENT_SOURCE_DIR}/../logger${BUILD_TARGET}
   )
   include_directories (
      ${CMAKE_CURRENT_SOURCE_DIR}/../FFJSON
      ${CMAKE_CURRENT_SOURCE_DIR}/../FFJSON${BUILD_TARGET}
   )
   if (CMAKE_CONFIGURATION_TYPES)
      message ("Linking corresponding to the Configuration Name")
      set (BLD_DIR "${BUILD_TARGET}/$(ConfigurationName)")
      link_directories (
         "${CMAKE_CURRENT_SOURCE_DIR}/../ferrybase/${BLD_DIR}"
      )
      link_directories (
         "${CMAKE_CURRENT_SOURCE_DIR}/../logger/${BLD_DIR}"
      )
      link_directories (
         "${CMAKE_CURRENT_SOURCE_DIR}/../FFJSON/${BLD_DIR}"
      )
   else ()
      set (BLD_DIR "${BUILD_TARGET}/")
      link_directories ("${CMAKE_CURRENT_SOURCE_DIR}/../ferrybase/${BLD_DIR}")
      link_directories ("${CMAKE_CURRENT_SOURCE_DIR}/../logger/${BLD_DIR}")
      link_directories ("${CMAKE_CURRENT_SOURCE_DIR}/../FFJSON/${BLD_DIR}")
   endif ()
else ()
   include_directories (
      ${PROJECT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_INSTALL_FULL_INCLUDEDIR}/${Umbrella}
   )
endif ()
if (macOS)
   include_directories (/usr/local/opt/openssl/include)
   link_directories (/usr/local/opt/openssl/lib)
endif ()

if (NOT LINKAGE)
  set (LINKAGE "STATIC")
  message ("Building static by default")
else (NOT LINKAGE)
  string (TOUPPER "${LINKAGE}" LINKAGE)
endif ()

if (${LINKAGE} MATCHES "STATIC")
   add_executable (${PROJECT_NAME} ${SOURCES} ${HEADERS})
else ()
   add_executable (${PROJECT_NAME} ${SOURCES} ${HEADERS})
endif ()

target_link_libraries (
   ${PROJECT_NAME} FFJSON logger ferrybase ssl crypto pthread pam
)
set_target_properties (
   ${PROJECT_NAME}
   PROPERTIES
   VERSION ${VERSION_MAJOR}.${VERSION_MINOR}
   SOVERSION ${VERSION_MAJOR}
)
install (TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})
install (
   FILES "${PROJECT_SOURCE_DIR}/config.ffjson"
   DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}
   RENAME ${PROJECT_NAME}.ffjson
)
install (
   FILES "${PROJECT_SOURCE_DIR}/init.conf"
   DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/init
   RENAME ${PROJECT_NAME}.init
)
install (
   FILES "${PROJECT_SOURCE_DIR}/init.override"
   DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/init
   RENAME ${PROJECT_NAME}.override
)
