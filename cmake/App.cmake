# The app: JUCE, the CLAP SDK (hosting CLAP, decision 0021), Signalsmith
# Stretch (spectral warping, decision 0022) and, on a Mac, the ARA SDK.
# All fetched at fixed versions. To use copies already on disk, pass
# -DFETCHCONTENT_SOURCE_DIR_JUCE=... (and _CLAP, _SIGNALSMITH_STRETCH,
# _SIGNALSMITH_LINEAR, _ARA_SDK).

include(FetchContent)

FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.5
    GIT_SHALLOW    TRUE)

FetchContent_Declare(clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG        1.2.9
    GIT_SHALLOW    TRUE)

FetchContent_Declare(signalsmith_stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG        1.4.0
    GIT_SHALLOW    TRUE)

FetchContent_MakeAvailable(juce)

# Only the headers of these are wanted, not their own targets and tests.
FetchContent_GetProperties(clap)
if(NOT clap_POPULATED)
    FetchContent_Populate(clap)
endif()
FetchContent_MakeAvailable(signalsmith_stretch)

# Ableton Link (GPL v2 or later, decision 0034), headers and its asio.
FetchContent_Declare(link
    GIT_REPOSITORY https://github.com/Ableton/link.git
    GIT_TAG        Link-4.1.1
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES modules/asio-standalone)
FetchContent_GetProperties(link)
if(NOT link_POPULATED)
    FetchContent_Populate(link)
endif()
include(${link_SOURCE_DIR}/AbletonLinkConfig.cmake)
# Link's config sets C++17 for everything after it; Kalibre is C++20.
set(CMAKE_CXX_STANDARD 20)

# Shine, a small fixed-point MP3 encoder (LGPL-2.0): MP3 export (decision 0041).
FetchContent_Declare(shine
    GIT_REPOSITORY https://github.com/toots/shine.git
    GIT_TAG        ab5e3526b64af1a2eaa43aa6f441a7312e013519)
FetchContent_GetProperties(shine)
if(NOT shine_POPULATED)
    FetchContent_Populate(shine)
