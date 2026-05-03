cmake_minimum_required(VERSION 3.20)

project(LocalCall VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

include(GNUInstallDirs)

# Feature switches:
#   LOCALCALL_WITH_OPENCV=AUTO|ON|OFF
#   LOCALCALL_WITH_WEBRTC=AUTO|ON|OFF
#   LOCALCALL_WITH_MULTIMEDIA=ON|OFF
set(LOCALCALL_WITH_OPENCV "AUTO" CACHE STRING "Build OpenCV video/screen-share support: AUTO, ON, OFF")
set_property(CACHE LOCALCALL_WITH_OPENCV PROPERTY STRINGS AUTO ON OFF)
set(LOCALCALL_WITH_WEBRTC "AUTO" CACHE STRING "Build libdatachannel WebRTC support: AUTO, ON, OFF")
set_property(CACHE LOCALCALL_WITH_WEBRTC PROPERTY STRINGS AUTO ON OFF)
option(LOCALCALL_WITH_MULTIMEDIA "Build Qt Multimedia voice-note/audio support" ON)
option(LOCALCALL_INSTALL_DESKTOP_FILE "Install Linux .desktop launcher and icon" ON)

if(MSVC)
    add_compile_options(/Zc:__cplusplus /permissive-)
endif()

# Helpful Qt auto-detection for local Qt online-installer builds. Linux distros,
# Arch, Nix and Homebrew normally do not need this because Qt is found through
# CMAKE_PREFIX_PATH, pkg-config, or the CMake package registry.
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
            "$ENV{HOME}/Qt/6.11.0/gcc_64"
            "$ENV{HOME}/Qt/6.10.0/gcc_64"
            "$ENV{HOME}/Qt/6.9.0/gcc_64"
            "$ENV{HOME}/Qt/6.8.0/gcc_64"
            "$ENV{HOME}/Qt/6.7.0/gcc_64"
            "$ENV{HOME}/Qt/6.6.2/gcc_64"
            "$ENV{HOME}/Qt/6.11.0/macos"
            "$ENV{HOME}/Qt/6.10.0/macos"
            "/opt/homebrew/opt/qt"
            "/usr/local/opt/qt")
        if(EXISTS "${_candidate}/lib/cmake/Qt6/Qt6Config.cmake")
            set(CMAKE_PREFIX_PATH "${_candidate}")
            message(STATUS "Auto-detected Qt at ${_candidate}")
            break()
        endif()
    endforeach()
endif()

if(MSVC AND DEFINED CMAKE_PREFIX_PATH)
    string(TOLOWER "${CMAKE_PREFIX_PATH}" _prefix_lower)
    if("${_prefix_lower}" MATCHES "mingw")
        message(FATAL_ERROR
            "\nMISMATCH: MSVC is being used with a MinGW Qt kit.\n"
            "Install/use a Qt MSVC 2022 64-bit kit, then reconfigure with e.g.:\n"
            "  cmake -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64\n")
    endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent)

set(HAS_MULTIMEDIA FALSE)
if(LOCALCALL_WITH_MULTIMEDIA)
    find_package(Qt6 COMPONENTS Multimedia MultimediaWidgets QUIET)
    if(Qt6Multimedia_FOUND)
        set(HAS_MULTIMEDIA TRUE)
        message(STATUS "Qt6 Multimedia: ENABLED")
    else()
        message(FATAL_ERROR
            "\nQt6 Multimedia was requested but not found.\n"
            "Install the distro package (Fedora: qt6-qtmultimedia-devel, Arch: qt6-multimedia)\n"
            "or configure with -DLOCALCALL_WITH_MULTIMEDIA=OFF.\n")
    endif()
else()
    message(STATUS "Qt6 Multimedia: DISABLED by option")
endif()

set(HAS_OPENCV FALSE)
if(NOT LOCALCALL_WITH_OPENCV STREQUAL "OFF")
    find_package(OpenCV QUIET)
    if(OpenCV_FOUND)
        set(HAS_OPENCV TRUE)
        message(STATUS "OpenCV: ENABLED")
    elseif(LOCALCALL_WITH_OPENCV STREQUAL "ON")
        message(FATAL_ERROR "OpenCV was requested but not found. Install opencv-devel/opencv or set -DLOCALCALL_WITH_OPENCV=OFF.")
    else()
        message(STATUS "OpenCV: not found, video/screen-share disabled")
    endif()
endif()

set(HAS_WEBRTC FALSE)
if(NOT LOCALCALL_WITH_WEBRTC STREQUAL "OFF")
    find_package(Qt6 COMPONENTS WebSockets QUIET)
    find_package(LibDataChannel CONFIG QUIET)
    find_package(OpenH264 CONFIG QUIET)
    find_package(unofficial-libopus CONFIG QUIET)
    if(NOT unofficial-libopus_FOUND)
        find_package(Opus CONFIG QUIET)
    endif()
    find_package(libyuv CONFIG QUIET)
    if(NOT libyuv_FOUND)
        find_package(unofficial-libyuv CONFIG QUIET)
    endif()

    if(Qt6WebSockets_FOUND AND LibDataChannel_FOUND AND OpenH264_FOUND)
        set(HAS_WEBRTC TRUE)
        message(STATUS "WebRTC/libdatachannel: ENABLED")
    elseif(LOCALCALL_WITH_WEBRTC STREQUAL "ON")
        message(FATAL_ERROR
            "WebRTC was requested but dependencies were not found. Required: Qt6 WebSockets, libdatachannel, OpenH264.\n"
            "Set -DLOCALCALL_WITH_WEBRTC=OFF to build the stable LAN TCP/UDP version only.")
    else()
        message(STATUS "WebRTC/libdatachannel: not found, optional RTC module disabled")
    endif()
endif()

