cmake_minimum_required(VERSION 3.18)
set(CMAKE_TOOLCHAIN_FILE "$ENV{DEVKITPRO}/cmake/Switch.cmake")
project(switch_motion_probe LANGUAGES C CXX)

# Standalone, credential-free diagnostic. It deliberately uses the production
# sampler and wire encoder without starting the UI, cloud session or network.
add_executable(switch_motion_probe main.cpp
    ../../app/src/mihoyo/input_packet.cpp
    ../../app/src/mihoyo/game_control.cpp)
target_compile_features(switch_motion_probe PRIVATE cxx_std_20)
target_compile_options(switch_motion_probe PRIVATE -Wall -Wextra -Werror)
target_include_directories(switch_motion_probe PRIVATE ../../app/src)
nx_create_nro(switch_motion_probe NOICON)
