#pragma once

/// Shared call-mode enum — kept in its own header so it can be included
/// unconditionally (CallWindow.h itself is guarded by HAS_MEDIA_AUDIO).
enum class CallMode { Voice, VideoCamera, VideoScreen };
