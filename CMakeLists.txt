cmake_minimum_required(VERSION 3.10)
set(CMAKE_BUILD_TYPE Debug)
project(sylar_coroutine)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE-CXX_FLAGS} -O0")

include_directories(${PROJECT_SOURCE_DIR})

find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_package(yaml-cpp REQUIRED)
find_package(OpenSSL REQUIRED)
if(OPENSSL_FOUND)
    include_directories(${OPENSSL_INCLUDE_DIR})
endif()

set(SYLAR_SOURCES
    sylar/mutex.cpp
    sylar/thread.cpp
    sylar/address.cpp
    sylar/socket.cpp
    sylar/stream.cpp
    sylar/socket_stream.cpp
    sylar/http/http_parser.cpp
    sylar/http/http_session.cpp
    sylar/http/http.cpp
    sylar/http/servlet.cpp
    sylar/http/http_server.cpp
    sylar/http/http_connection.cpp
    sylar/tcp_server.cpp
    sylar/bytearray.cpp
    sylar/config.cpp
    sylar/env.cpp
    sylar/util.cpp
    sylar/fiber.cpp
    sylar/scheduler.cpp
    sylar/iomanager.cpp
    sylar/timer.cpp
    sylar/hook.cpp
    sylar/fd_manager.cpp
)

function(ragelmaker src_rl outputlist outputdir)
    #Create a custom build step that will call ragel on the provided src_rl file.
    #The output .cpp file will be appended to the variable name passed in outputlist.

    get_filename_component(src_file ${src_rl} NAME_WE)

    set(rl_out ${outputdir}/${src_file}.rl.cc)

    #adding to the list inside a function takes special care, we cannot use list(APPEND...)
    #because the results are local scope only
    set(${outputlist} ${${outputlist}} ${rl_out} PARENT_SCOPE)

    #Warning: The " -S -M -l -C -T0  --error-format=msvc" are added to match existing window invocation
    #we might want something different for mac and linux
    add_custom_command(
        OUTPUT ${rl_out}
        COMMAND cd ${outputdir}
        COMMAND ragel ${CMAKE_CURRENT_SOURCE_DIR}/${src_rl} -o ${rl_out} -l -C -G2  --error-format=msvc
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${src_rl}
        )
    set_source_files_properties(${rl_out} PROPERTIES GENERATED TRUE)
endfunction(ragelmaker)

ragelmaker(sylar/http/http11_parser.rl SYLAR_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/sylar/http)
ragelmaker(sylar/http/httpclient_parser.rl SYLAR_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/sylar/http)
ragelmaker(sylar/uri.rl SYLAR_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/sylar)

set(LIBS
        sylar
        pthread
        yaml-cpp
        ${OPENSSL_LIBRARIES}
        dl
)


add_library(sylar STATIC ${SYLAR_SOURCES})
target_link_libraries(sylar PRIVATE Threads::Threads)
include_directories(${Boost_INCLUDE_DIRS})
target_link_libraries(sylar PRIVATE Boost::boost Boost::system)
target_link_libraries(sylar PRIVATE yaml-cpp)
target_link_libraries(sylar PRIVATE OpenSSL::SSL)
target_link_libraries(sylar PRIVATE OpenSSL::Crypto)
# add_executable(test_thread tests/test_thread.cc)
# target_link_libraries(test_thread sylar)

# add_executable(test_util tests/test_util.cc)
# target_link_libraries(test_util sylar)

# add_executable(test_fiber tests/test_fiber.cc)
# target_link_libraries(test_fiber sylar)

# add_executable(test_scheduler tests/test_scheduler.cc)
# target_link_libraries(test_scheduler sylar)

# add_executable(test_iomanager tests/test_iomanager.cpp)
# target_link_libraries(test_iomanager sylar)

# add_executable(test_hook tests/test_hook.cpp)
# target_link_libraries(test_hook sylar)

# add_executable(test_address tests/test_address.cpp)
# target_link_libraries(test_address sylar)

# add_executable(test_socket tests/test_socket.cpp)
# target_link_libraries(test_socket sylar)

# add_executable(test_bytearray tests/test_bytearray.cpp)
# target_link_libraries(test_bytearray sylar)

# add_executable(test_http tests/test_http.cpp)
# target_link_libraries(test_http sylar)

# add_executable(test_http_parser tests/test_http_parser.cpp)
# target_link_libraries(test_http_parser sylar)

# add_executable(test_http_server tests/test_http_server.cpp)
# target_link_libraries(test_http_server sylar)

add_executable(test_http_connection tests/test_http_connection.cpp)
target_link_libraries(test_http_connection sylar)

# add_executable(test_uri tests/test_uri.cpp)
# target_link_libraries(test_uri sylar)