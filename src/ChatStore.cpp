#include "ChatStore.h"
#include "Helpers.h"
#include "nlohmann/json.hpp"
#include <QDir>
#include <QFile>
#include <QSet>
#include <QStandardPaths>

using json = nlohmann::json;

ChatStore::ChatStore(QObject* parent) : QObject(parent)
{
    QDir().mkpath(dataDir());
}

QString ChatStore::dataDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + "/Local Call/chats";
}

QString ChatStore::sanitiseKey(const QString& key) const
{
    QString s;
    for (QChar c : key)
        s += (c.isLetterOrNumber() || c == '-') ? c : '_';
    return s;
}

QString ChatStore::filePath(const QString& key) const
{
    return dataDir() + "/" + sanitiseKey(key) + ".json";
}

QList<ChatMessage> ChatStore::load(const QString& convKey)
{
    if (!m_cache.contains(convKey)) {
        QList<StoredMessage> stored;
        QFile f(filePath(convKey));
        if (f.open(QIODevice::ReadOnly)) {
            try {
                auto arr = json::parse(f.readAll().toStdString());
                for (const auto& v : arr)
                    stored.append(v.get<StoredMessage>());
            } catch (...) {}
        }
        m_cache[convKey] = stored;
    }

    QList<ChatMessage> result;
    for (const auto& s : m_cache[convKey])
        result.append(toMessage(s));
    return result;
}

void ChatStore::append(const QString& convKey, const ChatMessage& msg)
{
    if (!m_cache.contains(convKey)) load(convKey);
    m_cache[convKey].append(toStored(msg));

    try {
        json arr = json::array();
        for (const auto& s : m_cache[convKey]) arr.push_back(s);
        QFile out(filePath(convKey));
        if (out.open(QIODevice::WriteOnly))
            out.write(QByteArray::fromStdString(arr.dump()));
    } catch (...) {}
}

ChatMessage ChatStore::toMessage(const StoredMessage& s)
{
    ChatMessage m;
    if      (s.kind == "Text")      m.kind = MessageKind::Text;
    else if (s.kind == "Image")     m.kind = MessageKind::Image;
    else if (s.kind == "File")      m.kind = MessageKind::File;
    else if (s.kind == "VoiceNote") m.kind = MessageKind::VoiceNote;
    else                            m.kind = MessageKind::Text;

    m.fromId    = s.fromId;
    m.fromName  = s.fromName;
    m.text      = s.text;
    m.fileName  = s.fileName;
    m.mime      = s.mime;
    m.isMine    = s.isMine;
    m.timestamp = s.ts;
    if (!s.data.empty()) m.data = Helpers::base64Decode(s.data);
    return m;
}

StoredMessage ChatStore::toStored(const ChatMessage& m)
{
    StoredMessage s;
    switch (m.kind) {
        case MessageKind::Text:      s.kind = "Text";      break;
        case MessageKind::Image:     s.kind = "Image";     break;
        case MessageKind::File:      s.kind = "File";      break;
        case MessageKind::VoiceNote: s.kind = "VoiceNote"; break;
        default:                     s.kind = "Text";      break;
    }
    s.fromId   = m.fromId;
    s.fromName = m.fromName;
    s.text     = m.text;
    s.fileName = m.fileName;
    s.mime     = m.mime;
    s.isMine   = m.isMine;
    s.ts       = m.timestamp;
    if (!m.data.empty()) s.data = Helpers::base64Encode(m.data);
    return s;
}

void ChatStore::deleteConversation(const QString& convKey)
{
    m_cache.remove(convKey);
    QFile::remove(filePath(convKey));
}

void ChatStore::deleteMessages(const QString& convKey, const QList<int64_t>& timestamps)
{
    if (!m_cache.contains(convKey)) load(convKey);
    QSet<int64_t> toRemove(timestamps.begin(), timestamps.end());
    auto& stored = m_cache[convKey];
    stored.removeIf([&](const StoredMessage& s){ return toRemove.contains(s.ts); });

    try {
        json arr = json::array();
        for (const auto& s : stored) arr.push_back(s);
        QFile out(filePath(convKey));
        if (out.open(QIODevice::WriteOnly))
            out.write(QByteArray::fromStdString(arr.dump()));
    } catch (...) {}
}
