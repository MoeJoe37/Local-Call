# Deploy the exact Qt runtime that CMake used to link LocalCall.
# This script is intentionally aggressive: it removes stale Qt DLLs/plugins
# and re-copies the selected Qt kit so old build caches cannot keep launching
# against incompatible Qt binaries.
# This avoids the common Windows failure where a different Qt6Widgets.dll is
# copied by a windeployqt.exe found earlier in PATH.

foreach(_required LOCALCALL_TARGET_FILE LOCALCALL_TARGET_DIR LOCALCALL_QT_BIN_DIR LOCALCALL_WINDEPLOYQT LOCALCALL_CONFIG)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} was not provided to deploy_windows_qt.cmake")
    endif()

    # MSBuild/CMake can pass -D values with literal quotes depending on the
    # generator and escaping rules. Strip them here so EXISTS checks do not look
    # for a file literally named "C:/path/app.exe".
    string(STRIP "${${_required}}" _value)
    string(REGEX REPLACE "^\"(.*)\"$" "\\1" _value "${_value}")
    string(REGEX REPLACE "^'(.*)'$" "\\1" _value "${_value}")
    set(${_required} "${_value}")
endforeach()

file(TO_CMAKE_PATH "${LOCALCALL_TARGET_FILE}" LOCALCALL_TARGET_FILE)
file(TO_CMAKE_PATH "${LOCALCALL_TARGET_DIR}" LOCALCALL_TARGET_DIR)
file(TO_CMAKE_PATH "${LOCALCALL_QT_BIN_DIR}" LOCALCALL_QT_BIN_DIR)
file(TO_CMAKE_PATH "${LOCALCALL_WINDEPLOYQT}" LOCALCALL_WINDEPLOYQT)

if(NOT EXISTS "${LOCALCALL_TARGET_FILE}")
    message(FATAL_ERROR
        "Target executable does not exist after quote/path normalization:\n"
        "  ${LOCALCALL_TARGET_FILE}\n"
        "Build the executable first, or disable automatic deployment with -DLOCALCALL_POST_BUILD_DEPLOY_QT=OFF.")
endif()

if(NOT EXISTS "${LOCALCALL_WINDEPLOYQT}")
    message(FATAL_ERROR "windeployqt was not found: ${LOCALCALL_WINDEPLOYQT}")
endif()

# Clean stale Qt files before deploying. Stale DLLs are the usual cause of
# entry-point errors such as QWidget::size not found: the executable was linked
# against one Qt build but loads a different Qt DLL at runtime.
file(GLOB _stale_qt_dlls
    "${LOCALCALL_TARGET_DIR}/Qt6*.dll"
    "${LOCALCALL_TARGET_DIR}/Qt6*.pdb"
)
if(_stale_qt_dlls)
    file(REMOVE ${_stale_qt_dlls})
endif()

foreach(_plugin_dir
        platforms styles imageformats iconengines generic networkinformation
        multimedia mediaservice audio tls sqldrivers printsupport translations
        accessible bearer)
    if(EXISTS "${LOCALCALL_TARGET_DIR}/${_plugin_dir}")
        file(REMOVE_RECURSE "${LOCALCALL_TARGET_DIR}/${_plugin_dir}")
    endif()
endforeach()

set(_mode --release)
if("${LOCALCALL_CONFIG}" MATCHES "^[Dd]ebug$")
    set(_mode --debug)
endif()

message(STATUS "Deploying Qt runtime from: ${LOCALCALL_QT_BIN_DIR}")
message(STATUS "Using deploy tool       : ${LOCALCALL_WINDEPLOYQT}")
message(STATUS "Deploy target           : ${LOCALCALL_TARGET_FILE}")

