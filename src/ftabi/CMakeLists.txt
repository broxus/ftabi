set(SUBPROJ_NAME                          ftabi)
set(${SUBPROJ_NAME}_NAMESPACE             ftabi)

set(${SUBPROJ_NAME}_CXX_STANDARD          17)
set(${SUBPROJ_NAME}_CXX_EXTENSIONS        OFF)
set(${SUBPROJ_NAME}_CXX_STANDARD_REQUIRED YES)

set(${SUBPROJ_NAME}_MAJOR_VERSION         0)
set(${SUBPROJ_NAME}_MINOR_VERSION         0)
set(${SUBPROJ_NAME}_PATCH_VERSION         1)

# Insert here your source files
set(${SUBPROJ_NAME}_HEADERS
    "Abi.hpp")

set(${SUBPROJ_NAME}_SOURCES
    "Abi.cpp")

# ############################################################### #
# Options ####################################################### #
# ############################################################### #

include(OptionHelpers)
generate_basic_options_library(${SUBPROJ_NAME})

# ############################################################### #
# Library version ############################################### #
# ############################################################### #

set(${SUBPROJ_NAME}_VERSION
    ${${SUBPROJ_NAME}_MAJOR_VERSION}.${${SUBPROJ_NAME}_MINOR_VERSION}.${${SUBPROJ_NAME}_PATCH_VERSION})

# Set build type to library target
if(${SUBPROJ_NAME}_BUILD_SHARED)
    set(${SUBPROJ_NAME}_TARGET_TYPE "SHARED")
else()
    set(${SUBPROJ_NAME}_TARGET_TYPE "STATIC")
endif()

string(TOLOWER ${${SUBPROJ_NAME}_TARGET_TYPE} ${SUBPROJ_NAME}_TARGET_TYPE_LOWER)


# ############################################################### #
# Set all target sources ######################################## #
# ############################################################### #

set(
    ${SUBPROJ_NAME}_ALL_SRCS
    ${${SUBPROJ_NAME}_HEADERS}
    ${${SUBPROJ_NAME}_SOURCES})

# ############################################################### #
# Create target for build ####################################### #
# ############################################################### #

# Library target
add_library(
    ${SUBPROJ_NAME}
    ${${SUBPROJ_NAME}_TARGET_TYPE}
    ${${SUBPROJ_NAME}_ALL_SRCS})

# Enable C++ standard
set_target_properties(
    ${SUBPROJ_NAME} PROPERTIES
    CXX_STANDARD          ${${SUBPROJ_NAME}_CXX_STANDARD}
    CXX_EXTENSIONS        ${${SUBPROJ_NAME}_CXX_EXTENSIONS}
    CXX_STANDARD_REQUIRED ${${SUBPROJ_NAME}_CXX_STANDARD_REQUIRED})

set_target_properties(
    ${SUBPROJ_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib"
    OUTPUT_NAME              "${SUBPROJ_NAME}$<$<CONFIG:Debug>:d>")

target_include_directories(
    ${SUBPROJ_NAME}
    PRIVATE   $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
              $<INSTALL_INTERFACE:include>)

target_link_libraries(${SUBPROJ_NAME} PUBLIC tddb tonlib)
