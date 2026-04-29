cmake_minimum_required(VERSION 3.16)
project(evdi-control LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(evdi-control
    src/display_backend.cpp
    src/edid.cpp
    src/evdi_loader.cpp
    src/main.cpp
    src/modeline.cpp
    src/state_file.cpp
    src/util.cpp
)

target_include_directories(evdi-control PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(evdi-control PRIVATE
    dl
)
