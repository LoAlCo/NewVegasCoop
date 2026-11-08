cmake_minimum_required(VERSION 3.25)
project(NVCoop LANGUAGES C CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "New Vegas is a 32-bit game: configure with the x86 preset (-A Win32).")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# The C++ runtime is built in, so the plugin works on PCs without Visual Studio (a debug build otherwise needs
# its debug runtime DLLs).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Optional: copy the built plugin into the game after each build.
# Set it in your own CMakeUserPresets.json (not committed), e.g. ".../Fallout New Vegas".
set(GAME_DIR "" CACHE PATH "Fallout New Vegas folder to copy the plugin into")

set(XNVSE ${CMAKE_SOURCE_DIR}/external/xNVSE)

# ENet: reliable networking over UDP
file(GLOB ENET_SOURCES ${CMAKE_SOURCE_DIR}/external/enet/*.c)
add_library(enet STATIC ${ENET_SOURCES})
target_include_directories(enet PUBLIC ${CMAKE_SOURCE_DIR}/external/enet/include)
target_compile_definitions(enet PRIVATE _WINSOCK_DEPRECATED_NO_WARNINGS _CRT_SECURE_NO_WARNINGS)
target_link_libraries(enet PUBLIC ws2_32 winmm)

# Core: everything that works without the game (shared by the plugin, tools and tests)
add_library(nvcoop_core STATIC
    src/log.cpp
    src/config.cpp
    src/net/protocol.cpp
    src/net/session.cpp
    src/net/steam_session.cpp
    src/coop/coop.cpp
    src/coop/containers.cpp
    src/coop/doors.cpp
    src/coop/objects.cpp
    src/coop/mods.cpp
    src/coop/mod_transfer.cpp
    src/coop/sha256.cpp
    src/coop/quests.cpp
    src/coop/smoothing.cpp
    src/coop/world_items.cpp
    src/coop/desync.cpp
    src/coop/chat.cpp
    src/coop/voice.cpp
    src/coop/game_mode.cpp
    src/coop/steer.cpp
    src/dev/inspect_text.cpp
    src/dev/lab_channel.cpp
)
target_include_directories(nvcoop_core PUBLIC src)
target_compile_definitions(nvcoop_core PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(nvcoop_core PUBLIC /W3 /utf-8)
target_link_libraries(nvcoop_core PUBLIC enet)

# The plugin the game loads
add_library(NVCoop SHARED
    src/main.cpp
    src/linked_saves.cpp
    src/teleporter.cpp
    src/downed.cpp
    src/chat_voice.cpp
    src/quick_start.cpp
    src/crash.cpp
    src/dll_blocker.cpp
    src/dev/second_copy.cpp
    src/dev/dump_game.cpp
    src/dev/commands.cpp
    src/speech.cpp
    src/voice_out.cpp
    src/display.cpp
    src/game/game.cpp
    src/game/scripts.cpp
    src/game/hooks.cpp
    src/game/popups.cpp
    src/game/say_hooks.cpp
    src/game/looks.cpp
    src/game/commands.cpp
    src/game/lockers.cpp
    src/game/escape_menu.cpp
    src/game/level_up.cpp
    src/game/perk_menu.cpp
    src/game/remove_all_items.cpp
    src/game/standins.cpp
    src/game/npcs.cpp
    src/game/far_cells.cpp
    src/game/doors.cpp
    src/game/objects.cpp
    src/game/terminals.cpp
    src/game/activations.cpp
    src/game/mines.cpp
    src/game/companions.cpp
    src/game/dialogue.cpp
    src/game/steam.cpp
    src/game/sleepwait.cpp
    src/game/containers.cpp
    src/game/items.cpp
    src/game/quests.cpp
    src/game/progress.cpp
    src/game/mover.cpp
    src/game/inspector.cpp
    src/game/mirror.cpp
    src/game/puppet.cpp
    src/exports.def
)
target_include_directories(NVCoop PRIVATE ${XNVSE} ${XNVSE}/nvse ${XNVSE}/nvse/nvse)
target_compile_definitions(NVCoop PRIVATE RUNTIME RUNTIME_VERSION=0x040020D0 _WINDOWS _USRDLL)
target_compile_options(NVCoop PRIVATE /FInvse/prefix.h /MP)
target_link_libraries(NVCoop PRIVATE nvcoop_core dbghelp psapi)
# A map file (build/<config>/NVCoop.map) turns addresses in crash dumps into our function names.
target_link_options(NVCoop PRIVATE /MAP)

if(GAME_DIR)
    add_custom_command(TARGET NVCoop POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GAME_DIR}/Data/NVSE/Plugins"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:NVCoop> "${GAME_DIR}/Data/NVSE/Plugins/"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GAME_DIR}/Data/MCM"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/data/MCM/NVCoop.json" "${GAME_DIR}/Data/MCM/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data/textures" "${GAME_DIR}/Data/Textures"
        COMMENT "Copying NVCoop.dll into the game")
endif()

# Fake friend: pretend second player for testing on one PC
add_executable(fakefriend tools/fakefriend/main.cpp)
target_link_libraries(fakefriend PRIVATE nvcoop_core)

# Tests
enable_testing()
add_executable(nvcoop_tests tests/test_main.cpp)
target_link_libraries(nvcoop_tests PRIVATE nvcoop_core)
add_test(NAME nvcoop_tests COMMAND nvcoop_tests)
