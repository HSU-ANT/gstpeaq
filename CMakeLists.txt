cmake_minimum_required(VERSION 3.10)

project(peaq LANGUAGES C CXX)

# Set the project version
set(PROJECT_VERSION_MAJOR 0)
set(PROJECT_VERSION_MINOR 6)
set(PROJECT_VERSION_PATCH 1)
set(PROJECT_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")

# Set preprocessor definitions
add_definitions(-DPACKAGE_URL="https://github.com/HSU-ANT/gstpeaq")
add_definitions(-DPACKAGE_STRING="GstPEAQ 0.6.1")
add_definitions(-DPACKAGE="gstpeaq")
add_definitions(-DPACKAGE_NAME="GstPEAQ")
add_definitions(-DPACKAGE_BUGREPORT="https://github.com/HSU-ANT/gstpeaq/issues")
add_definitions(-DPACKAGE_VERSION="${PROJECT_VERSION}")

# Find GStreamer
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-base-1.0 gstreamer-fft-1.0)

# Include GStreamer directories
include_directories(${GST_INCLUDE_DIRS})
link_directories(${GST_LIBRARY_DIRS})

# Find additional dependencies
pkg_check_modules(GST_PLUGINS_BASE REQUIRED gstreamer-plugins-base-1.0)
include_directories(${GST_PLUGINS_BASE_INCLUDE_DIRS})
link_directories(${GST_PLUGINS_BASE_LIBRARY_DIRS})

# Add source files
set(SOURCES
    src/earmodel.c
    src/fbearmodel.c
    src/fftearmodel.c
    src/gstpeaq.c
    src/gstpeaqplugin.c
    src/leveladapter.c
    src/modpatt.c
    src/movaccum.c
    src/movs.c
    src/nn.c
    src/peaq.c
)

# Add include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# Define the executable target
add_executable(peaq ${SOURCES})

# Link libraries
target_link_libraries(peaq ${GST_LIBRARIES} ${GST_PLUGINS_BASE_LIBRARIES})
