
cmake_minimum_required(VERSION 3.21)

project(SvgApp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV REQUIRED)

add_library(svg-converter
    src/converter.cpp
)
target_link_libraries(svg-converter PUBLIC ${OpenCV_LIBS})

add_executable(svg-app
    src/main.cpp
)

target_link_libraries(svg-app PRIVATE svg-converter)

message(STATUS "OpenCV version: ${OpenCV_VERSION}")
message(STATUS "OpenCV include dirs: ${OpenCV_INCLUDE_DIRS}")
