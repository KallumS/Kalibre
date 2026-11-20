cmake_minimum_required(VERSION 3.22)

project(Kalibre VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
    # Apple silicon only (decision 0001). macOS 12 is the oldest JUCE 9 and
    # the plug-in SDKs all support for hosting.
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Apple silicon")
endif()

option(KALIBRE_BUILD_APP   "Build the app (downloads JUCE and the plug-in SDKs)" ON)
option(KALIBRE_BUILD_TESTS "Build the tests" ON)

#==============================================================================
# Lua 5.4, vendored: user scripts (decision 0019).
#==============================================================================
file(GLOB LUA_SOURCES ThirdParty/lua/*.c)
list(REMOVE_ITEM LUA_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/lua/lua.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/lua/luac.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/lua/onelua.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/lua/ltests.c)
# Built as C++ (decision 0068): an error in a script unwinds through C++ (the
# Lua tools' ImGui windows) as an exception, cleanly, instead of a longjmp.
set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE CXX)
add_library(KalibreLua STATIC ${LUA_SOURCES})
target_include_directories(KalibreLua PUBLIC ThirdParty/lua)
set_target_properties(KalibreLua PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
if(APPLE)
    target_compile_definitions(KalibreLua PRIVATE LUA_USE_MACOSX)
elseif(UNIX)
    target_compile_definitions(KalibreLua PRIVATE LUA_USE_LINUX)
endif()

#==============================================================================
# The core: the project, its edits, time, harmony, patterns, playback lists,
# modulators, capture and scripting. No JUCE (decision 0002): it builds and
# tests in seconds.
#==============================================================================
# The family's Lua engines (Engines/, decision 0062), embedded so the app needs
# nothing beside itself: a generated table of { name, bytes }.
file(GLOB_RECURSE KALIBRE_LUA_ENGINES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Engines/*.lua ${CMAKE_CURRENT_SOURCE_DIR}/Engines/*.jsfx
     ${CMAKE_CURRENT_SOURCE_DIR}/Engines/*.jsfx-inc ${CMAKE_CURRENT_SOURCE_DIR}/Engines/jsfx/Data/*)
set(EMBEDDED_LUA_CPP ${CMAKE_CURRENT_BINARY_DIR}/EmbeddedLua.cpp)
add_custom_command(
    OUTPUT ${EMBEDDED_LUA_CPP}
    COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_CURRENT_SOURCE_DIR}/Engines -DOUT=${EMBEDDED_LUA_CPP}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedLua.cmake
    DEPENDS ${KALIBRE_LUA_ENGINES} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedLua.cmake
    COMMENT "Embedding the Lua engines")

add_library(KalibreCore STATIC
    Source/Core/TempoMap.cpp
    Source/Core/Model.cpp
    Source/Core/Edit.cpp
    Source/Core/Roll.cpp
    Source/Core/Patterns.cpp
    Source/Core/Harmony.cpp
    Source/Core/Playback.cpp
    Source/Core/Modulation.cpp
    Source/Core/Capture.cpp
    Source/Core/ProjectFile.cpp
    Source/Core/Notation.cpp
    Source/Core/MusicXml.cpp
    Source/Core/Xml.cpp
    Source/Core/Comp.cpp
    Source/Core/MidiLearn.cpp
    Source/Core/Loudness.cpp
    Source/Core/Sync.cpp
    Source/Core/ControlSettings.cpp
    Source/Core/Mackie.cpp
    Source/Core/AudioTools.cpp
    Source/Core/Song.cpp
    Source/Core/Surround.cpp
    Source/Core/ProjectTools.cpp
    Source/Core/Hui.cpp
    Source/Core/Mpe.cpp
    Source/Core/Controllers.cpp
    Source/Core/AutoCc.cpp
    Source/Core/Ideas.cpp
    Source/Core/Articulations.cpp
    Source/Core/NoteSelect.cpp
    Source/Core/StepInput.cpp
    Source/Core/NoteSplit.cpp
    Source/Core/NoteFixer.cpp
    Source/Core/WorkClock.cpp
    Source/Core/DawProject.cpp
    Source/Core/Toolbox.cpp
    Source/Core/Trombolese.cpp
    Source/Core/Library.cpp
    Source/Core/Mcp.cpp
    ${EMBEDDED_LUA_CPP}
    Source/Script/LuaHost.cpp)
target_include_directories(KalibreCore PUBLIC Source/Core Source/Script ThirdParty)
target_link_libraries(KalibreCore PUBLIC KalibreLua)
set_target_properties(KalibreCore PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(KalibreCore PRIVATE -Wall -Wextra)
endif()

if(KALIBRE_BUILD_TESTS)
    enable_testing()
    add_executable(KalibreTests
        Tests/TestMain.cpp
        Tests/TestTempo.cpp
        Tests/TestEdit.cpp
        Tests/TestRoll.cpp
        Tests/TestPatterns.cpp
        Tests/TestHarmony.cpp
        Tests/TestPlayback.cpp
        Tests/TestModulation.cpp
        Tests/TestCapture.cpp
        Tests/TestFile.cpp
        Tests/TestNotation.cpp
        Tests/TestMusicXml.cpp
        Tests/TestScript.cpp
        Tests/TestComp.cpp
        Tests/TestMidiLearn.cpp
        Tests/TestLoudness.cpp
        Tests/TestSync.cpp
        Tests/TestMackie.cpp
        Tests/TestAudioTools.cpp
        Tests/TestSong.cpp
        Tests/TestSurround.cpp
        Tests/TestProjectTools.cpp
        Tests/TestHui.cpp
        Tests/TestMpe.cpp
        Tests/TestAutoCc.cpp
        Tests/TestIdeas.cpp
        Tests/TestArticulations.cpp
        Tests/TestNoteSelect.cpp
        Tests/TestStepInput.cpp
        Tests/TestNoteSplit.cpp
        Tests/TestNoteFixer.cpp
        Tests/TestWorkClock.cpp
        Tests/TestDawProject.cpp
        Tests/TestToolbox.cpp
        Tests/TestTrombolese.cpp
        Tests/TestLibrary.cpp
        Tests/TestMcp.cpp)
    target_link_libraries(KalibreTests PRIVATE KalibreCore)
    target_compile_definitions(KalibreTests PRIVATE KALIBRE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME Core COMMAND KalibreTests)
endif()

if(NOT KALIBRE_BUILD_APP)
    return()
endif()

include(cmake/App.cmake)
