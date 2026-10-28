
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

# Input handling shared by the WASM binding and the gRPC server.
add_library(svg-engine-common
    src/engine_common.cpp
)

target_link_libraries(svg-engine-common PUBLIC ${OpenCV_LIBS})
target_include_directories(svg-engine-common PUBLIC ${OpenCV_INCLUDE_DIRS})

if(EMSCRIPTEN)
    add_executable(svg-converter-wasm src/wasm.cpp)

    target_link_libraries(svg-converter-wasm PRIVATE svg-converter svg-engine-common)
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

    option(SVG_BUILD_TESTS "Build the native C++ unit tests" OFF)
    if(SVG_BUILD_TESTS)
        enable_testing()
        add_executable(engine-common-test tests/cpp/engine_common_test.cpp)
        target_include_directories(engine-common-test PRIVATE src)
        target_link_libraries(engine-common-test PRIVATE svg-converter svg-engine-common)
        add_test(NAME engine-common-test COMMAND engine-common-test)
    endif()

    option(SVG_BUILD_SERVER "Build the native gRPC conversion server" OFF)
    if(SVG_BUILD_SERVER)
        find_package(Protobuf CONFIG QUIET)
        if(NOT Protobuf_FOUND)
            find_package(Protobuf REQUIRED)  # distro packages ship only the module
        endif()
        find_package(gRPC CONFIG REQUIRED)

        set(SVG_PROTO_DIR "${CMAKE_SOURCE_DIR}/proto")
        set(SVG_PROTO_GEN_DIR "${CMAKE_BINARY_DIR}/gen")
        file(MAKE_DIRECTORY "${SVG_PROTO_GEN_DIR}/svgconv/v1")

        set(SVG_PROTO_FILE "${SVG_PROTO_DIR}/svgconv/v1/svgconv.proto")
        set(SVG_PROTO_GEN_BASE "${SVG_PROTO_GEN_DIR}/svgconv/v1/svgconv")
        set(SVG_PROTO_GEN_SOURCES
            "${SVG_PROTO_GEN_BASE}.pb.cc"
            "${SVG_PROTO_GEN_BASE}.grpc.pb.cc"
        )
        add_custom_command(
            OUTPUT ${SVG_PROTO_GEN_SOURCES}
                   "${SVG_PROTO_GEN_BASE}.pb.h"
                   "${SVG_PROTO_GEN_BASE}.grpc.pb.h"
            COMMAND $<TARGET_FILE:protobuf::protoc>
                    --proto_path=${SVG_PROTO_DIR}
                    --cpp_out=${SVG_PROTO_GEN_DIR}
                    --grpc_out=${SVG_PROTO_GEN_DIR}
                    --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
                    ${SVG_PROTO_FILE}
            DEPENDS ${SVG_PROTO_FILE}
            COMMENT "Generating C++ protobuf/gRPC sources"
            VERBATIM
        )

        add_library(svgconv-proto STATIC ${SVG_PROTO_GEN_SOURCES})
        target_link_libraries(svgconv-proto PUBLIC protobuf::libprotobuf gRPC::grpc++)
        target_include_directories(svgconv-proto PUBLIC "${SVG_PROTO_GEN_DIR}")

        add_executable(svg-grpc-server server/grpc_server.cpp)
        target_include_directories(svg-grpc-server PRIVATE src)
        target_link_libraries(svg-grpc-server PRIVATE
            svg-converter svg-engine-common svgconv-proto)
    endif()
endif()

message(STATUS "OpenCV version: ${OpenCV_VERSION}")
message(STATUS "OpenCV include dirs: ${OpenCV_INCLUDE_DIRS}")
