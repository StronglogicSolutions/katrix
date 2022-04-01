cmake_minimum_required(VERSION 3.11...3.13)

option(HUNTER_ENABLED "Enable Hunter package manager" OFF)
include("cmake/HunterGate.cmake")
HunterGate(
    URL "https://github.com/cpp-pm/hunter/archive/v0.23.305.tar.gz"
    SHA1 "fc8d7a6dac2fa23681847b3872d88d3839b657b0"
    LOCAL
)

project(katrix
				VERSION 0.0.1)
set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard")
set(CMAKE_CXX_STANDARD_REQUIRED ON CACHE BOOL "Require C++ standard to be supported")
set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "compile as PIC by default")

add_subdirectory(third_party/mtxclient)

include_directories(third_party/mtxclient/include src)

set(SOURCE_FILES
	src/main.cpp
)

add_executable(katrix_bot ${SOURCE_FILES})
add_library(katrix STATIC ${SOURCE_FILES})


include(FetchContent)
FetchContent_Declare(
	coeurl
	GIT_REPOSITORY https://nheko.im/Nheko-Reborn/coeurl.git
	GIT_TAG        v0.1.2
	)
FetchContent_MakeAvailable(coeurl)
target_link_libraries(katrix_bot PUBLIC coeurl::coeurl matrix_client)
target_link_libraries(katrix PUBLIC coeurl::coeurl matrix_client)