# Avoid CMake list splitting in PATH=...;... by setting ENV{PATH} directly.
# This is more reliable on Windows generators than passing PATH through
# cmake -E env from inside script mode.
set(_localcall_old_path "$ENV{PATH}")
set(ENV{PATH} "${LOCALCALL_QT_BIN_DIR};$ENV{PATH}")
execute_process(
    COMMAND "${LOCALCALL_WINDEPLOYQT}" ${_mode} --compiler-runtime --no-translations
            "${LOCALCALL_TARGET_FILE}"
    RESULT_VARIABLE _deploy_result
    OUTPUT_VARIABLE _deploy_stdout
    ERROR_VARIABLE  _deploy_stderr
)
set(ENV{PATH} "${_localcall_old_path}")

if(NOT _deploy_result EQUAL 0)
    message(STATUS "windeployqt stdout:\n${_deploy_stdout}")
    message(STATUS "windeployqt stderr:\n${_deploy_stderr}")
    message(FATAL_ERROR "windeployqt failed with exit code ${_deploy_result}")
endif()


# Force deployed Qt DLLs to exactly match the Qt kit used by CMake.
# windeployqt normally handles this, but this final pass prevents stale or
# PATH-selected Qt DLLs from causing Windows entry-point errors at startup.
# Copy the direct modules first, then verify every Qt6*.dll that exists beside
# the EXE against the selected kit.
foreach(_required_qt_dll
        Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll Qt6Network.dll Qt6Concurrent.dll
        Qt6Multimedia.dll Qt6MultimediaWidgets.dll)
    if(EXISTS "${LOCALCALL_QT_BIN_DIR}/${_required_qt_dll}")
        file(COPY_FILE
            "${LOCALCALL_QT_BIN_DIR}/${_required_qt_dll}"
            "${LOCALCALL_TARGET_DIR}/${_required_qt_dll}"
            ONLY_IF_DIFFERENT)
    endif()
endforeach()

file(GLOB _deployed_qt_dlls "${LOCALCALL_TARGET_DIR}/Qt6*.dll")
foreach(_qt_dll IN LISTS _deployed_qt_dlls)
    get_filename_component(_qt_dll_name "${_qt_dll}" NAME)
    set(_kit_qt_dll "${LOCALCALL_QT_BIN_DIR}/${_qt_dll_name}")
    if(EXISTS "${_kit_qt_dll}")
        file(COPY_FILE "${_kit_qt_dll}" "${_qt_dll}" ONLY_IF_DIFFERENT)
    else()
        message(FATAL_ERROR
            "Qt runtime mismatch guard failed. Deployed ${_qt_dll_name}, "
            "but it does not exist in the selected Qt kit: ${LOCALCALL_QT_BIN_DIR}")
    endif()
endforeach()

# Ensure the Windows platform plugin also comes from the same Qt kit.
set(_kit_qwindows "${LOCALCALL_QT_BIN_DIR}/../plugins/platforms/qwindows.dll")
set(_out_platforms "${LOCALCALL_TARGET_DIR}/platforms")
if(EXISTS "${_kit_qwindows}")
    file(MAKE_DIRECTORY "${_out_platforms}")
    file(COPY_FILE "${_kit_qwindows}" "${_out_platforms}/qwindows.dll" ONLY_IF_DIFFERENT)
endif()

file(WRITE "${LOCALCALL_TARGET_DIR}/qt.conf"
"[Paths]\n"
"Prefix=.\n"
"Plugins=.\n"
"Imports=.\n"
"Qml2Imports=.\n")

# The Windows launcher reads this file before starting LocalCallApp.exe and can
# refresh stale Qt DLLs from the same Qt kit that CMake used. This prevents
# repeated loader entry-point popups when users run build/Release/LocalCall.exe.
get_filename_component(_localcall_qt_prefix "${LOCALCALL_QT_BIN_DIR}/.." ABSOLUTE)
file(WRITE "${LOCALCALL_TARGET_DIR}/localcall-qt-prefix.txt" "${_localcall_qt_prefix}\n")

message(STATUS "windeployqt completed successfully.")
