
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
target_include_directories(svg-converter PUBLIC ${OpenCV_INCLUDE_DIRS})

if(EMSCRIPTEN)
    add_executable(svg-converter-wasm src/wasm.cpp)

    target_link_libraries(svg-converter-wasm PRIVATE svg-converter)
    target_link_options(svg-converter-wasm PRIVATE
        "-lembind"
        "-sMODULARIZE=1"
        "-sEXPORT_ES6=1"
        "-sEXPORT_NAME=createSvgConverterModule"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sENVIRONMENT=web,worker,node"
    )

    set(SVG_WASM_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/dist/wasm"
        CACHE PATH "Output directory for the WASM module")
    set_target_properties(svg-converter-wasm PROPERTIES
        OUTPUT_NAME "svg-converter"
        SUFFIX ".js"
        RUNTIME_OUTPUT_DIRECTORY "${SVG_WASM_OUTPUT_DIR}"
    )
else()
    add_executable(svg-app
        src/main.cpp
    )

    target_link_libraries(svg-app PRIVATE svg-converter)
endif()

message(STATUS "OpenCV version: ${OpenCV_VERSION}")
message(STATUS "OpenCV include dirs: ${OpenCV_INCLUDE_DIRS}")
