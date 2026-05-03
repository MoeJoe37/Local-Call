#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include "SigMsg.h"

class SecurityManager : public QObject {
    Q_OBJECT
public:
    explicit SecurityManager(QObject* parent = nullptr);

    bool loadOrCreate();

    QString publicKeyBase64() const noexcept { return m_publicKeyB64; }
    QString fingerprint() const noexcept { return m_fingerprint; }

    bool signMessage(SigMsg& msg) const;
    bool verifyMessage(const SigMsg& msg, const QString& expectedPublicKeyB64 = {}) const;

    static QString fingerprintForPublicKey(const QString& publicKeyB64);
    static QByteArray canonicalBytesForSigning(const SigMsg& msg);

private:
    bool loadIdentity();
    bool createIdentity();
    bool saveIdentity() const;

    QByteArray m_privateKeyRaw;
    QByteArray m_publicKeyRaw;
    QString    m_privateKeyB64;
    QString    m_publicKeyB64;
    QString    m_fingerprint;
};
