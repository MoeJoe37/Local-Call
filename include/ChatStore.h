#pragma once
#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include "ChatMessage.h"

class ChatStore : public QObject {
    Q_OBJECT
public:
    explicit ChatStore(QObject* parent = nullptr);

    QList<ChatMessage> load(const QString& convKey);
    void append(const QString& convKey, const ChatMessage& msg);
    void deleteConversation(const QString& convKey);
    void deleteMessages(const QString& convKey, const QList<int64_t>& timestamps);

private:
    QString dataDir() const;
    QString filePath(const QString& key) const;
    QString sanitiseKey(const QString& key) const;
    static ChatMessage toMessage(const StoredMessage& s);
    static StoredMessage toStored(const ChatMessage& m);

    QMap<QString, QList<StoredMessage>> m_cache;
};
