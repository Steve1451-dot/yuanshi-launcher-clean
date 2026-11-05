cmake_minimum_required(VERSION 3.18)

if (NOT DEFINED ENV{DEVKITPRO})
    message(FATAL_ERROR "DEVKITPRO is not set. Install devkitPro and export DEVKITPRO before building.")
endif ()

set(DEVKITPRO $ENV{DEVKITPRO})
set(CMAKE_TOOLCHAIN_FILE ${DEVKITPRO}/cmake/Switch.cmake CACHE FILEPATH "devkitPro Switch toolchain")

project(yuanshi-launcher LANGUAGES C CXX)
option(CLOUDGENSHIN_NATIVE "Boot the native Cloud Genshin integration (not GeForce NOW)" ON)
option(CLOUDGENSHIN_DEBUG_SYMBOLS "Keep source-level symbols in the matching ELF (not in the NRO)" OFF)

set(OPENNOW_VERSION "0.0.6" CACHE STRING "OpenNOW application version")
# One source of truth: when the wrapper script does not pass a version, derive
# the CloudGenshin label from app_version.hpp instead of keeping a second
# hard-coded default that silently went stale.
if (NOT DEFINED CLOUDGENSHIN_VERSION)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/app/src/app_version.hpp" _cloudgenshin_version_header)
    string(REGEX MATCH "#define CLOUDGENSHIN_APP_VERSION \"([0-9]+\\.[0-9]+\\.[0-9]+)\""
        _cloudgenshin_version_match "${_cloudgenshin_version_header}")
    if (NOT _cloudgenshin_version_match)
        message(FATAL_ERROR "Unable to read CLOUDGENSHIN_APP_VERSION from app/src/app_version.hpp")
    endif ()
    set(CLOUDGENSHIN_VERSION "${CMAKE_MATCH_1}")
endif ()
if (NOT CLOUDGENSHIN_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "CLOUDGENSHIN_VERSION must use major.minor.patch format")
endif ()
if (NOT OPENNOW_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "OPENNOW_VERSION must use major.minor.patch format")
endif ()
message(STATUS "CloudGenshin version: ${CLOUDGENSHIN_VERSION}")

set(PLATFORM_SWITCH ON)
# One Deko3D application supports both NVDEC zero-copy and software-decoded
# YUV uploads. Decoder selection is a runtime setting rather than a build type.
set(USE_GLFW OFF CACHE BOOL "Disable GLFW for unified Deko3D" FORCE)
set(USE_DEKO3D ON CACHE BOOL "Use Deko3D on Switch" FORCE)
set(BOREALIS_USE_DEKO3D ON CACHE BOOL "Use Borealis Deko3D driver" FORCE)
set(USE_GLES2 OFF CACHE BOOL "Disable GLES for unified Deko3D" FORCE)
set(USE_SDL2 OFF)
set(USE_GLES3 OFF CACHE BOOL "Disable OpenGL ES 3 on Switch")
set(BRLS_UNITY_BUILD OFF CACHE BOOL "Disable Borealis unity builds")
set(FMT_OS OFF CACHE BOOL "Disable fmt OS-specific APIs for devkitA64")

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
pkg_check_modules(JANSSON REQUIRED IMPORTED_TARGET jansson)

add_subdirectory(extern/borealis/library)
add_subdirectory(extern/libpeer)
if(CLOUDGENSHIN_NATIVE)
    # Modern browser-compatible DTLS certificate for the native cloud build.
    target_compile_definitions(peer PRIVATE CONFIG_DTLS_USE_ECDSA=1)
endif()

