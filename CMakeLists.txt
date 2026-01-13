cmake_minimum_required(VERSION 3.10)
project(MastheadCamera CXX)

# Find PkgConfig first
find_package(PkgConfig REQUIRED)

# Use IMPORTED_TARGET to create the PkgConfig:: targets
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET gstreamer-1.0)
pkg_check_modules(GSTREAMER_VIDEO REQUIRED IMPORTED_TARGET gstreamer-video-1.0)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)

# Standard and requirements
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCE_FILES 
    src/main.cpp 
    src/attitude.cpp
    src/video.cpp)

add_executable(${PROJECT_NAME} ${SOURCE_FILES})

target_include_directories(${PROJECT_NAME} PRIVATE include)

# Link using the namespaced PkgConfig targets
target_link_libraries(${PROJECT_NAME} 
    PRIVATE 
    PkgConfig::GSTREAMER 
    PkgConfig::GSTREAMER_VIDEO 
    PkgConfig::CAIRO
)
