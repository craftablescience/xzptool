cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

# set up project
project(xzptool
        DESCRIPTION "A tool to convert .xz_ files to/from editable .xzp pack files."
        VERSION 1.0.1
        HOMEPAGE_URL "https://github.com/craftablescience/xzptool")
set(PROJECT_ORGANIZATION_NAME "craftablescience" CACHE INTERNAL "" FORCE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# add helpers
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/helpers")
include(CS_All)

# initial VS integration
cs_setup_vs_defaults(STARTUP_PROJECT "${PROJECT_NAME}")

# set common compile options
option(XZPTOOL_USE_LTO "Compile ${PROJECT_NAME} with LTO/IPO" OFF)
cs_configure_defaults(XZPTOOL_USE_LTO)

# external libraries
cs_include_directory(ext)

# xzptool
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/config.h.in" "${CMAKE_CURRENT_SOURCE_DIR}/src/config.h")
add_executable(${PROJECT_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/src/xzptool.cpp")
cs_configure_target(${PROJECT_NAME} LOGO MANIFEST)
target_link_libraries(${PROJECT_NAME} PRIVATE argparse::argparse bufferstream jcalg1)
if(WIN32 AND MSVC)
    target_link_options(${PROJECT_NAME} PRIVATE "/SAFESEH:NO")
endif()