target_include_directories(borealis SYSTEM AFTER PUBLIC ${DEVKITPRO}/portlibs/switch/include)

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    app/src/*.cpp
    app/src/*.c
)

set(PLATFORM_SOURCES)

if (PLATFORM_SWITCH AND NOT CLOUDGENSHIN_NATIVE)
    list(APPEND PLATFORM_SOURCES
        extern/borealis/library/lib/platforms/switch/switch_wrapper.c
    )
endif ()

add_executable(${PROJECT_NAME} ${APP_SOURCES} ${PLATFORM_SOURCES})

target_include_directories(${PROJECT_NAME}
    PRIVATE
        app/src
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/borealis/library/lib/extern/glfw/deps
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/ffmpeg/include
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/libpeer/src
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/libpeer/third_party/mbedtls/include
)

# Prefer the in-tree libpeer mbedtls/srtp build over portlibs copies that
# share the same -lmbedtls/-lmbedcrypto names.
target_link_directories(${PROJECT_NAME}
    BEFORE PRIVATE
        ${CMAKE_BINARY_DIR}/dist/lib
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/ffmpeg/lib
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    PLATFORM_SWITCH
    __SWITCH__
    HAS_SOCKLEN_T=1
    OPENNOW_APP_VERSION="${OPENNOW_VERSION}"
)
if (CLOUDGENSHIN_NATIVE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE CLOUDGENSHIN_NATIVE CLOUDGENSHIN_APP_VERSION="${CLOUDGENSHIN_VERSION}")
    if(CLOUDGENSHIN_DEBUG_SYMBOLS)
        target_compile_options(${PROJECT_NAME} PRIVATE -g2)
        target_compile_options(peer PRIVATE -g2)
    endif()
endif ()
if (PLATFORM_SWITCH)
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_DK_RENDERER BOREALIS_USE_DEKO3D OPENNOW_ENABLE_NVDEC)
    # uam is the Deko3D shader compiler. It is a host tool that is not part of
    # every devkitPro install (the Linux package ships it, the Windows bundle
    # can lack it). The compiled .dksh outputs are deterministic from the GLSL
    # sources, so a configure without uam reuses the shaders already present in
    # the build tree instead of failing the whole build. A clean build tree
    # still requires uam, and that case reports the missing tool explicitly.
    find_program(NX_UAM_EXE NAMES uam HINTS "${DEVKITPRO}/tools/bin")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/resources/shaders")
    set(BOREALIS_DEKO_SHADER_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/extern/borealis/library/lib/extern/nanovg/deko3d/shaders")
    if (NX_UAM_EXE)
        execute_process(COMMAND ${NX_UAM_EXE} -s vert
            -o "${CMAKE_BINARY_DIR}/resources/shaders/fill_vsh.dksh"
            "${BOREALIS_DEKO_SHADER_DIR}/fill_vsh.glsl"
            COMMAND_ERROR_IS_FATAL ANY)
        execute_process(COMMAND ${NX_UAM_EXE} -s frag
            -o "${CMAKE_BINARY_DIR}/resources/shaders/fill_aa_fsh.dksh"
            "${BOREALIS_DEKO_SHADER_DIR}/fill_aa_fsh.glsl"
            COMMAND_ERROR_IS_FATAL ANY)
        execute_process(COMMAND ${NX_UAM_EXE} -s frag
            -o "${CMAKE_BINARY_DIR}/resources/shaders/fill_fsh.dksh"
            "${BOREALIS_DEKO_SHADER_DIR}/fill_fsh.glsl"
            COMMAND_ERROR_IS_FATAL ANY)
        execute_process(COMMAND ${NX_UAM_EXE} -s vert
            -o "${CMAKE_BINARY_DIR}/resources/shaders/opennow_video_vsh.dksh"
            "${CMAKE_CURRENT_SOURCE_DIR}/app/src/stream/deko3d/basic_vsh.glsl"
            COMMAND_ERROR_IS_FATAL ANY)
        execute_process(COMMAND ${NX_UAM_EXE} -s frag
            -o "${CMAKE_BINARY_DIR}/resources/shaders/opennow_video_fsh.dksh"
            "${CMAKE_CURRENT_SOURCE_DIR}/app/src/stream/deko3d/texture_fsh.glsl"
            COMMAND_ERROR_IS_FATAL ANY)
    else ()
        set(_opennow_required_shaders
            fill_vsh.dksh fill_aa_fsh.dksh fill_fsh.dksh
            opennow_video_vsh.dksh opennow_video_fsh.dksh)
        set(_opennow_missing_shaders)
        foreach(_shader IN LISTS _opennow_required_shaders)
            if (NOT EXISTS "${CMAKE_BINARY_DIR}/resources/shaders/${_shader}")
                list(APPEND _opennow_missing_shaders "${_shader}")
            endif ()
        endforeach ()
        if (_opennow_missing_shaders)
            message(FATAL_ERROR
                "uam was not found and these Deko3D shaders are not prebuilt in "
                "${CMAKE_BINARY_DIR}/resources/shaders: ${_opennow_missing_shaders}. "
                "Install uam or configure a build tree that already contains them.")
        endif ()
        message(STATUS "uam not found: reusing prebuilt Deko3D shaders")
    endif ()
endif ()

target_compile_options(${PROJECT_NAME}
    PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
        -ffunction-sections
        -fdata-sections
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        borealis
        PkgConfig::CURL
        PkgConfig::JANSSON
        peer
        -lavformat
        -lavcodec
        -lavutil
        -lswresample
        z
)

set(PROJECT_AUTHOR "OpenCloudGaming")
set(PROJECT_DISPLAY_NAME "OpenNOW")
set(PROJECT_VERSION_STRING "${OPENNOW_VERSION}")
if (CLOUDGENSHIN_NATIVE)
    set(PROJECT_AUTHOR "Steve1451-dot")
    set(PROJECT_DISPLAY_NAME "yuanshi launcher")
    set(PROJECT_VERSION_STRING "${CLOUDGENSHIN_VERSION}")
endif ()
set(PROJECT_TITLEID "05004F4E4F575358")
set(PROJECT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/icon.jpg)
set(PROJECT_ROMFS_DIR ${CMAKE_BINARY_DIR}/romfs)
set(PROJECT_FORWARDER_COPY_COMMAND)
if (NOT CLOUDGENSHIN_NATIVE)
    set(PROJECT_FORWARDER_COPY_COMMAND
        COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/opennow-forwarder-installer/opennow-forwarder-installer.nro
            ${PROJECT_ROMFS_DIR}/shortcut/OpenNOWForwarderInstaller.nro)
endif ()

add_executable(OpenNOWShortcut app/shortcut/main.c)
target_compile_options(OpenNOWShortcut PRIVATE -Wall -Wextra -Werror)

# nacptool currently fills only the first 12 NACP language slots. Fill the
# remaining Russian, Korean and Chinese slots from the primary title so the
# launcher does not show an empty name in those system languages.
find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_custom_target(OpenNOWShortcut.nro
    DEPENDS OpenNOWShortcut
    COMMAND ${NX_NACPTOOL_EXE}
        --create
        "OpenNOW Shortcut"
        "${PROJECT_AUTHOR}"
        "${PROJECT_VERSION_STRING}"
        OpenNOWShortcut.nacp
        --titleid=05004F4E53484F52
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/fill-nacp-languages.py
        OpenNOWShortcut.nacp
    COMMAND ${NX_ELF2NRO_EXE}
        OpenNOWShortcut.elf
        OpenNOWShortcut.nro
        --icon=${PROJECT_ICON}
        --nacp=OpenNOWShortcut.nacp
)

add_custom_target(${PROJECT_NAME}.nro
    DEPENDS ${PROJECT_NAME} OpenNOWShortcut.nro
    COMMAND ${NX_NACPTOOL_EXE}
        --create
        "${PROJECT_DISPLAY_NAME}"
        "${PROJECT_AUTHOR}"
        "${PROJECT_VERSION_STRING}"
        ${PROJECT_NAME}.nacp
        --titleid=${PROJECT_TITLEID}
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/scripts/fill-nacp-languages.py
        ${PROJECT_NAME}.nacp
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${PROJECT_ROMFS_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_ROMFS_DIR}/img
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_ROMFS_DIR}/shortcut
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_BINARY_DIR}/OpenNOWShortcut.nro
        ${PROJECT_ROMFS_DIR}/shortcut/OpenNOWShortcut.nro
    ${PROJECT_FORWARDER_COPY_COMMAND}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/font
        ${PROJECT_ROMFS_DIR}/font
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/certs
        ${PROJECT_ROMFS_DIR}/certs
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n
        ${PROJECT_ROMFS_DIR}/i18n
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon
        ${PROJECT_ROMFS_DIR}/icon
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/img
        ${PROJECT_ROMFS_DIR}/img
    # Keep existing source files untouched, but do not redistribute official
    # game key art in new binaries. Homepage customization reads user SD files.
    COMMAND ${CMAKE_COMMAND} -E rm -f
        ${PROJECT_ROMFS_DIR}/img/cloudgenshin-official-bg.jpg
        ${PROJECT_ROMFS_DIR}/img/cloud-sr.jpg
        ${PROJECT_ROMFS_DIR}/img/cloud-zzz-bg.png
        ${PROJECT_ROMFS_DIR}/img/cloud-endfield.jpg
        ${PROJECT_ROMFS_DIR}/img/CLOUD-BACKGROUND-SOURCE.md
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/material
        ${PROJECT_ROMFS_DIR}/material
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_BINARY_DIR}/resources/shaders
        ${PROJECT_ROMFS_DIR}/shaders
    COMMAND ${NX_ELF2NRO_EXE}
        ${PROJECT_NAME}.elf
        ${PROJECT_NAME}.nro
        --icon=${PROJECT_ICON}
        --nacp=${PROJECT_NAME}.nacp
        --romfsdir=${PROJECT_ROMFS_DIR}
)
