cmake_minimum_required(VERSION 3.16)
project(jcalg1)
set(CMAKE_CXX_STANDARD 11)

# Create library
if(NOT WIN32)
    message(STATUS "Note that JCalg1 library was only ever compiled as a 32-bit Windows library. You'd better be cross-compiling right now!")
endif()
add_library(${PROJECT_NAME} INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include/jcalg1.hpp")

target_link_libraries(${PROJECT_NAME} INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/lib/jcalg1_static.lib")

target_include_directories(${PROJECT_NAME} INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
