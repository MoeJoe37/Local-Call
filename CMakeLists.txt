cmake_minimum_required(VERSION 3.20)

project(LocalCall VERSION 2.0.22 LANGUAGES CXX)

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
set(LOCALCALL_WITH_WEBRTC "ON" CACHE STRING "Build libdatachannel WebRTC support: AUTO, ON, OFF")
set_property(CACHE LOCALCALL_WITH_WEBRTC PROPERTY STRINGS AUTO ON OFF)
option(LOCALCALL_WITH_MULTIMEDIA "Build Qt Multimedia voice-note/audio support" ON)
option(LOCALCALL_INSTALL_DESKTOP_FILE "Install Linux .desktop launcher and icon" ON)
option(LOCALCALL_INSTALL_QT_RUNTIME "Deploy Qt runtime dependencies on cmake --install using Qt's CMake deployment API" ON)
# Windows note: older build folders may cache LOCALCALL_POST_BUILD_DEPLOY_QT=OFF
# from previous releases. Force it ON by default on Windows because launching a
# Qt executable without a freshly deployed matching Qt runtime causes entry-point
# errors such as QSlider destructor not found. Use LOCALCALL_DISABLE_POST_BUILD_DEPLOY_QT=ON
# only for CI/package systems that deploy Qt themselves.
option(LOCALCALL_DISABLE_POST_BUILD_DEPLOY_QT "Advanced: disable automatic Windows Qt deployment after build." OFF)
if(WIN32 AND NOT LOCALCALL_DISABLE_POST_BUILD_DEPLOY_QT)
    set(LOCALCALL_POST_BUILD_DEPLOY_QT ON CACHE BOOL "On Windows, run the matching windeployqt after build." FORCE)
else()
    option(LOCALCALL_POST_BUILD_DEPLOY_QT "On Windows, run the matching windeployqt after build." OFF)
endif()

if(MSVC)
    add_compile_options(/Zc:__cplusplus /permissive-)
endif()

