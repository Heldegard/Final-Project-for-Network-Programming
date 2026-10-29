cmake_minimum_required(VERSION 3.20)
project(lankvm VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 UNICODE _UNICODE)

if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra)
endif()

set(LANKVM_CORE_SOURCES
    src/common/log.cpp
    src/common/config.cpp
    src/common/crypto.cpp
    src/common/protocol.cpp
    src/common/net.cpp
    src/common/stream.cpp
    src/common/clipboard.cpp
    src/common/clipxfer.cpp
    src/common/inject.cpp
    src/master/master.cpp
    src/master/dropzone.cpp
    src/agent/agent.cpp
    src/app/tray.cpp
    src/app/settings.cpp
)

add_library(lankvm_core STATIC ${LANKVM_CORE_SOURCES})
target_include_directories(lankvm_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(lankvm_core PUBLIC ws2_32 bcrypt shell32 ole32 oleaut32 user32 gdi32 comctl32 shlwapi iphlpapi)

add_executable(lankvm src/app/main.cpp)
target_link_libraries(lankvm PRIVATE lankvm_core)

enable_testing()

add_executable(lankvm_tests tests/tests.cpp)
target_link_libraries(lankvm_tests PRIVATE lankvm_core)
add_test(NAME core COMMAND lankvm_tests)

add_executable(loopback_test tests/loopback_test.cpp)
target_link_libraries(loopback_test PRIVATE lankvm_core)
add_test(NAME loopback COMMAND loopback_test)

add_executable(file_transfer_test tests/file_transfer_test.cpp)
target_link_libraries(file_transfer_test PRIVATE lankvm_core)
add_test(NAME file_transfer COMMAND file_transfer_test)

if(MINGW)
    foreach(target lankvm lankvm_tests loopback_test file_transfer_test)
        target_link_options(${target} PRIVATE -static)
    endforeach()
endif()
