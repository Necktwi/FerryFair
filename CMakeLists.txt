cmake_minimum_required (VERSION 3.5)
include(GNUInstallDirs)
project (ferryfair)
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
set (GCC_COVERAGE_LINK_FLAGS "-fPIC -Wl,-unresolved-symbols=ignore-in-shared-libs")
IF (DEFINED _DEBUG)
set (GCC_COVERAGE_COMPILE_FLAGS "${GCC_COVERAGE_COMPILE_FLAGS} -gdwarf-2")
set (GCC_COVERAGE_LINK_FLAGS "${GCC_COVERAGE_LINK_FLAGS}")
ENDIF()
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${GCC_COVERAGE_COMPILE_FLAGS}")
set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${GCC_COVERAGE_LINK_FLAGS}")
file(GLOB HEADERS [a-zA-Z0-9_]*.h)
file(GLOB SOURCES [a-zA-Z0-9_]*.cpp)
include_directories(${PROJECT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
message("CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
include_directories (${PROJECT_SOURCE_DIR}/ferrybase ${PROJECT_BINARY_DIR}/ferrybase)
add_subdirectory(ferrybase ${PROJECT_SOURCE_DIR}/ferrybase/build/)
include_directories(${PROJECT_SOURCE_DIR}/logger ${PROJECT_BINARY_DIR}/logger)
add_subdirectory(logger ${PROJECT_SOURCE_DIR}/logger/build/)
include_directories (${PROJECT_SOURCE_DIR}/FFJSON ${PROJECT_BINARY_DIR}/FFJSON)
add_subdirectory(FFJSON ${PROJECT_SOURCE_DIR}/FFJSON/build/)
include_directories(${PROJECT_SOURCE_DIR}/libwebsockets/lib ${PROJECT_SOURCE_DIR}/libwebsockets ${PROJECT_SOURCE_DIR}/libwebsockets/build)
add_subdirectory(libwebsockets ${PROJECT_SOURCE_DIR}/libwebsockets/build/)
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})
target_link_libraries(${PROJECT_NAME} websockets FFJSON logger ferrybase pthread ssl crypto)
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
