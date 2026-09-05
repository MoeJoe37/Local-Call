#pragma once

#include <QString>
#include <QMetaType>

/// Live call telemetry, produced by MediaTransport + MediaPipeline and rendered
/// by the call window's stats overlay.  Replaces the old signaling-port TCP
/// connect timing, which measured the wrong path entirely.
struct CallStats {
    QString transport;          // "UDP" / "TCP" / "WebRTC"
    QString audioCodec;
    QString videoCodec;

    int  kbpsUp        {0};
    int  kbpsDown      {0};
    int  rttMs         {-1};    // -1 = not measured yet
    int  lossPercent   {0};     // inbound chunk loss
    int  videoWidth    {0};
    int  videoHeight   {0};
    int  fpsIn         {0};
    int  fpsOut        {0};
    int  jitterMs      {0};
    int  droppedFrames {0};     // outbound frames dropped by transport backpressure
};

Q_DECLARE_METATYPE(CallStats)