find_package(nlohmann_json CONFIG QUIET)
if(nlohmann_json_FOUND)
    message(STATUS "nlohmann/json: using packaged CMake target")
else()
    set(JSON_DEST "${CMAKE_SOURCE_DIR}/third_party/nlohmann/json.hpp")
    if(EXISTS "${JSON_DEST}")
        message(STATUS "nlohmann/json: using bundled third_party header")
    else()
        message(FATAL_ERROR "nlohmann/json was not found. Install nlohmann-json-devel/nlohmann-json or place json.hpp in third_party/nlohmann/.")
    endif()
endif()

set(LOCALCALL_SOURCES
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

set(LOCALCALL_HEADERS
    include/CallTypes.h
    include/ChatMessage.h
    include/ChatStore.h
    include/FirewallHelper.h
    include/FriendInfo.h
    include/FriendManager.h
    include/GroupCreateDialog.h
    include/GroupInfo.h
    include/GroupManageDialog.h
    include/Helpers.h
    include/InputDialog.h
    include/MainWindow.h
    include/MediaSettings.h
    include/NotificationWindow.h
    include/PeerDiscovery.h
    include/PeerInfo.h
    include/SigMsg.h
    include/SignalingClient.h
    include/SignalingServer.h
)

if(HAS_MULTIMEDIA)
    list(APPEND LOCALCALL_SOURCES src/VoiceNoteRecorder.cpp)
    list(APPEND LOCALCALL_HEADERS include/VoiceNoteRecorder.h)
endif()

if(HAS_MULTIMEDIA OR HAS_OPENCV)
    list(APPEND LOCALCALL_SOURCES src/MediaWorker.cpp src/CallWindow.cpp)
    list(APPEND LOCALCALL_HEADERS include/MediaWorker.h include/CallWindow.h)
endif()

if(HAS_WEBRTC)
    list(APPEND LOCALCALL_SOURCES
        src/MdnsDiscovery.cpp
        src/RtcSignaling.cpp
        src/RtcPeer.cpp
        src/MediaPipeline.cpp)
    list(APPEND LOCALCALL_HEADERS
        include/MdnsDiscovery.h
        include/RtcSignaling.h
        include/RtcPeer.h
        include/MediaPipeline.h)
endif()

if(WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/icon.ico")
    set(_RC_FILE "${CMAKE_BINARY_DIR}/app_icon.rc")
    file(WRITE "${_RC_FILE}" "IDI_ICON1 ICON \"${CMAKE_SOURCE_DIR}/icon.ico\"\n")
    list(APPEND LOCALCALL_SOURCES "${_RC_FILE}")
endif()

if(ANDROID)
    qt_add_executable(LocalCall ${LOCALCALL_SOURCES} ${LOCALCALL_HEADERS})
elseif(WIN32)
    add_executable(LocalCall WIN32 ${LOCALCALL_SOURCES} ${LOCALCALL_HEADERS})
else()
    add_executable(LocalCall ${LOCALCALL_SOURCES} ${LOCALCALL_HEADERS})
endif()

set_target_properties(LocalCall PROPERTIES
    OUTPUT_NAME "LocalCall"
    VERSION ${PROJECT_VERSION}
)

target_include_directories(LocalCall PRIVATE include third_party)

target_compile_definitions(LocalCall PRIVATE
    LOCALCALL_VERSION="${PROJECT_VERSION}"
    LOCALCALL_PROTOCOL="localcall.v1"
)

target_link_libraries(LocalCall PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Concurrent
)

if(nlohmann_json_FOUND)
    target_link_libraries(LocalCall PRIVATE nlohmann_json::nlohmann_json)
endif()

if(HAS_MULTIMEDIA)
    target_link_libraries(LocalCall PRIVATE Qt6::Multimedia)
    if(Qt6MultimediaWidgets_FOUND)
        target_link_libraries(LocalCall PRIVATE Qt6::MultimediaWidgets)
    endif()
    target_compile_definitions(LocalCall PRIVATE HAS_MULTIMEDIA=1)
endif()

if(HAS_OPENCV)
    target_include_directories(LocalCall PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(LocalCall PRIVATE ${OpenCV_LIBS})
    target_compile_definitions(LocalCall PRIVATE HAS_OPENCV=1)
endif()

if(HAS_WEBRTC)
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

if(WIN32)
    target_link_libraries(LocalCall PRIVATE ws2_32 dwmapi)
    set_target_properties(LocalCall PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if(APPLE)
    set_target_properties(LocalCall PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Local Call"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.localcall.LocalCall"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/icon.ico")
    add_custom_command(TARGET LocalCall POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/icon.ico"
                "$<TARGET_FILE_DIR:LocalCall>/icon.ico"
        COMMENT "Copying icon.ico next to executable")
endif()

install(TARGETS LocalCall
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE  DESTINATION .
)

if(UNIX AND NOT APPLE AND NOT ANDROID AND LOCALCALL_INSTALL_DESKTOP_FILE)
    if(EXISTS "${CMAKE_SOURCE_DIR}/packaging/linux/localcall.desktop")
        install(FILES packaging/linux/localcall.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    endif()
    if(EXISTS "${CMAKE_SOURCE_DIR}/packaging/linux/localcall.png")
        install(FILES packaging/linux/localcall.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/32x32/apps
            RENAME localcall.png)
    endif()
endif()

message(STATUS "")
message(STATUS "=== Local Call build configuration ===")
message(STATUS "  Version       : ${PROJECT_VERSION}")
message(STATUS "  Platform      : ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Qt Multimedia : ${HAS_MULTIMEDIA}")
message(STATUS "  OpenCV        : ${HAS_OPENCV}")
message(STATUS "  WebRTC P2P    : ${HAS_WEBRTC}")
message(STATUS "======================================")
