cmake_minimum_required(VERSION 3.20)
project(rook_simulation VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_library(rooks_core src/board.cpp src/simulation.cpp)
target_include_directories(rooks_core PUBLIC include)
target_compile_features(rooks_core PUBLIC cxx_std_20)
target_link_libraries(rooks_core PUBLIC Threads::Threads)

add_executable(rooks src/main.cpp)
target_link_libraries(rooks PRIVATE rooks_core)

function(enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endif()
endfunction()
enable_warnings(rooks_core)
enable_warnings(rooks)

