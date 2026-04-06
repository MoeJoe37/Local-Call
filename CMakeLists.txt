cmake_minimum_required(VERSION 3.20)
project(LocalCall VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ── MSVC: tell it to report __cplusplus correctly ─────────────────────────────
# Qt requires this flag — without it MSVC reports __cplusplus = 199711L regardless
# of /std:c++17 or /std:c++20, which triggers Qt's compile-time error.
if(MSVC)
    add_compile_options(/Zc:__cplusplus /permissive-)
endif()

# ── Prevent vcpkg from intercepting Qt packages ────────────────────────────────
set(VCPKG_MANIFEST_MODE OFF)

# ── Qt prefix auto-detection ──────────────────────────────────────────────────
if(NOT DEFINED CMAKE_PREFIX_PATH)
    foreach(_candidate
            "C:/Qt/6.11.0/msvc2022_64"
            "C:/Qt/6.11.0/msvc2019_64"
            "C:/Qt/6.10.0/msvc2022_64"
            "C:/Qt/6.9.0/msvc2022_64"
            "C:/Qt/6.8.0/msvc2022_64"
            "C:/Qt/6.7.0/msvc2022_64"
            "C:/Qt/6.6.2/msvc2022_64"
            "C:/Qt/6.6.2/msvc2019_64"
            "C:/Qt/6.6.1/msvc2019_64"
            "C:/Qt/6.5.3/msvc2019_64"
            "$ENV{HOME}/Qt/6.11.0/gcc_64"
            "$ENV{HOME}/Qt/6.11.0/macos"
            "$ENV{HOME}/Qt/6.6.2/gcc_64"
            "$ENV{HOME}/Qt/6.6.2/macos"
            "/opt/homebrew/opt/qt"
            "/usr/local/opt/qt")
        if(EXISTS "${_candidate}/lib/cmake/Qt6/Qt6Config.cmake")
            set(CMAKE_PREFIX_PATH "${_candidate}")
            message(STATUS "Auto-detected Qt at ${_candidate}")
            break()
        endif()
    endforeach()
endif()

# ── Guard: catch mingw/msvc mismatch early ────────────────────────────────────
# If the user is building with MSVC but the Qt prefix points at a MinGW kit,
# compilation will fail with cryptic errors. Catch it here with a clear message.
if(MSVC AND DEFINED CMAKE_PREFIX_PATH)
    string(TOLOWER "${CMAKE_PREFIX_PATH}" _prefix_lower)
    if("${_prefix_lower}" MATCHES "mingw")
        message(FATAL_ERROR
            "\n"
            "  MISMATCH: You are building with MSVC but the Qt prefix path\n"
            "  points at a MinGW kit:\n"
            "    ${CMAKE_PREFIX_PATH}\n"
            "\n"
            "  Fix — install the MSVC 2022 64-bit kit:\n"
            "    1. Open Qt Maintenance Tool\n"
            "    2. Add/Remove Components\n"
            "    3. Qt → Qt 6.11.0 → tick 'MSVC 2022 64-bit'\n"
            "    4. Also tick 'Qt Multimedia' under Additional Libraries\n"
            "    5. Install, then re-run:\n"
            "       cmake -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64\n"
        )
    endif()
endif()


# ── Qt core (required) ─────────────────────────────────────────────────────────
# Note: WebSockets is intentionally NOT listed here — it is only needed by the
# WebRTC signaling layer (RtcSignaling.cpp) which is already guarded by
# HAS_WEBRTC. It is found and linked conditionally below.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent)

# ── Qt Multimedia (required — install via Qt Maintenance Tool) ─────────────────
# Open Qt Maintenance Tool → Add/Remove Components → Qt 6.6.2 → tick
# "Qt Multimedia".  Do NOT use vcpkg for this — vcpkg's qt5-multimedia is for
# Qt 5 and is incompatible with Qt 6.
find_package(Qt6 COMPONENTS Multimedia MultimediaWidgets)
if(Qt6Multimedia_FOUND)
    message(STATUS "Qt6Multimedia found — voice notes and audio calls enabled")
    set(HAS_MULTIMEDIA TRUE)
else()
    message(FATAL_ERROR
        "\n"
        "  Qt6 Multimedia NOT found.\n"
        "\n"
        "  Fix: open Qt Maintenance Tool, navigate to:\n"
        "    Qt → Qt 6.11.0 → Additional Libraries → Qt Multimedia\n"
        "  Tick the checkbox and install.\n"
        "\n"
        "  Do NOT use 'vcpkg install qt5-multimedia' — that is Qt 5 only.\n"
    )
endif()

# ── P2P WebRTC module dependencies (optional — enabled when vcpkg finds them) ──
find_package(LibDataChannel CONFIG QUIET)
find_package(unofficial-libopus CONFIG QUIET)
if(NOT unofficial-libopus_FOUND)
    find_package(Opus CONFIG QUIET)
endif()
find_package(OpenH264 CONFIG QUIET)
find_package(libyuv CONFIG QUIET)
if(NOT libyuv_FOUND)
    find_package(unofficial-libyuv CONFIG QUIET)
endif()

set(HAS_WEBRTC FALSE)
if(LibDataChannel_FOUND AND OpenH264_FOUND)
    set(HAS_WEBRTC TRUE)
    message(STATUS "WebRTC module: ENABLED (libdatachannel + openh264 found)")
else()
    message(WARNING "WebRTC module: DISABLED\n"
        "  Run: vcpkg install libdatachannel openh264 libopus libyuv\n"
        "  Then reconfigure with -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake")
endif()

# ── OpenCV (optional) ─────────────────────────────────────────────────────────
find_package(OpenCV QUIET)
if(OpenCV_FOUND)
    message(STATUS "OpenCV found — video features enabled")
    set(HAS_OPENCV TRUE)
else()
    message(WARNING "OpenCV NOT found — video calls/screen share disabled.")
    set(HAS_OPENCV FALSE)
endif()

# ── nlohmann/json ─────────────────────────────────────────────────────────────
set(JSON_DEST "${CMAKE_SOURCE_DIR}/third_party/nlohmann/json.hpp")
set(_json_ok FALSE)
if(EXISTS "${JSON_DEST}")
    file(READ "${JSON_DEST}" _json_content)
    string(FIND "${_json_content}" "NLOHMANN_JSON_VERSION" _json_pos)
    if(NOT _json_pos EQUAL -1)
        set(_json_ok TRUE)
        message(STATUS "nlohmann/json found in third_party/")
    endif()
endif()

if(NOT _json_ok)
    message(STATUS "Downloading nlohmann/json single-header...")
    file(DOWNLOAD
        "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp"
        "${JSON_DEST}"
        TIMEOUT 60
        STATUS _dl_status
        SHOW_PROGRESS
    )
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
        message(WARNING "Direct download failed (${_dl_status}). Trying FetchContent git clone...")
        include(FetchContent)
        FetchContent_Declare(nlohmann_json
            GIT_REPOSITORY https://github.com/nlohmann/json.git
            GIT_TAG        v3.11.3
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(nlohmann_json)
        set(_json_via_target TRUE)
    else()
        message(STATUS "nlohmann/json downloaded successfully.")
    endif()
endif()

# ── Sources ───────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/Helpers.cpp
    src/PeerDiscovery.cpp
    src/SignalingServer.cpp
    src/SignalingClient.cpp
    src/FriendManager.cpp
    src/ChatStore.cpp
    src/FirewallHelper.cpp
    src/NotificationWindow.cpp
    src/InputDialog.cpp
    src/GroupCreateDialog.cpp
    src/GroupManageDialog.cpp
    src/MainWindow.cpp
    src/MainWindow_logic.cpp
)

if(HAS_MULTIMEDIA)
    list(APPEND SOURCES src/VoiceNoteRecorder.cpp)
endif()
if(HAS_MULTIMEDIA OR HAS_OPENCV)
    list(APPEND SOURCES src/MediaWorker.cpp src/CallWindow.cpp)
endif()
if(HAS_WEBRTC)
    list(APPEND SOURCES
        src/MdnsDiscovery.cpp
        src/RtcSignaling.cpp
        src/RtcPeer.cpp
        src/MediaPipeline.cpp)
    list(APPEND HEADERS
        include/MdnsDiscovery.h
        include/RtcSignaling.h
        include/RtcPeer.h
        include/MediaPipeline.h)
endif()

# ── Windows: embed icon via .rc resource file ─────────────────────────────────
if(WIN32)
    if(EXISTS "${CMAKE_SOURCE_DIR}/icon.ico")
        # Generate an .rc file pointing at the icon so the .exe carries it
        set(_RC_FILE "${CMAKE_BINARY_DIR}/app_icon.rc")
        file(WRITE "${_RC_FILE}" "IDI_ICON1 ICON \"${CMAKE_SOURCE_DIR}/icon.ico\"\n")
        list(APPEND SOURCES "${_RC_FILE}")
    endif()
endif()

set(HEADERS
    include/MediaSettings.h   include/SigMsg.h
    include/PeerInfo.h        include/FriendInfo.h
    include/GroupInfo.h       include/ChatMessage.h
    include/Helpers.h         include/PeerDiscovery.h
    include/SignalingServer.h include/SignalingClient.h
    include/FriendManager.h   include/ChatStore.h
    include/FirewallHelper.h  include/NotificationWindow.h
    include/InputDialog.h
    include/GroupCreateDialog.h include/GroupManageDialog.h
    include/MainWindow.h      include/CallTypes.h
)

# Only expose these headers to AUTOMOC when the matching feature is present —
# without this guard, moc generates stubs that reference excluded .cpp bodies.
if(HAS_MULTIMEDIA)
    list(APPEND HEADERS include/VoiceNoteRecorder.h)
endif()
if(HAS_MULTIMEDIA OR HAS_OPENCV)
    list(APPEND HEADERS include/MediaWorker.h include/CallWindow.h)
endif()

# ── Executable ────────────────────────────────────────────────────────────────
# Use WIN32 flag only on Windows (hides the console window).
# On Android Qt requires qt_add_executable; on all others add_executable is fine.
if(ANDROID)
    qt_add_executable(LocalCall ${SOURCES} ${HEADERS})
elseif(WIN32)
    add_executable(LocalCall WIN32 ${SOURCES} ${HEADERS})
else()
    add_executable(LocalCall ${SOURCES} ${HEADERS})
endif()

# ── macOS bundle properties + icon ────────────────────────────────────────────
if(APPLE)
    set_target_properties(LocalCall PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME        "Local Call"
        MACOSX_BUNDLE_GUI_IDENTIFIER     "com.localcall.LocalCall"
        MACOSX_BUNDLE_BUNDLE_VERSION     "1.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    )
    # Convert .ico → .icns if ImageMagick is available, otherwise skip
    find_program(MAGICK convert)
    if(MAGICK AND EXISTS "${CMAKE_SOURCE_DIR}/icon.ico")
        set(_ICNS "${CMAKE_BINARY_DIR}/LocalCall.icns")
        add_custom_command(OUTPUT "${_ICNS}"
            COMMAND ${MAGICK} "${CMAKE_SOURCE_DIR}/icon.ico" "${_ICNS}"
            DEPENDS "${CMAKE_SOURCE_DIR}/icon.ico"
            COMMENT "Converting icon.ico to .icns")
        add_custom_target(app_icns ALL DEPENDS "${_ICNS}")
        add_dependencies(LocalCall app_icns)
        set_target_properties(LocalCall PROPERTIES
            MACOSX_BUNDLE_ICON_FILE "LocalCall.icns")
        set_source_files_properties("${_ICNS}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources")
        target_sources(LocalCall PRIVATE "${_ICNS}")
    endif()
endif()

# ── Android: copy icon into the expected drawable location ────────────────────
if(ANDROID AND EXISTS "${CMAKE_SOURCE_DIR}/icon.ico")
    find_program(MAGICK convert)
    if(MAGICK)
        set(_PNG "${CMAKE_BINARY_DIR}/android/res/drawable/icon.png")
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/android/res/drawable")
        add_custom_command(OUTPUT "${_PNG}"
            COMMAND ${MAGICK} "${CMAKE_SOURCE_DIR}/icon.ico" "${_PNG}"
            DEPENDS "${CMAKE_SOURCE_DIR}/icon.ico"
            COMMENT "Converting icon.ico to Android drawable PNG")
        add_custom_target(android_icon ALL DEPENDS "${_PNG}")
        add_dependencies(LocalCall android_icon)
    endif()
endif()

# ── Include paths ─────────────────────────────────────────────────────────────
target_include_directories(LocalCall PRIVATE
    include
    third_party
)
if(HAS_OPENCV)
    target_include_directories(LocalCall PRIVATE ${OpenCV_INCLUDE_DIRS})
endif()
if(DEFINED _json_via_target)
    target_link_libraries(LocalCall PRIVATE nlohmann_json::nlohmann_json)
endif()

# ── Link ──────────────────────────────────────────────────────────────────────
target_link_libraries(LocalCall PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Concurrent)

if(HAS_WEBRTC)
    # WebSockets is required for the RTC signaling layer (RtcSignaling.cpp)
    find_package(Qt6 COMPONENTS WebSockets QUIET)
    if(NOT Qt6WebSockets_FOUND)
        message(FATAL_ERROR
            "\n"
            "  Qt6 WebSockets NOT found, but it is required for the WebRTC module.\n"
            "\n"
            "  Fix: open Qt Maintenance Tool, navigate to:\n"
            "    Qt → Qt 6.x.x → Additional Libraries → Qt WebSockets\n"
            "  Tick the checkbox and install.\n"
            "\n"
            "  Alternatively, disable the WebRTC module by not installing\n"
            "  libdatachannel/openh264 (they are optional).\n"
        )
    endif()

    target_link_libraries(LocalCall PRIVATE LibDataChannel::LibDataChannel Qt6::WebSockets)
    target_compile_definitions(LocalCall PRIVATE HAS_WEBRTC=1)

    if(OpenH264_FOUND)
        target_link_libraries(LocalCall PRIVATE OpenH264::OpenH264)
    endif()
    if(unofficial-libopus_FOUND)
        target_link_libraries(LocalCall PRIVATE unofficial::libopus::libopus)
    elseif(Opus_FOUND)
        target_link_libraries(LocalCall PRIVATE Opus::opus)
    endif()
    if(libyuv_FOUND)
        target_link_libraries(LocalCall PRIVATE libyuv::libyuv)
    elseif(unofficial-libyuv_FOUND)
        target_link_libraries(LocalCall PRIVATE unofficial::libyuv::libyuv)
    endif()
endif()

if(HAS_MULTIMEDIA)
    target_link_libraries(LocalCall PRIVATE Qt6::Multimedia)
    if(Qt6MultimediaWidgets_FOUND)
        target_link_libraries(LocalCall PRIVATE Qt6::MultimediaWidgets)
    endif()
    target_compile_definitions(LocalCall PRIVATE HAS_MULTIMEDIA=1)
endif()

if(HAS_OPENCV)
    target_link_libraries(LocalCall PRIVATE ${OpenCV_LIBS})
    target_compile_definitions(LocalCall PRIVATE HAS_OPENCV=1)
endif()

# Windows-only system libraries
if(WIN32)
    target_link_libraries(LocalCall PRIVATE ws2_32 dwmapi)
    set_target_properties(LocalCall PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# ── Copy icon.ico next to the executable on Windows ───────────────────────────
# The programmatic icon loader in main.cpp looks for it beside the binary.
if(WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/icon.ico")
    add_custom_command(TARGET LocalCall POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/icon.ico"
            "$<TARGET_FILE_DIR:LocalCall>/icon.ico"
        COMMENT "Copying icon.ico to output directory")
endif()

# Copy icon.ico to build root for Linux (beside the binary)
if(UNIX AND NOT APPLE AND NOT ANDROID AND EXISTS "${CMAKE_SOURCE_DIR}/icon.ico")
    add_custom_command(TARGET LocalCall POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/icon.ico"
            "$<TARGET_FILE_DIR:LocalCall>/icon.ico"
        COMMENT "Copying icon.ico to output directory")
endif()

message(STATUS "")
message(STATUS "=== Local Call build configuration ===")
message(STATUS "  Platform      : ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Qt Multimedia : ${HAS_MULTIMEDIA}  (voice notes, audio calls)")
message(STATUS "  OpenCV        : ${HAS_OPENCV}  (video calls, screen share)")
message(STATUS "  WebRTC P2P    : ${HAS_WEBRTC}  (mDNS discovery + libdatachannel)")
message(STATUS "==========================================")

