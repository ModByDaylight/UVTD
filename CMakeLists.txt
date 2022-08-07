cmake_minimum_required(VERSION 3.18)
message("Project: UnrealVTableDumper")

# We're only using C++20 features but we need to use C++23 so that we get std::format which only exists in the "latest" flag in MSVC
set(CMAKE_CXX_STANDARD 23)

set(TARGET UnrealVTableDumper)
project(${TARGET})

set(PRIVATE_COMPILE_DEFINITIONS
        $<${MAKE_DEPENDENCIES_SHARED}:RC_EXPORT RC_FUNCTION_EXPORTS>
        $<${MAKE_DEPENDENCIES_STATIC}:RC_FUNCTION_BUILD_STATIC>)
set(PRIVATE_COMPILE_OPTIONS /MP $<$<CONFIG:RELEASE>:/Zi> /W3 /wd4005 /wd4251)
set(PRIVATE_LINK_OPTIONS /DEBUG:FULL)
set(PUBLIC_COMPILE_FEATURES cxx_std_20)

# Tell WinAPI macros to map to unicode functions instead of ansi
add_compile_definitions(_UNICODE)
add_compile_definitions(UNICODE)

set(${TARGET}_Sources
        "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/TypeLayoutGenerator.cpp")

add_executable(${TARGET} ${${TARGET}_Sources})
target_include_directories(${TARGET} PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> /include> "${CMAKE_CURRENT_SOURCE_DIR}/external_deps/DIA_SDK/include")
target_compile_options(${TARGET} PRIVATE ${PRIVATE_COMPILE_OPTIONS})
target_link_options(${TARGET} PRIVATE ${PRIVATE_LINK_OPTIONS})
target_compile_features(${TARGET} PUBLIC ${PUBLIC_COMPILE_FEATURES})
target_link_libraries(${TARGET} PRIVATE ${UVTD_LINK_WITH_LIBRARIES} ${UVTD_LINK_WITH_INTERFACE_LIBRARIES} "${CMAKE_CURRENT_SOURCE_DIR}/external_deps/DIA_SDK/lib/amd64/diaguids.lib")

