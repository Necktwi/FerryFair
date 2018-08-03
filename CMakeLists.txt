# Gowtham Kudupudi 23/06/2018
# 
# Further authors should be added above following a new line.
# Tab size is 3. Right margin is 80 characters away.
# 
# ©FerryFair
                                                                                
cmake_minimum_required (VERSION 3.5)
include(GNUInstallDirs)
project (FerryFair)
message ("Generating build procedure for ${PROJECT_NAME} ...")

add_definitions(-DFOO)

message ("Extropolating build type ...")
string (REPLACE ${CMAKE_CURRENT_SOURCE_DIR} "" SRC_RLTV_BLD_DIR
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
   add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../FFJSON 
      ${CMAKE_CURRENT_SOURCE_DIR}/../FFJSON/Build${BUILD_TARGET})
   add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../libwebsockets 
      ${CMAKE_CURRENT_SOURCE_DIR}/../libwebsockets/Build${BUILD_TARGET})
   if (NOT TARGET logger)
      add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../logger 
         ${CMAKE_CURRENT_SOURCE_DIR}/../logger/Build${BUILD_TARGET})
   endif ()
   if (NOT TARGET ferrybase)
      add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../ferrybase 
         ${CMAKE_CURRENT_SOURCE_DIR}/../ferrybase/Build${BUILD_TARGET})
   endif ()
endif ()
IF (DEFINED _DEBUG)
ADD_DEFINITIONS(-D_DEBUG=${_DEBUG})
ENDIF()
set (VERSION_MAJOR 1)
set (VERSION_MINOR 0)
set (Umbrella "ferryfair")
configure_file (
    "${PROJECT_SOURCE_DIR}/config.h.in"
    "${PROJECT_BINARY_DIR}/config.h"
    )
configure_file (
    "${PROJECT_SOURCE_DIR}/config.json"
    "${PROJECT_BINARY_DIR}/config.json"
    )
IF(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
# Mac OS X specific code
SET(macOS ON)
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")

set (GCC_COVERAGE_COMPILE_FLAGS "-pthread")
set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (GCC_COVERAGE_LINK_FLAGS 
   "-fPIC -Wl,-unresolved-symbols=ignore-in-shared-libs")
IF (DEFINED _DEBUG)
   set (GCC_COVERAGE_COMPILE_FLAGS "${GCC_COVERAGE_COMPILE_FLAGS} -g")
   set (GCC_COVERAGE_LINK_FLAGS "${GCC_COVERAGE_LINK_FLAGS} -g")
ENDIF()
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${GCC_COVERAGE_COMPILE_FLAGS}")
set (CMAKE_EXE_LINKER_FLAGS
   "${CMAKE_EXE_LINKER_FLAGS} ${GCC_COVERAGE_LINK_FLAGS}"
)
file(GLOB HEADERS [a-zA-Z0-9_]*.h)
file(GLOB SOURCES [a-zA-Z0-9_]*.cpp)
list(APPEND HEADERS "config.h.in" ${PROJECT_BINARY_DIR}/config.h)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../ferrybase")
   include_directories(${PROJECT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
   include_directories (${CMAKE_CURRENT_SOURCE_DIR}/../ferrybase 
      ${CMAKE_CURRENT_SOURCE_DIR}/../ferrybase/Build${BUILD_TARGET} 
      ${CMAKE_CURRENT_SOURCE_DIR}/..)
   include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../logger 
      ${CMAKE_CURRENT_SOURCE_DIR}/../logger/Build${BUILD_TARGET})
   include_directories (${CMAKE_CURRENT_SOURCE_DIR}/../FFJSON 
      ${CMAKE_CURRENT_SOURCE_DIR}/../FFJSON/Build${BUILD_TARGET})
   include_directories (${CMAKE_CURRENT_SOURCE_DIR}/../libwebsockets/lib
      ${CMAKE_CURRENT_SOURCE_DIR}/../libwebsockets/Build${BUILD_TARGET})
   if(CMAKE_CONFIGURATION_TYPES)
      message("Linking corresponding to the Configuration Name")
      link_directories(
         "${CMAKE_CURRENT_SOURCE_DIR}/../ferrbase/Build${BUILD_TARGET}/$(ConfigurationName)")
      link_directories(
         "${CMAKE_CURRENT_SOURCE_DIR}/../logger/Build${BUILD_TARGET}/$(ConfigurationName)")
      link_directories("${CMAKE_CURRENT_SOURCE_DIR}/../FFJSON/Build${BUILD_TARGET}/$(\
         ConfigurationName)")
      link_directories(
         "${CMAKE_CURRENT_SOURCE_DIR}/../libwebsockets/Build${BUILD_TARGET}/$(\
            ConfigurationName)")
   else ()
      link_directories("${CMAKE_CURRENT_SOURCE_DIR}/../ferrbase/Build${BUILD_TARGET}/")
      link_directories("${CMAKE_CURRENT_SOURCE_DIR}/../logger/Build${BUILD_TARGET}/")
      link_directories("${CMAKE_CURRENT_SOURCE_DIR}/../FFJSON/Build${BUILD_TARGET}/")
      link_directories("${CMAKE_CURRENT_SOURCE_DIR}/../libwebsockets/Build${BUILD_TARGET}/")
   endif ()
else()
   include_directories(${PROJECT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR} 
      ${CMAKE_INSTALL_FULL_INCLUDEDIR}/${Umbrella})
endif()
if (macOS)
   include_directories(/usr/local/opt/openssl/include)
   link_directories(/usr/local/opt/openssl/lib)
endif ()
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})
target_link_libraries(${PROJECT_NAME} websockets FFJSON logger ferrybase pthread
   ssl crypto)
set_target_properties(${PROJECT_NAME}
    PROPERTIES
    VERSION ${VERSION_MAJOR}.${VERSION_MINOR}
    SOVERSION ${VERSION_MAJOR}
    )
install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES "${PROJECT_SOURCE_DIR}/config.json"
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}
    RENAME ${PROJECT_NAME}.json)
install(FILES "${PROJECT_SOURCE_DIR}/init.conf"
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/init
    RENAME ${PROJECT_NAME}.init)
install(FILES "${PROJECT_SOURCE_DIR}/init.override"
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/init
    RENAME ${PROJECT_NAME}.override)