endif()
file(GLOB SHINE_SOURCES ${shine_SOURCE_DIR}/src/lib/*.c)
add_library(KalibreShine STATIC ${SHINE_SOURCES})
target_include_directories(KalibreShine PUBLIC ${shine_SOURCE_DIR}/src/lib)
set_target_properties(KalibreShine PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)

# ysfx (Apache-2.0): the JSFX engine (REAPER's EEL2) with its graphics, for the
# family's JSFX built in as devices (decision 0066). Its engine only: not its
# own plug-in, tests or tools; its graphics view (plugin/components) is
# compiled into KalibreApp below.
FetchContent_Declare(ysfx
    GIT_REPOSITORY https://github.com/JoepVanlier/ysfx.git
    GIT_TAG        5c3452fee62583aa3d1b7e877d0c758c4024af89
    GIT_SUBMODULES thirdparty/dr_libs)
FetchContent_GetProperties(ysfx)
if(NOT ysfx_POPULATED)
    FetchContent_Populate(ysfx)
endif()
# One correction (decision 0093): in @serialize, file_avail says how many values
# are left to read, as REAPER's does; ysfx said only 1 or 0, so a JSFX checking
# for at least so many (Midular) never read its saved phrase back.
set(YSFX_FILE_API ${ysfx_SOURCE_DIR}/sources/ysfx_api_file.cpp)
file(READ ${YSFX_FILE_API} ysfx_file_api)
string(REPLACE "return (m_buffer->size() > m_pos) ? 1 : 0;"
               "return (m_buffer->size() > m_pos) ? (int32_t) ((m_buffer->size() - m_pos) / 4) : 0;"
               ysfx_file_api_fixed "${ysfx_file_api}")
if(NOT ysfx_file_api_fixed STREQUAL ysfx_file_api)
    file(WRITE ${YSFX_FILE_API} "${ysfx_file_api_fixed}")
endif()
# A second (decision 0094): export_buffer_to_project, a stub in ysfx, hands the
# buffer to Kalibre (Transient Shuffler's Print), through a hook Kalibre sets.
set(YSFX_HOST_API ${ysfx_SOURCE_DIR}/sources/ysfx_api_host_interaction_dummy.hpp)
file(READ ${YSFX_HOST_API} ysfx_host_api)
string(REPLACE "static EEL_F NSEEL_CGEN_CALL export_buffer_to_project(void *, INT_PTR, EEL_F **)
{
    return 0;
}"
"extern \"C\" int (*kalibre_export_buffer_to_project) (void* fx, const double* interleaved, long long frames, int channels, double rate, int track);
int (*kalibre_export_buffer_to_project) (void*, const double*, long long, int, double, int) = nullptr;
static EEL_F NSEEL_CGEN_CALL export_buffer_to_project(void *opaque, INT_PTR np, EEL_F **parms)
{
    if (! kalibre_export_buffer_to_project || np < 4) return 0;
    ysfx_t *fx = (ysfx_t *)opaque;
    const int64_t addr = ysfx_eel_round<int64_t>(*parms[0]);
    const int64_t frames = ysfx_eel_round<int64_t>(*parms[1]);
    const int32_t channels = ysfx_eel_round<int32_t>(*parms[2]);
    if (frames <= 0 || channels <= 0 || frames * channels > 64 * 1024 * 1024) return 0;
    std::vector<double> data ((size_t) (frames * channels));
    ysfx_eel_ram_reader reader (fx->vm.get(), addr);
    for (auto &v : data) v = reader.read_next();
    return kalibre_export_buffer_to_project (fx, data.data(), frames, channels, *parms[3], np > 4 ? ysfx_eel_round<int32_t>(*parms[4]) : -1);
}"
       ysfx_host_api_fixed "${ysfx_host_api}")
if(NOT ysfx_host_api_fixed STREQUAL ysfx_host_api)
    file(WRITE ${YSFX_HOST_API} "${ysfx_host_api_fixed}")
endif()
set(YSFX_PLUGIN OFF CACHE BOOL "" FORCE)
set(YSFX_GFX ON CACHE BOOL "" FORCE)
set(YSFX_TESTS OFF CACHE BOOL "" FORCE)
set(YSFX_TOOLS OFF CACHE BOOL "" FORCE)
set(YSFX_SKIP_CHECKSUM ON CACHE BOOL "" FORCE)
add_subdirectory(${ysfx_SOURCE_DIR} ${ysfx_BINARY_DIR} EXCLUDE_FROM_ALL)
# ysfx sets the standard for what comes after it; Kalibre is C++20.
set(CMAKE_CXX_STANDARD 20)

# Dear ImGui (MIT): the window the family's ReaImGui tools draw (Starting
# Blocks, Starting Blocks Notation, Midi Catalogue), run by an embedded Lua on
# a stand-in for ReaImGui and drawn with JUCE (decision 0068).
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.9b
    GIT_SHALLOW    TRUE)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()
add_library(KalibreImGui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
target_include_directories(KalibreImGui PUBLIC ${imgui_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/Source/Engine/LuaTool)
target_compile_definitions(KalibreImGui PUBLIC IMGUI_USER_CONFIG="ToolImConfig.h")
set_target_properties(KalibreImGui PROPERTIES POSITION_INDEPENDENT_CODE ON)

option(KALIBRE_ARA "Host ARA plug-ins (Melodyne and others)" ${APPLE})
if(KALIBRE_ARA)
    FetchContent_Declare(ara_sdk
        GIT_REPOSITORY https://github.com/Celemony/ARA_SDK.git
        GIT_TAG        releases/2.3.0
        GIT_SHALLOW    TRUE
        GIT_SUBMODULES ARA_API ARA_Library)
    FetchContent_GetProperties(ara_sdk)
    if(NOT ara_sdk_POPULATED)
        FetchContent_Populate(ara_sdk)
    endif()
    juce_set_ara_sdk_path(${ara_sdk_SOURCE_DIR})
endif()

#==============================================================================
# The engine and the window, shared by the app, its tests and the renderer.
#==============================================================================
add_library(KalibreApp STATIC
    Source/Engine/ClapFormat.cpp
    Source/Engine/PluginManager.cpp
    Source/Engine/TrackProcessors.cpp
    Source/Engine/MorphProcessor.cpp
    Source/Engine/ContainerProcessor.cpp
    Source/Engine/MultibandProcessor.cpp
    Source/Engine/SplitProcessor.cpp
    Source/Engine/StereoSplitProcessor.cpp
    Source/Engine/DrumMachine.cpp
    Source/Engine/DrumMachineEditor.cpp
    Source/Engine/DrumPadsDevice.cpp
    Source/Engine/DrumPadsEditor.cpp
    Source/Engine/JsfxDevice.cpp
    Source/Engine/HostedProcessor.cpp
    Source/Engine/NoteSplitter.cpp
    Source/Engine/LuaTool/LuaTool.cpp
    Source/Engine/LuaTool/ToolSession.cpp
    Source/Engine/LuaTool/ImGuiLua.cpp
    Source/Engine/LuaTool/GfxTool.cpp
    ${ysfx_SOURCE_DIR}/plugin/components/graphics_view.cpp
    ${ysfx_SOURCE_DIR}/plugin/utility/async_updater.cpp
    ${ysfx_SOURCE_DIR}/plugin/utility/rt_semaphore.cpp
    Source/Engine/BuiltinInstruments.cpp
    Source/Engine/HardwareInsert.cpp
    Source/Engine/Mp3Writer.cpp
    Source/Engine/Warp.cpp
    Source/Engine/Engine.cpp
    Source/Engine/ParallelGraph.cpp
    Source/Engine/AudioFiles.cpp
    Source/Engine/AraHost.cpp
    Source/Engine/SyncDevices.cpp
    Source/App/Theme.cpp
    Source/App/Controller.cpp
    Source/App/ReaperScripts.cpp
    Source/App/AudioEditing.cpp
    Source/App/Presets.cpp
    Source/App/ProjectFiles.cpp
    Source/App/NoteFixing.cpp
    Source/App/SampleEditor.cpp
    Source/App/OfflineFxView.cpp
    Source/App/MetersView.cpp
    Source/App/PluginManagerView.cpp
    Source/App/ArticulationManagerView.cpp
    Source/App/NoteSplitterEditor.cpp
    Source/App/CrashReports.cpp
    Source/App/VideoView.cpp
    Source/App/Keys.cpp
    Source/App/KeysPanel.cpp
    Source/App/TopBar.cpp
    Source/App/Arrange.cpp
    Source/App/PianoRoll.cpp
    Source/App/ToolPicker.cpp
    Source/App/ArrangeTools.cpp
    Source/App/PianoRollTools.cpp
    Source/App/CcLanes.cpp
    Source/App/PatternEditors.cpp
    Source/App/NotationView.cpp
    Source/App/DeviceRack.cpp
    Source/App/Mixer.cpp
    Source/App/LowerZone.cpp
    Source/App/Browser.cpp
    Source/App/LibraryScanner.cpp
    Source/App/PreviewPlayer.cpp
    Source/App/LibraryView.cpp
    Source/App/LibraryFolders.cpp
    Source/App/MainComponent.cpp
    Source/App/Menus.cpp
    Source/App/AgentTools.cpp
    Source/App/RandomSongs.cpp
    Source/App/McpServer.cpp
    Source/App/Surfaces.cpp
    Source/App/StudioPanel.cpp
    Source/App/SettingsWindows.cpp
    Source/App/BattlePass.cpp)
# ScaleView (KallumS/ScaleView, GPL-3.0, decision 0093), the same way: its source
# unchanged, its folder its include root (its ScaleModel.h is Kalibre's own, the
# same file), its plug-in entry point renamed so it doesn't meet IVI's.
set(SCALEVIEW_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/ScaleView/Source/PluginProcessor.cpp
                      ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/ScaleView/Source/PluginEditor.cpp)
target_sources(KalibreApp PRIVATE ${SCALEVIEW_SOURCES} Source/Engine/ScaleViewDevice.cpp)
set_source_files_properties(${SCALEVIEW_SOURCES} Source/Engine/ScaleViewDevice.cpp Tests/App/TestFamilyDevices.cpp PROPERTIES
    INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/ScaleView/Source
    COMPILE_DEFINITIONS "JucePlugin_Name=\"ScaleView\";createPluginFilter=scaleViewPluginFilter")

# Instant Virtual Instrument (KallumS/Instant-Virtual-Instrument, GPL-3.0), its
# source unchanged, compiled into the app library as a built-in instrument
# (decision 0067). Its own folder is its include root, and its plug-in name
# the one JUCE would have given it, on its files only.
file(GLOB_RECURSE IVI_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/InstantVirtualInstrument/Source/*.cpp)
target_sources(KalibreApp PRIVATE ${IVI_SOURCES} Source/Engine/InstantVirtualInstrument.cpp)
set_source_files_properties(${IVI_SOURCES} Source/Engine/InstantVirtualInstrument.cpp PROPERTIES
    INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/InstantVirtualInstrument/Source
    COMPILE_DEFINITIONS "JucePlugin_Name=\"Instant Virtual Instrument\"")

if(APPLE)
    target_sources(KalibreApp PRIVATE Source/Engine/ClapGuiMac.mm Source/Engine/NetworkMidiMac.mm Source/App/PluginClipMac.mm Source/App/FilePanelsMac.mm)
    target_link_libraries(KalibreApp PRIVATE "-framework CoreMIDI")
    target_link_libraries(KalibreApp PUBLIC juce::juce_video)   # the Video tab (decision 0046)
elseif(WIN32)
    # Windows (decision 0129): the scanner's helper, CLAP windows and crash reports of its own.
    target_sources(KalibreApp PRIVATE Source/Engine/NetworkMidiOther.cpp Source/App/PluginClipOther.cpp Source/App/FilePanelsOther.cpp
                                      Source/Engine/HelperProcessWin.cpp Source/Engine/ClapGuiWin.cpp Source/App/CrashReportsWin.cpp)
else()
    target_sources(KalibreApp PRIVATE Source/Engine/NetworkMidiOther.cpp Source/App/PluginClipOther.cpp Source/App/FilePanelsOther.cpp)
endif()

target_include_directories(KalibreApp PUBLIC
    Source/App Source/Engine
    ${ysfx_SOURCE_DIR}/plugin
    ${clap_SOURCE_DIR}/include)
target_link_libraries(KalibreApp
    PUBLIC
        ysfx::ysfx
        KalibreImGui
        KalibreCore
        KalibreShine
        signalsmith-stretch
        Ableton::Link
        juce::juce_osc
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_extra
    PRIVATE
        juce::juce_recommended_config_flags)

target_compile_definitions(KalibreApp PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_LV2=0
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_PLUGINHOST_ARA=$<IF:$<BOOL:${KALIBRE_ARA}>,1,0>
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    KALIBRE_VERSION="${PROJECT_VERSION}")

#==============================================================================
juce_add_gui_app(Kalibre
    PRODUCT_NAME          "Kalibre"
    COMPANY_NAME          "Kallum Shah"
    BUNDLE_ID             "com.kallumshah.kalibre"
    VERSION               ${PROJECT_VERSION}
    DOCUMENT_EXTENSIONS   kalibre
    ICON_BIG              "${CMAKE_SOURCE_DIR}/Resources/Icon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Kalibre records from your audio interface."
    HARDENED_RUNTIME_ENABLED FALSE
    PLIST_TO_MERGE "<plist><dict><key>LSMinimumSystemVersion</key><string>12.0</string><key>NSHighResolutionCapable</key><true/></dict></plist>")

target_sources(Kalibre PRIVATE Source/App/Main.cpp)
target_compile_definitions(Kalibre PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Kalibre,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Kalibre,JUCE_VERSION>")
target_link_libraries(Kalibre PRIVATE
    KalibreApp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

#==============================================================================
# The window's own tests, and a tool that draws the real window into PNGs
# (so a change can be seen from a Linux container, decision 0023).
if(KALIBRE_BUILD_TESTS)
    juce_add_console_app(KalibreAppTests PRODUCT_NAME "Kalibre App Tests")
    target_sources(KalibreAppTests PRIVATE Tests/App/TestAppMain.cpp Tests/App/TestEngine.cpp Tests/App/TestWindow.cpp Tests/App/TestSurfaces.cpp Tests/App/TestBattlePass.cpp
    Tests/App/TestFiles.cpp Tests/App/TestPlugins.cpp Tests/App/TestTrackKinds.cpp Tests/App/TestContainer.cpp Tests/App/TestTools.cpp Tests/App/TestFamilyDevices.cpp
    Tests/App/TestNeutral.cpp Tests/App/TestLibrary.cpp Tests/App/TestAgents.cpp Tests/App/TestRandom.cpp Tests/App/TestDrums.cpp Tests/App/TestDrumPads.cpp)
    target_include_directories(KalibreAppTests PRIVATE Tests)
    target_compile_definitions(KalibreAppTests PRIVATE JUCE_STANDALONE_APPLICATION=1 KALIBRE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(KalibreAppTests PRIVATE KalibreApp juce::juce_recommended_config_flags)
    add_test(NAME App COMMAND KalibreAppTests)

    juce_add_console_app(KalibreRender PRODUCT_NAME "Kalibre Render")
    target_sources(KalibreRender PRIVATE tools/RenderWindow.cpp)
    target_compile_definitions(KalibreRender PRIVATE JUCE_STANDALONE_APPLICATION=1 KALIBRE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(KalibreRender PRIVATE KalibreApp juce::juce_recommended_config_flags)
endif()
