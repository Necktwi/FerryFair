cmake_minimum_required (VERSION 3.5)
project (ferrymediaserver)
IF (DEFINED _DEBUG)
ADD_DEFINITIONS(-D_DEBUG=${_DEBUG})
ENDIF()
set (FFJSON_VERSION_MAJOR 1)
set (FFJSON_VERSION_MINOR 0)
set (Umbrella "ferryfair")
IF(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
# Mac OS X specific code
SET(macOS ON)
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
# configure a header file to pass some of the CMake settings
# to the source code
configure_file (
    "${PROJECT_SOURCE_DIR}/ferrymediaserverConfig.h.in"
    "${PROJECT_BINARY_DIR}/ferrymediaserverConfig.h"
    )
# add the binary tree to the search path for include files
# so that we will find TutorialConfig.h
include_directories("${PROJECT_BINARY_DIR}")

set (GCC_COVERAGE_COMPILE_FLAGS "-std=c++14 -O0")
set (GCC_COVERAGE_LINK_FLAGS "")
include_directories(${CMAKE_CURRENT_SOURCE_DIR} "/usr/local/include/ferryfair")
link_directories(/usr/local/lib)
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${GCC_COVERAGE_COMPILE_FLAGS}")
set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${GCC_COVERAGE_LINK_FLAGS}")
file(GLOB HEADERS *.h)
file(GLOB SOURCES *.cpp)
add_executable(ferrymediaserver ${SOURCES} ${HEADERS})
target_link_libraries(ferrymediaserver websockets FFJSON logger base ssl pthread xml2 crypto z)
if(!macOS)
target_link_libraries(ferrymediaserver rt)
endif(!macOS)
set_target_properties(ferrymediaserver
    PROPERTIES
    VERSION ${FFJSON_VERSION_MAJOR}.${FFJSON_VERSION_MINOR}
    SOVERSION ${FFJSON_VERSION_MAJOR}
    )
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
install(FILES "${PROJECT_BINARY_DIR}/ferrymediaserverConfig.h"
    DESTINATION include/${Umbrella})
install(FILES "${PROJECT_SOURCE_DIR}/config.json"
    DESTINATION /etc
    RENAME ${PROJECT_NAME}.json)
