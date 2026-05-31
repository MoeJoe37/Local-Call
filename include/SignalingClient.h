#pragma once
#include "SigMsg.h"
#include <QString>

namespace SignalingClient {
    // Fire-and-forget (1 attempt)
    void send(const QString& ip, const SigMsg& msg);
    // Reliable (3 attempts, 600ms apart, background thread)
    void sendReliable(const QString& ip, const SigMsg& msg);
    // Reliable blocking send for ordered transfers such as voice-note chunks.
    bool sendReliableBlocking(const QString& ip, const SigMsg& msg, int attempts = 3, int retryDelayMs = 120);
}
