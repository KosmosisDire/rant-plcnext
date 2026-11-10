cmake_minimum_required(VERSION 3.27)
project(Rant)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/get_cpm.cmake")

# Rant from the sibling checkout, where the C# part takes the IEC types from too, until a Rant
# release carries them. -DCPM_rant_SOURCE:PATH=<tree> builds against another one.
set(CPM_rant_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../rant" CACHE PATH "Rant's source tree")
CPMAddPackage(NAME rant
  GITHUB_REPOSITORY KosmosisDire/Rant
  VERSION 0.0.43
  EXCLUDE_FROM_ALL YES)

# Rant Math from the sibling checkout, which also holds the IEC functions the C# part takes.
# Its static library goes into this shared one, so it is position independent.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CPM_math_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../math-rant" CACHE PATH "Rant Math's source tree")
CPMAddPackage(NAME math
  GITHUB_REPOSITORY KosmosisDire/rant-math
  VERSION 0.0.3
  EXCLUDE_FROM_ALL YES)

# The eCLR glue plcncli generates from the C# blocks, the blocks and the mirror in src/.
file(GLOB Generated CONFIGURE_DEPENDS intermediate/code/*.cpp)
add_library(Rant SHARED
  src/Rant-cli.cpp
  src/Runtime-cli.cpp
  src/RantMath-cli.cpp
  src/blocks/runtime.cpp
  src/blocks/layout.cpp
  src/mirror/mirror.cpp
  src/mirror/sampler.cpp
  src/mirror/sync.cpp
  src/plc/browse.cpp
  src/plc/codec.cpp
  src/plc/model.cpp
  src/plc/tasks.cpp
  src/core/log.cpp
  ${Generated}
  # A cross build gets only the header only rant target, so the library compiles the anchor itself.
  ${rant_SOURCE_DIR}/dist/rant.cpp)
target_compile_features(Rant PRIVATE cxx_std_17)
target_link_libraries(Rant PRIVATE rant::rant rant_math pthread)

set(PLCNEXT_TOOLCHAIN_INSTALLDIR $ENV{PLCNEXT_TOOLCHAIN_INSTALLDIR} CACHE STRING "toolchain install dir")
set(ECLR_TOOLCHAIN_ROOT "${PLCNEXT_TOOLCHAIN_INSTALLDIR}/eCLR")
include("${ECLR_TOOLCHAIN_ROOT}/EclrNative3_4.cmake")

# Includes are written relative to src/ ("blocks/runtime.h", "plc/model.h").
target_include_directories(Rant PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/intermediate/code
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${math_SOURCE_DIR}/bindings/plcnext
  ${ECLR_TOOLCHAIN_ROOT}/${ECLR_VERSION}/include
  ${ECLR_TOOLCHAIN_ROOT}/${ECLR_VERSION}/include/${ECLR_TARGET_OS})

list(INSERT CMAKE_MODULE_PATH 0 "${ARP_TOOLCHAIN_CMAKE_MODULE_PATH}")
set_target_properties(Rant PROPERTIES INSTALL_RPATH $ORIGIN)

# Without --no-undefined a missing library links anyway and crashes the firmware at load.
target_link_options(Rant PRIVATE LINKER:--no-undefined)
target_link_options(Rant PRIVATE LINKER:--as-needed)

find_package(ArpDevice REQUIRED)
find_package(Arp REQUIRED)
target_link_libraries(Rant PRIVATE Arp::ALL)

string(REGEX REPLACE "^.*\\(([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+).*$" "\\1" _ARP_SHORT_DEVICE_VERSION ${ARP_DEVICE_VERSION})
install(TARGETS Rant
  LIBRARY DESTINATION ${ARP_DEVICE}_${_ARP_SHORT_DEVICE_VERSION}/$<CONFIG>/lib
  ARCHIVE DESTINATION ${ARP_DEVICE}_${_ARP_SHORT_DEVICE_VERSION}/$<CONFIG>/lib
  RUNTIME DESTINATION ${ARP_DEVICE}_${_ARP_SHORT_DEVICE_VERSION}/$<CONFIG>/bin)
unset(_ARP_SHORT_DEVICE_VERSION)
