cmake_minimum_required(VERSION 3.18)
message("Project: UnrealVTableDumper")

# We're only using C++20 features but we need to use C++23 so that we get std::format which only exists in the "latest" flag in MSVC
set(CMAKE_CXX_STANDARD 23)

set(RC_VENDOR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor)

if (NOT EXISTS "${CMAKE_SOURCE_DIR}/local_repo_path.build_option")
    # Path to RC is not defined, so we'll assume that git submodules are being used
    set(RC_LOCAL_REPO_PATH_INTERNAL ${RC_VENDOR_DIR}/RC)
    set(RC_COMMON_CMAKE_FILE ${RC_VENDOR_DIR}/RC/CMake/RC_Common.cmake)
    message("Path to RC repo not defined. Using default: '${RC_LOCAL_REPO_PATH_INTERNAL}'")
else()
    file(READ ${CMAKE_SOURCE_DIR}/local_repo_path.build_option RC_LOCAL_REPO_PATH_FROM_FILE)
    if (IS_ABSOLUTE ${RC_LOCAL_REPO_PATH_FROM_FILE})
        set(RC_LOCAL_REPO_PATH_INTERNAL ${RC_LOCAL_REPO_PATH_FROM_FILE})

    else ()
        set(RC_LOCAL_REPO_PATH_INTERNAL ${CMAKE_CURRENT_SOURCE_DIR}/${RC_LOCAL_REPO_PATH_FROM_FILE})
    endif ()
    set(RC_COMMON_CMAKE_FILE ${RC_LOCAL_REPO_PATH_INTERNAL}/CMake/RC_Common.cmake)
    message("Path for the 'RC' repo overridden, RC_LOCAL_REPO_PATH=${RC_LOCAL_REPO_PATH_INTERNAL}")
endif()

set(TARGET UnrealVTableDumper)
project(${TARGET})

include(${RC_COMMON_CMAKE_FILE})

# Tell WinAPI macros to map to unicode functions instead of ansi
add_compile_definitions(_UNICODE)
add_compile_definitions(UNICODE)

set(SOURCES
        "src/main.cpp"
        #"src/UVTD_DbgHelp.cpp"
        "src/UVTD_DIA.cpp"
        )

# We don't ever want this to be statically linked because we need to actually have a dll to inject
message("Creating shared ${TARGET} library")
#add_library(${TARGET} SHARED ${SOURCES})
add_executable(${TARGET} ${SOURCES})

if (${CMAKE_BUILD_TYPE} MATCHES Release)
    target_compile_options(${TARGET} PUBLIC /Zi)
endif()

target_compile_options(${TARGET} PUBLIC /MP /W4)
target_link_options(${TARGET} PUBLIC /DEBUG:FULL)

add_rc_dependencies_to_targets(
        TARGETS ${TARGET}
        DEPENDENCIES
        #Unreal
        Input
        Helpers
        Function

        # Sub-dependencies that needs to get linked if UVTD is built as a dynamic library and RC is built as a static library
        File
        DynamicOutput
        #SinglePassSigScanner
        #LuaRaw
)

target_include_directories(${TARGET} PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(${TARGET} PUBLIC cxx_std_20)

# Third-Party Dependencies
find_library(dbghelp NAMES dbghelp ${PROJECT_BINARY_DIR})
message(STATUS "find_library returned ${dbghelp}")
add_library(dbghelp SHARED IMPORTED)
set_property(TARGET dbghelp PROPERTY IMPORTED_LOCATION "${dbghelp}")
target_link_libraries(${TARGET} PRIVATE DbgHelp)

#target_link_libraries(${TARGET} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/external_deps/DIA_SDK/lib/amd64/diaguids.lib")
#target_include_directories(${TARGET} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/external_deps/DIA_SDK/include")
target_link_libraries(${TARGET} PRIVATE "D:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\DIA SDK\\lib\\amd64\\diaguids.lib")
target_include_directories(${TARGET} PRIVATE "D:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\DIA SDK\\include")