# Make installed Linux/macOS bundles prefer libraries beside the executable/app
# instead of accidentally loading incompatible system libraries. Windows is
# handled by copying the exact Qt DLLs next to LocalCall.exe.
if(UNIX AND NOT APPLE AND NOT ANDROID)
    set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)
    set(CMAKE_INSTALL_RPATH "$ORIGIN;$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
elseif(APPLE)
    set(CMAKE_INSTALL_RPATH "@executable_path/../Frameworks;@loader_path/../Frameworks")
endif()

# Helpful Qt auto-detection for local Qt online-installer builds. Linux distros,
# Arch, Nix and Homebrew normally do not need this because Qt is found through
# CMAKE_PREFIX_PATH, pkg-config, or the CMake package registry.
#
# The Windows build script and some users pass CMAKE_PREFIX_PATH manually. If
# that value points at a Qt version/kit that is not installed, vanilla CMake
# stops later at find_package(Qt6). Detect that case here and prepend a valid
# Qt kit if one exists, so a stale hardcoded path does not break the build.
set(_localcall_qt_prefix_valid FALSE)
if(DEFINED CMAKE_PREFIX_PATH)
    foreach(_qt_prefix IN LISTS CMAKE_PREFIX_PATH)
        if(EXISTS "${_qt_prefix}/lib/cmake/Qt6/Qt6Config.cmake" OR
           EXISTS "${_qt_prefix}/Qt6Config.cmake")
            set(_localcall_qt_prefix_valid TRUE)
            break()
        endif()
    endforeach()
endif()

set(_localcall_qt_candidates
        "C:/Qt/6.11.0/msvc2022_64"
        "C:/Qt/6.11.0/msvc2019_64"
        "C:/Qt/6.10.0/msvc2022_64"
        "C:/Qt/6.10.0/msvc2019_64"
        "C:/Qt/6.9.0/msvc2022_64"
        "C:/Qt/6.9.0/msvc2019_64"
        "C:/Qt/6.8.0/msvc2022_64"
        "C:/Qt/6.8.0/msvc2019_64"
        "C:/Qt/6.7.0/msvc2022_64"
        "C:/Qt/6.7.0/msvc2019_64"
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

if(WIN32)
    file(GLOB _localcall_qt_globbed_candidates
        LIST_DIRECTORIES TRUE
        "C:/Qt/6.*/*"
        "$ENV{USERPROFILE}/Qt/6.*/*")
    list(APPEND _localcall_qt_candidates ${_localcall_qt_globbed_candidates})
endif()

if(NOT _localcall_qt_prefix_valid)
    foreach(_candidate IN LISTS _localcall_qt_candidates)
        if(EXISTS "${_candidate}/lib/cmake/Qt6/Qt6Config.cmake")
            if(DEFINED CMAKE_PREFIX_PATH)
                set(CMAKE_PREFIX_PATH "${_candidate};${CMAKE_PREFIX_PATH}")
            else()
                set(CMAKE_PREFIX_PATH "${_candidate}")
            endif()
            set(_localcall_qt_prefix_valid TRUE)
            message(STATUS "Auto-detected Qt at ${_candidate}")
            break()
        endif()
    endforeach()
endif()

if(WIN32 AND NOT _localcall_qt_prefix_valid)
    message(STATUS
        "Qt6 was not auto-detected. Install a Qt 6 MSVC 2022 64-bit kit, "
        "or set CMAKE_PREFIX_PATH/Qt6_DIR to the folder containing Qt6Config.cmake.")
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
find_package(OpenSSL REQUIRED COMPONENTS Crypto)

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
set(OPENH264_AVAILABLE FALSE)
set(OPUS_AVAILABLE FALSE)
set(LIBYUV_AVAILABLE FALSE)
set(LIBDATACHANNEL_AVAILABLE FALSE)
set(LIBDATACHANNEL_TARGET "")
if(NOT LOCALCALL_WITH_WEBRTC STREQUAL "OFF")
    # vcpkg currently documents LibDataChannel::LibDataChannel, while several
    # Linux/Nix packages expose lowercase package names or plain datachannel
    # targets. Try all common variants before disabling the secure RTC path.
    find_package(LibDataChannel CONFIG QUIET)
    if(NOT LibDataChannel_FOUND AND NOT TARGET LibDataChannel::LibDataChannel)
        find_package(libdatachannel CONFIG QUIET)
    endif()
    foreach(_candidate
            LibDataChannel::LibDataChannel
            libdatachannel::libdatachannel
            datachannel
            datachannel-static)
        if(TARGET ${_candidate})
            set(LIBDATACHANNEL_AVAILABLE TRUE)
            set(LIBDATACHANNEL_TARGET ${_candidate})
            break()
        endif()
    endforeach()
    # If the package config did not create a known CMake target, fall back to
    # a manual header/library search. Do not mark it available from *_FOUND
    # alone, because that can produce an empty link line on some packages.
    if(NOT LIBDATACHANNEL_AVAILABLE)
        find_path(LIBDATACHANNEL_INCLUDE_DIR rtc/rtc.hpp PATH_SUFFIXES include)
        find_library(LIBDATACHANNEL_LIBRARY NAMES datachannel libdatachannel)
        if(LIBDATACHANNEL_INCLUDE_DIR AND LIBDATACHANNEL_LIBRARY)
            set(LIBDATACHANNEL_AVAILABLE TRUE)
        endif()
    endif()

    # vcpkg's opus port is named "opus" and normally provides Opus::opus.
    # Some Linux distributions expose pkg-config/library-only installs, so keep
    # a manual fallback to avoid disabling the RTC path unnecessarily.
    find_package(Opus CONFIG QUIET)
    if(Opus_FOUND OR TARGET Opus::opus)
        set(OPUS_AVAILABLE TRUE)
    else()
        find_path(OPUS_INCLUDE_DIR opus/opus.h PATH_SUFFIXES include)
        find_library(OPUS_LIBRARY NAMES opus libopus)
        if(OPUS_INCLUDE_DIR AND OPUS_LIBRARY)
            set(OPUS_AVAILABLE TRUE)
        endif()
    endif()

    # vcpkg's openh264 port currently installs headers/libs/pkg-config, not a
    # guaranteed CMake package config, so use find_path/find_library first.
    find_package(OpenH264 CONFIG QUIET)
    if(OpenH264_FOUND OR TARGET OpenH264::OpenH264 OR TARGET openh264::openh264)
        set(OPENH264_AVAILABLE TRUE)
    else()
        find_path(OPENH264_INCLUDE_DIR wels/codec_api.h PATH_SUFFIXES include)
        find_library(OPENH264_LIBRARY NAMES openh264 libopenh264)
        if(OPENH264_INCLUDE_DIR AND OPENH264_LIBRARY)
            set(OPENH264_AVAILABLE TRUE)
        endif()
    endif()

    find_package(libyuv CONFIG QUIET)
    if(NOT libyuv_FOUND)
        find_package(unofficial-libyuv CONFIG QUIET)
    endif()
    if(libyuv_FOUND OR unofficial-libyuv_FOUND OR TARGET libyuv::libyuv OR TARGET unofficial::libyuv::libyuv OR TARGET yuv)
        set(LIBYUV_AVAILABLE TRUE)
    else()
        find_path(LIBYUV_INCLUDE_DIR libyuv.h PATH_SUFFIXES include)
        find_library(LIBYUV_LIBRARY NAMES yuv libyuv)
        if(LIBYUV_INCLUDE_DIR AND LIBYUV_LIBRARY)
            set(LIBYUV_AVAILABLE TRUE)
        endif()
    endif()

    if(LIBDATACHANNEL_AVAILABLE AND HAS_MULTIMEDIA AND OPUS_AVAILABLE AND OPENH264_AVAILABLE AND LIBYUV_AVAILABLE)
        set(HAS_WEBRTC TRUE)
        message(STATUS "WebRTC/libdatachannel: ENABLED")
    elseif(LOCALCALL_WITH_WEBRTC STREQUAL "ON")
        message(FATAL_ERROR
            "\nWebRTC was requested, but one or more RTC dependencies were not found.\n"
            "  Qt6 Multimedia      : ${HAS_MULTIMEDIA}\n"
            "  libdatachannel      : ${LIBDATACHANNEL_AVAILABLE}\n"
            "  Opus                : ${OPUS_AVAILABLE}\n"
            "  OpenH264            : ${OPENH264_AVAILABLE}\n"
            "  libyuv              : ${LIBYUV_AVAILABLE}\n\n"
            "Windows fix: vcpkg installed the RTC libraries, but CMake did not find one of them. Delete build/, make sure CMAKE_TOOLCHAIN_FILE points to the same vcpkg root, then configure again.\n"
            "Linux fix: install libdatachannel, opus, openh264, libyuv, and Qt Multimedia development packages. Qt WebSockets is no longer required.\n")
    else()
        message(STATUS "WebRTC/libdatachannel: not found, optional RTC module disabled")
        message(STATUS "  Qt6 Multimedia      : ${HAS_MULTIMEDIA}")
        message(STATUS "  libdatachannel      : ${LIBDATACHANNEL_AVAILABLE}")
        message(STATUS "  Opus                : ${OPUS_AVAILABLE}")
        message(STATUS "  OpenH264            : ${OPENH264_AVAILABLE}")
        message(STATUS "  libyuv              : ${LIBYUV_AVAILABLE}")
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
    src/SecurityManager.cpp
    src/NotificationWindow.cpp
    src/InputDialog.cpp
    src/GroupCreateDialog.cpp
    src/GroupManageDialog.cpp
    src/MainWindow.cpp
    src/MainWindow_logic.cpp
    resources/resources.qrc
)

set(LOCALCALL_HEADERS
    include/CallTypes.h
    include/ChatMessage.h
    include/ChatStore.h
    include/FirewallHelper.h
    include/SecurityManager.h
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

if(HAS_MULTIMEDIA OR HAS_OPENCV OR HAS_WEBRTC)
    list(APPEND LOCALCALL_SOURCES src/CallWindow.cpp)
    list(APPEND LOCALCALL_HEADERS include/CallWindow.h)
    if(HAS_MULTIMEDIA OR HAS_OPENCV)
        list(APPEND LOCALCALL_SOURCES src/MediaWorker.cpp)
        list(APPEND LOCALCALL_HEADERS include/MediaWorker.h)
    endif()
endif()

if(HAS_WEBRTC)
    list(APPEND LOCALCALL_SOURCES
        src/MdnsDiscovery.cpp
        src/RtcPeer.cpp
        src/MediaPipeline.cpp
        src/UdpMediaPeer.cpp
        src/MediaTcpPeer.cpp)
    list(APPEND LOCALCALL_HEADERS
        include/MdnsDiscovery.h
        include/RtcPeer.h
        include/MediaPipeline.h
        include/UdpMediaPeer.h
        include/MediaTcpPeer.h)
endif()


if(WIN32)
    list(APPEND LOCALCALL_SOURCES src/WindowsRuntimeGuard.cpp)
    list(APPEND LOCALCALL_HEADERS include/WindowsRuntimeGuard.h)
endif()

if(WIN32)
    set(_RC_FILE "${CMAKE_BINARY_DIR}/app_icon.rc")
    set(_localcall_rc_contents "")
    if(EXISTS "${CMAKE_SOURCE_DIR}/icon.ico")
        string(APPEND _localcall_rc_contents "IDI_ICON1 ICON \"${CMAKE_SOURCE_DIR}/icon.ico\"\n")
    endif()
    if(EXISTS "${CMAKE_SOURCE_DIR}/packaging/windows/localcall.exe.manifest")
        string(APPEND _localcall_rc_contents "1 RT_MANIFEST \"${CMAKE_SOURCE_DIR}/packaging/windows/localcall.exe.manifest\"\n")
    endif()
    file(WRITE "${_RC_FILE}" "${_localcall_rc_contents}")
    list(APPEND LOCALCALL_SOURCES "${_RC_FILE}")
endif()

set(LOCALCALL_QT_TARGET LocalCall)
if(WIN32)
    # On Windows the visible LocalCall.exe is a tiny native launcher with no Qt
    # imports. The real Qt application is LocalCallApp.exe. This prevents
    # Windows loader entry-point popups from LocalCall.exe when stale Qt DLLs
    # are present, and lets the launcher repair/verify the Qt runtime before
    # starting the Qt process.
    set(LOCALCALL_QT_TARGET LocalCallApp)
endif()

if(ANDROID)
    qt_add_executable(${LOCALCALL_QT_TARGET} ${LOCALCALL_SOURCES} ${LOCALCALL_HEADERS})
elseif(WIN32)
    add_executable(${LOCALCALL_QT_TARGET} WIN32 ${LOCALCALL_SOURCES} ${LOCALCALL_HEADERS})
else()
    add_executable(${LOCALCALL_QT_TARGET} ${LOCALCALL_SOURCES} ${LOCALCALL_HEADERS})
endif()

if(WIN32)
    set_target_properties(${LOCALCALL_QT_TARGET} PROPERTIES
        OUTPUT_NAME "LocalCallApp"
        VERSION ${PROJECT_VERSION}
    )
    set(LOCALCALL_LAUNCHER_SOURCES src/WindowsLauncher.cpp)
    if(DEFINED _RC_FILE)
        list(APPEND LOCALCALL_LAUNCHER_SOURCES "${_RC_FILE}")
    endif()
    add_executable(LocalCall WIN32 ${LOCALCALL_LAUNCHER_SOURCES})
    set_target_properties(LocalCall PROPERTIES
        OUTPUT_NAME "LocalCall"
        VERSION ${PROJECT_VERSION}
    )
    target_compile_definitions(LocalCall PRIVATE LOCALCALL_VERSION="${PROJECT_VERSION}")
    target_link_libraries(LocalCall PRIVATE shell32)
    add_dependencies(LocalCall ${LOCALCALL_QT_TARGET})
else()
    set_target_properties(${LOCALCALL_QT_TARGET} PROPERTIES
        OUTPUT_NAME "LocalCall"
        VERSION ${PROJECT_VERSION}
    )
endif()

if(UNIX AND NOT APPLE AND NOT ANDROID)
    set_target_properties(${LOCALCALL_QT_TARGET} PROPERTIES
        BUILD_RPATH_USE_ORIGIN TRUE
        INSTALL_RPATH "$ORIGIN;$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
        INSTALL_RPATH_USE_LINK_PATH TRUE
    )
elseif(APPLE)
    set_target_properties(${LOCALCALL_QT_TARGET} PROPERTIES
        INSTALL_RPATH "@executable_path/../Frameworks;@loader_path/../Frameworks"
    )
endif()

target_include_directories(${LOCALCALL_QT_TARGET} PRIVATE include third_party)

target_compile_definitions(${LOCALCALL_QT_TARGET} PRIVATE
    LOCALCALL_VERSION="${PROJECT_VERSION}"
    LOCALCALL_PROTOCOL="localcall.v1"
)

target_link_libraries(${LOCALCALL_QT_TARGET} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Concurrent
    OpenSSL::Crypto
)

if(nlohmann_json_FOUND)
    target_link_libraries(${LOCALCALL_QT_TARGET} PRIVATE nlohmann_json::nlohmann_json)
endif()

if(HAS_MULTIMEDIA)
    target_link_libraries(${LOCALCALL_QT_TARGET} PRIVATE Qt6::Multimedia)
    if(Qt6MultimediaWidgets_FOUND)
        target_link_libraries(${LOCALCALL_QT_TARGET} PRIVATE Qt6::MultimediaWidgets)
    endif()
    target_compile_definitions(${LOCALCALL_QT_TARGET} PRIVATE HAS_MULTIMEDIA=1)
endif()

if(HAS_OPENCV)
    target_include_directories(${LOCALCALL_QT_TARGET} PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(${LOCALCALL_QT_TARGET} PRIVATE ${OpenCV_LIBS})
    target_compile_definitions(${LOCALCALL_QT_TARGET} PRIVATE HAS_OPENCV=1)
endif()

if(HAS_WEBRTC)
    if(LIBDATACHANNEL_TARGET)
        target_link_libraries(${LOCALCALL_QT_TARGET} PRIVATE ${LIBDATACHANNEL_TARGET})
    else()
        target_include_directories(${LOCALCALL_QT_TARGET} PRIVATE ${LIBDATACHANNEL_INCLUDE_DIR})
        target_link_libraries(${LOCALCALL_QT_TARGET} PRIVATE ${LIBDATACHANNEL_LIBRARY})
    endif()
    target_compile_definitions(${LOCALCALL_QT_TARGET} PRIVATE HAS_WEBRTC=1)

    if(TARGET OpenH264::OpenH264)
        target_link_libraries(${LOCALCALL_QT_TARGET} PRIVATE OpenH264::OpenH264)
    elseif(TARGET openh264::openh264)
        target_link_libraries(${LOCALCALL_QT_TARGET} PRIVATE openh264::openh264)
    else()
        target_include_directories(${LOCALCALL_QT_TARGET} PRIVATE ${OPENH264_INCLUDE_DIR})
        target_link_libraries(${LOCALCALL_QT_TARGET} PRIVATE ${OPENH264_LIBRARY})
    endif()

    if(TARGET Opus::opus)
        target_link_libraries(${LOCALCALL_QT_TARGET} PRIVATE Opus::opus)
    else()
        target_include_directories(${LOCALCALL_QT_TARGET} PRIVATE ${OPUS_INCLUDE_DIR})
        target_link_libraries(${LOCALCALL_QT_TARGET} PRIVATE ${OPUS_LIBRARY})
    endif()

    if(TARGET libyuv::libyuv)
        target_link_libraries(${LOCALCALL_QT_TARGET} PRIVATE libyuv::libyuv)
    elseif(TARGET unofficial::libyuv::libyuv)
        target_link_libraries(${LOCALCALL_QT_TARGET} PRIVATE unofficial::libyuv::libyuv)
    elseif(TARGET yuv)
        target_link_libraries(${LOCALCALL_QT_TARGET} PRIVATE yuv)
    else()
        target_include_directories(${LOCALCALL_QT_TARGET} PRIVATE ${LIBYUV_INCLUDE_DIR})
        target_link_libraries(${LOCALCALL_QT_TARGET} PRIVATE ${LIBYUV_LIBRARY})
    endif()
endif()

if(WIN32)
    target_link_libraries(${LOCALCALL_QT_TARGET} PRIVATE ws2_32 dwmapi shell32 advapi32)
    # Do NOT delay-load Qt DLLs. Qt exports several data symbols such as
    # QAbstractButton::staticMetaObject, and MSVC cannot delay-load a DLL when
    # the executable imports data symbols from it. Attempting to delay-load
    # Qt6Widgets.dll causes LNK1194. Runtime consistency is handled by deploying
    # the exact Qt DLLs beside the executable and by the Windows run/deploy scripts.
    set_target_properties(${LOCALCALL_QT_TARGET} PROPERTIES WIN32_EXECUTABLE TRUE)

    get_target_property(_qt_qmake_for_debug Qt6::qmake IMPORTED_LOCATION)
    if(_qt_qmake_for_debug)
        get_filename_component(_qt_bin_dir_for_debug "${_qt_qmake_for_debug}" DIRECTORY)
        # Visual Studio F5 debugging should also prefer the app folder and the
        # exact Qt kit used at configure time. This does not affect installed
        # packages, but avoids accidentally loading another Qt from PATH.
        set_target_properties(${LOCALCALL_QT_TARGET} PROPERTIES
            VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${LOCALCALL_QT_TARGET}>"
            VS_DEBUGGER_ENVIRONMENT "PATH=$<TARGET_FILE_DIR:${LOCALCALL_QT_TARGET}>;${_qt_bin_dir_for_debug};%PATH%;QT_PLUGIN_PATH=$<TARGET_FILE_DIR:${LOCALCALL_QT_TARGET}>;QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:${LOCALCALL_QT_TARGET}>/platforms")
    endif()

    if(LOCALCALL_POST_BUILD_DEPLOY_QT)
        get_target_property(_qt_qmake_executable Qt6::qmake IMPORTED_LOCATION)
        if(_qt_qmake_executable)
            get_filename_component(_qt_bin_dir "${_qt_qmake_executable}" DIRECTORY)
            find_program(LOCALCALL_WINDEPLOYQT_EXECUTABLE
                NAMES windeployqt windeployqt.exe
                HINTS "${_qt_bin_dir}"
                NO_DEFAULT_PATH)
            if(LOCALCALL_WINDEPLOYQT_EXECUTABLE)
                add_custom_command(TARGET ${LOCALCALL_QT_TARGET} POST_BUILD
                    COMMAND "${CMAKE_COMMAND}"
                        "-DLOCALCALL_TARGET_FILE=$<TARGET_FILE:${LOCALCALL_QT_TARGET}>"
                        "-DLOCALCALL_TARGET_DIR=$<TARGET_FILE_DIR:${LOCALCALL_QT_TARGET}>"
                        "-DLOCALCALL_QT_BIN_DIR=${_qt_bin_dir}"
                        "-DLOCALCALL_WINDEPLOYQT=${LOCALCALL_WINDEPLOYQT_EXECUTABLE}"
                        "-DLOCALCALL_CONFIG=$<CONFIG>"
                        -P "${CMAKE_SOURCE_DIR}/cmake/deploy_windows_qt.cmake"
                    COMMENT "Deploying matching Qt runtime next to LocalCall.exe"
                    VERBATIM)
            else()
                message(WARNING "LOCALCALL_POST_BUILD_DEPLOY_QT is ON, but matching windeployqt was not found in ${_qt_bin_dir}")
            endif()
        else()
            message(WARNING "LOCALCALL_POST_BUILD_DEPLOY_QT is ON, but Qt6::qmake was not available")
        endif()
    endif()
endif()

if(APPLE)
    set_target_properties(${LOCALCALL_QT_TARGET} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Local Call"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.localcall.LocalCall"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/icon.ico")
    add_custom_command(TARGET ${LOCALCALL_QT_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/icon.ico"
                "$<TARGET_FILE_DIR:${LOCALCALL_QT_TARGET}>/icon.ico"
        COMMENT "Copying icon.ico next to Qt app")
    if(WIN32 AND TARGET LocalCall)
        add_custom_command(TARGET LocalCall POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_SOURCE_DIR}/icon.ico"
                    "$<TARGET_FILE_DIR:LocalCall>/icon.ico"
            COMMENT "Copying icon.ico next to launcher")
    endif()
endif()

install(TARGETS ${LOCALCALL_QT_TARGET}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE  DESTINATION .
)
if(WIN32 AND TARGET LocalCall)
    install(TARGETS LocalCall RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

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

if(LOCALCALL_INSTALL_QT_RUNTIME)
    if(COMMAND qt_generate_deploy_app_script)
        qt_generate_deploy_app_script(
            TARGET ${LOCALCALL_QT_TARGET}
            OUTPUT_SCRIPT LOCALCALL_QT_DEPLOY_SCRIPT
            NO_TRANSLATIONS
            NO_UNSUPPORTED_PLATFORM_ERROR
        )
        install(SCRIPT ${LOCALCALL_QT_DEPLOY_SCRIPT})
    else()
        message(WARNING "Qt CMake deployment API is unavailable. Use scripts/deploy-windows.ps1, windeployqt, macdeployqt, or distro packaging manually.")
    endif()
endif()

set(CPACK_PACKAGE_NAME "LocalCall")
set(CPACK_PACKAGE_VENDOR "Local Call Project")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Low-latency secure local and internet calling app")
if(WIN32)
    set(CPACK_GENERATOR "ZIP")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop;ZIP")
else()
    set(CPACK_GENERATOR "TGZ;ZIP")
endif()
include(CPack)

message(STATUS "")
message(STATUS "=== Local Call build configuration ===")
message(STATUS "  Version       : ${PROJECT_VERSION}")
message(STATUS "  Platform      : ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Qt Multimedia : ${HAS_MULTIMEDIA}")
message(STATUS "  OpenCV        : ${HAS_OPENCV}")
message(STATUS "  WebRTC P2P    : ${HAS_WEBRTC}")
message(STATUS "  Qt install deploy : ${LOCALCALL_INSTALL_QT_RUNTIME}")
message(STATUS "  Win post-build Qt deploy : ${LOCALCALL_POST_BUILD_DEPLOY_QT}")
message(STATUS "======================================")
