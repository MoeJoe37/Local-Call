#pragma once

// Must not include Qt headers here. This guard runs before QApplication.
// The Qt DLLs are normal imports, so the deployment scripts must place matching
// DLLs beside LocalCall.exe; this guard then validates the loaded runtime and
// fixes plugin/runtime search paths before Qt creates windows.
bool localcall_prepare_windows_runtime();
