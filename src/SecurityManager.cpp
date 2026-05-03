#include "SecurityManager.h"
#include "Helpers.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <openssl/evp.h>
#include <openssl/err.h>

namespace {
QString identityPath()
{
    QDir().mkpath(Helpers::appDataRoot());
    return QDir(Helpers::appDataRoot()).filePath("identity-ed25519.json");
}

QString b64(const QByteArray& bytes)
{
    return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QByteArray unb64(const QString& text)
{
    return QByteArray::fromBase64(text.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}
}

SecurityManager::SecurityManager(QObject* parent) : QObject(parent) {}

bool SecurityManager::loadOrCreate()
{
    if (loadIdentity()) return true;
    return createIdentity() && saveIdentity();
}

bool SecurityManager::loadIdentity()
{
    QFile f(identityPath());
    if (!f.open(QIODevice::ReadOnly)) return false;
    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return false;
    const auto obj = doc.object();
    m_privateKeyB64 = obj.value("private_key_ed25519_raw_b64url").toString();
    m_publicKeyB64  = obj.value("public_key_ed25519_raw_b64url").toString();
    m_privateKeyRaw = unb64(m_privateKeyB64);
    m_publicKeyRaw  = unb64(m_publicKeyB64);
    if (m_privateKeyRaw.size() != 32 || m_publicKeyRaw.size() != 32) return false;
    m_fingerprint = fingerprintForPublicKey(m_publicKeyB64);
    return true;
}

bool SecurityManager::createIdentity()
{
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
    if (!ctx) return false;

    EVP_PKEY* pkey = nullptr;
    bool ok = false;
    if (EVP_PKEY_keygen_init(ctx) == 1 && EVP_PKEY_keygen(ctx, &pkey) == 1 && pkey) {
        unsigned char pub[32];
        unsigned char priv[32];
        size_t pubLen = sizeof(pub);
        size_t privLen = sizeof(priv);
        if (EVP_PKEY_get_raw_public_key(pkey, pub, &pubLen) == 1 &&
            EVP_PKEY_get_raw_private_key(pkey, priv, &privLen) == 1 &&
            pubLen == sizeof(pub) && privLen == sizeof(priv)) {
            m_publicKeyRaw  = QByteArray(reinterpret_cast<const char*>(pub),  static_cast<int>(pubLen));
            m_privateKeyRaw = QByteArray(reinterpret_cast<const char*>(priv), static_cast<int>(privLen));
            m_publicKeyB64  = b64(m_publicKeyRaw);
            m_privateKeyB64 = b64(m_privateKeyRaw);
            m_fingerprint   = fingerprintForPublicKey(m_publicKeyB64);
            ok = true;
        }
    }
    if (pkey) EVP_PKEY_free(pkey);
    EVP_PKEY_CTX_free(ctx);
    return ok;
}

bool SecurityManager::saveIdentity() const
{
    QSaveFile f(identityPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;

    QJsonObject obj;
    obj["version"] = 1;
    obj["algorithm"] = "ed25519";
    obj["public_key_ed25519_raw_b64url"] = m_publicKeyB64;
    obj["private_key_ed25519_raw_b64url"] = m_privateKeyB64;
    obj["fingerprint_sha256_b64url"] = m_fingerprint;

    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return f.commit();
}

QString SecurityManager::fingerprintForPublicKey(const QString& publicKeyB64)
{
    const QByteArray pub = unb64(publicKeyB64);
    const QByteArray digest = QCryptographicHash::hash(pub, QCryptographicHash::Sha256);
    return b64(digest);
}

QByteArray SecurityManager::canonicalBytesForSigning(const SigMsg& msg)
{
    SigMsg copy = msg;
    copy.auth_signature.reset();
    nlohmann::json j = copy;
    return QByteArray::fromStdString(j.dump());
}

bool SecurityManager::signMessage(SigMsg& msg) const
{
    if (m_privateKeyRaw.size() != 32 || m_publicKeyRaw.size() != 32) return false;

    msg.auth_alg = "ed25519";
    msg.auth_public_key = m_publicKeyB64.toStdString();
    msg.auth_fingerprint = m_fingerprint.toStdString();
    msg.auth_signature.reset();

    EVP_PKEY* pkey = EVP_PKEY_new_raw_private_key(
        EVP_PKEY_ED25519, nullptr,
        reinterpret_cast<const unsigned char*>(m_privateKeyRaw.constData()),
        static_cast<size_t>(m_privateKeyRaw.size()));
    if (!pkey) return false;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) { EVP_PKEY_free(pkey); return false; }

    const QByteArray payload = canonicalBytesForSigning(msg);
    size_t sigLen = 0;
    bool ok = false;
    if (EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, pkey) == 1 &&
        EVP_DigestSign(ctx, nullptr, &sigLen,
                       reinterpret_cast<const unsigned char*>(payload.constData()),
                       static_cast<size_t>(payload.size())) == 1) {
        QByteArray sig(static_cast<int>(sigLen), Qt::Uninitialized);
        if (EVP_DigestSign(ctx,
                           reinterpret_cast<unsigned char*>(sig.data()), &sigLen,
                           reinterpret_cast<const unsigned char*>(payload.constData()),
                           static_cast<size_t>(payload.size())) == 1) {
            sig.resize(static_cast<int>(sigLen));
            msg.auth_signature = b64(sig).toStdString();
            ok = true;
        }
    }

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return ok;
}

bool SecurityManager::verifyMessage(const SigMsg& msg, const QString& expectedPublicKeyB64) const
{
    if (!msg.auth_public_key || !msg.auth_signature) return false;
    const QString pubB64 = QString::fromStdString(*msg.auth_public_key);
    if (!expectedPublicKeyB64.isEmpty() && pubB64 != expectedPublicKeyB64) return false;

    const QByteArray pub = unb64(pubB64);
    const QByteArray sig = unb64(QString::fromStdString(*msg.auth_signature));
    if (pub.size() != 32 || sig.isEmpty()) return false;

    EVP_PKEY* pkey = EVP_PKEY_new_raw_public_key(
        EVP_PKEY_ED25519, nullptr,
        reinterpret_cast<const unsigned char*>(pub.constData()),
        static_cast<size_t>(pub.size()));
    if (!pkey) return false;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) { EVP_PKEY_free(pkey); return false; }

    const QByteArray payload = canonicalBytesForSigning(msg);
    const int rc = EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, pkey) == 1
        ? EVP_DigestVerify(ctx,
                           reinterpret_cast<const unsigned char*>(sig.constData()),
                           static_cast<size_t>(sig.size()),
                           reinterpret_cast<const unsigned char*>(payload.constData()),
                           static_cast<size_t>(payload.size()))
        : 0;

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return rc == 1;
}
