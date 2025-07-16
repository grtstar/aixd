cmake_minimum_required(VERSION 3.10)

# Set the project name
project(aixd)


if (NOT DEFINED ENV{COMMON_DIR})
	message(FATAL_ERROR "Please set COMMON_DIR first")
else()
	message(STATUS "COMMON_DIR is $ENV{COMMON_DIR}")
endif()
set(CMAKE_FIND_ROOT_PATH $ENV{COMMON_DIR})
list(APPEND CMAKE_PREFIX_PATH $ENV{COMMON_DIR})

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)
add_link_options("LINKER:-rpath-link,$ENV{COMMON_DIR}/lib")

add_compile_options(-g -Wall -Wextra 
                    -Wuninitialized 
                    -Wmaybe-uninitialized
                    -Wno-unused-parameter
                    -fno-omit-frame-pointer)
add_compile_options(
                    -DISP_HW_V30 -DRKPLATFORM=ON -DARCH64=OFF
                    -DUAPI2
                    -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 
                    )
add_compile_options(-O0)

find_package(spdlog REQUIRED)
find_package(lcm REQUIRED)
find_package(PortAudio REQUIRED)
# Include directories
include_directories($ENV{COMMON_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(src)
include_directories(include)

# Add the executable
add_executable(aixd src/ai.cpp src/rksound.cpp)

# Link libraries
# lcm
target_link_libraries(${PROJECT_NAME} lcm)
# spdlog
target_link_libraries(${PROJECT_NAME} spdlog::spdlog)
# pthread
target_link_libraries(${PROJECT_NAME} pthread)
# ssl
target_link_libraries(${PROJECT_NAME} ssl)
target_link_libraries(${PROJECT_NAME} crypto)
# portaudio
target_link_libraries(${PROJECT_NAME} $ENV{COMMON_DIR}/lib/libportaudio.a)
# alsa
target_link_libraries(${PROJECT_NAME} asound)
