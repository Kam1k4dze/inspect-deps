cmake_minimum_required(VERSION 3.28)
project(inspect-deps)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
    message(STATUS "CMAKE_BUILD_TYPE not specified — defaulting to Release")
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif ()

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

FetchContent_Declare(
        CLI11
        GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
        GIT_TAG v2.6.1
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(CLI11)

FetchContent_Declare(
        glaze
        GIT_REPOSITORY https://github.com/stephenberry/glaze.git
        GIT_TAG v6.5.0
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(glaze)

FetchContent_Declare(
        elfio
        GIT_REPOSITORY https://github.com/serge1/ELFIO.git
        GIT_TAG Release_3.12
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(elfio)


add_executable(inspect-deps main.cpp)
target_link_libraries(inspect-deps PRIVATE CLI11::CLI11 glaze::glaze elfio::elfio ${CMAKE_DL_LIBS})

target_link_options(inspect-deps PRIVATE -static-libgcc -static-libstdc++)

include(GNUInstallDirs)
install(TARGETS inspect-deps DESTINATION ${CMAKE_INSTALL_BINDIR})
