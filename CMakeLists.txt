cmake_minimum_required(VERSION 3.16)
project(RecklessDrivin C)

set(CMAKE_C_STANDARD 99)

# Address Sanitizer option (helps find memory corruption)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
if(ENABLE_ASAN)
    if(MSVC)
        add_compile_options(/fsanitize=address)
    else()
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
    endif()
endif()

# SDL2: Try from source first (for portable builds), fall back to find_package
set(SDL2_FOUND FALSE)
set(SDL2_SOURCE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/SDL2"
    "${CMAKE_CURRENT_SOURCE_DIR}/../SDL2-2.26.4/SDL2-2.26.4"
    "$ENV{USERPROFILE}/Downloads/SDL2-2.26.4/SDL2-2.26.4"
)
foreach(SDL2_SRC_DIR ${SDL2_SOURCE_DIRS})
    if(EXISTS "${SDL2_SRC_DIR}/CMakeLists.txt")
        message(STATUS "Building SDL2 from source: ${SDL2_SRC_DIR}")
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TEST OFF CACHE BOOL "" FORCE)
        add_subdirectory("${SDL2_SRC_DIR}" "${CMAKE_BINARY_DIR}/SDL2" EXCLUDE_FROM_ALL)
        set(SDL2_FOUND TRUE)
        set(SDL2_FROM_SOURCE TRUE)
        break()
    endif()
endforeach()
if(NOT SDL2_FOUND)
    find_package(SDL2 QUIET)
    if(NOT SDL2_FOUND)
        message(FATAL_ERROR "SDL2 not found. Install SDL2 development libraries or place SDL2 source in ./SDL2/")
    endif()
endif()

find_package(OpenGL REQUIRED)

# Source files
set(GAME_SOURCES
    source/main.c
    source/error.c
    source/gameendseq.c
    source/gameframe.c
    source/gameinitexit.c
    source/high.c
    source/initexit.c
    source/objectCollision.c
    source/objectControl.c
    source/objectPhysics.c
    source/objects.c
    source/packs.c
    source/particlefx.c
    source/pause.c
    source/preferences.c
    source/random.c
    source/register.c
    source/renderframe.c
    # source/renderframezoom.c  -- excluded: duplicate 8-bit zoomed functions already in renderframe.c/roaddraw.c
    source/rle.c
    source/roaddraw.c
    source/screenfx.c
    source/sprites.c
    source/textfx.c
)

# Platform replacement files
set(PLATFORM_SOURCES
    platform/mac_compat.c
    platform/platform_screen.c
    platform/platform_sound.c
    platform/platform_input.c
    platform/platform_interface.c
    platform/quickdraw.c
)

# LZRW library
set(LZRW_SOURCES
    libs/lzrw/lzrw.c
    libs/lzrw/lzrw3a.c
)

# Excluded from build: HIDAccess.c, IsPortListValid.c, appleevents.c
# Also excluded: source/screen.c, source/sound.c, source/input.c, source/interface.c
# (replaced by platform/ equivalents)

# Windows application icon
if(WIN32)
    set(WIN_RESOURCES app-icon.rc)
endif()

add_executable(RecklessDrivin
    ${GAME_SOURCES}
    ${PLATFORM_SOURCES}
    ${LZRW_SOURCES}
    ${WIN_RESOURCES}
)

target_include_directories(RecklessDrivin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/headers
    ${CMAKE_CURRENT_SOURCE_DIR}/platform
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/lzrw
)

if(SDL2_FROM_SOURCE)
    target_include_directories(RecklessDrivin PRIVATE
        ${SDL2_SRC_DIR}/include
    )
    target_link_libraries(RecklessDrivin PRIVATE
        SDL2-static SDL2main
        OpenGL::GL
    )
else()
    target_include_directories(RecklessDrivin PRIVATE
        ${SDL2_INCLUDE_DIRS}
    )
    target_link_libraries(RecklessDrivin PRIVATE
        ${SDL2_LIBRARIES}
        OpenGL::GL
    )
endif()

# Windows-specific
if(WIN32)
    target_link_libraries(RecklessDrivin PRIVATE shlwapi)
    # Don't show console window in release builds
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set_target_properties(RecklessDrivin PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
endif()

# Linux-only math library
if(UNIX AND NOT APPLE)
    target_link_libraries(RecklessDrivin PRIVATE m)
endif()

# Resource extractor tool (standalone, no SDL dependency)
add_executable(res_extract tools/res_extract.c)
target_include_directories(res_extract PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/platform
)

# Custom target to extract resources
add_custom_target(extract_resources
    COMMAND res_extract "${CMAKE_CURRENT_SOURCE_DIR}/Data" "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    DEPENDS res_extract
    COMMENT "Extracting resources from Data file"
)
