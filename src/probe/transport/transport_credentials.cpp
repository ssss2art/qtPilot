// Copyright (c) 2026 qtPilot Contributors
// SPDX-License-Identifier: MIT
#include "transport/transport_credentials.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QRegularExpression>
#ifndef QT_NO_SSL
#include <QSslCertificate>
#include <QSslKey>
#include <QSslSocket>
#endif

namespace qtPilot {
namespace {
std::expected<QByteArray, QString> readCredential(const QString& path, qint64 limit) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return std::unexpected(QStringLiteral("Cannot read configured credential file"));
  }
  const auto bytes = file.read(limit + 1);
  if (bytes.isEmpty() || bytes.size() > limit) {
    return std::unexpected(QStringLiteral("Invalid credential file size"));
  }
  return bytes;
}
}  // namespace

bool TransportCredentials::accepts(const QByteArray& authorization) const {
  if (tokenDigest.isEmpty()) {
    return true;
  }
  if (authorization.size() < 39 || authorization.size() > 263 ||
      authorization.left(7).compare(QByteArray("Bearer "), Qt::CaseInsensitive) != 0) {
    return false;
  }
  const auto actual = QCryptographicHash::hash(authorization.mid(7), QCryptographicHash::Sha256);
  // Compare fixed-size digests without an early exit on any matching prefix.
  unsigned int difference = 0;
  for (int i = 0; i < tokenDigest.size(); ++i) {
    difference |=
        static_cast<unsigned char>(actual[i]) ^ static_cast<unsigned char>(tokenDigest[i]);
  }
  return difference == 0;
}

std::expected<TransportCredentials, QString> loadTransportCredentials(const NetworkPolicy& policy) {
  TransportCredentials credentials;
  if (policy.authenticationRequired()) {
    auto token = readCredential(policy.tokenFile, 258);
    if (!token) {
      return std::unexpected(token.error());
    }
    if (token->endsWith('\n')) {
      token->chop(1);
    }
    if (token->endsWith('\r')) {
      token->chop(1);
    }
    static const QRegularExpression s_tokenPattern(QStringLiteral("\\A[A-Za-z0-9_-]{32,256}\\z"));
    if (!s_tokenPattern.match(QString::fromLatin1(*token)).hasMatch()) {
      return std::unexpected(QStringLiteral("Invalid authentication credential format"));
    }
    credentials.tokenDigest = QCryptographicHash::hash(*token, QCryptographicHash::Sha256);
  }
  if (policy.tlsEnabled()) {
#ifndef QT_NO_SSL
    if (!QSslSocket::supportsSsl()) {
      return std::unexpected(QStringLiteral("TLS backend is unavailable"));
    }
    auto certificate = readCredential(policy.certificateFile, 1024 * 1024);
    auto key = readCredential(policy.privateKeyFile, 1024 * 1024);
    if (!certificate || !key) {
      return std::unexpected(QStringLiteral("Cannot read TLS credentials"));
    }
    const auto chain = QSslCertificate::fromData(*certificate, QSsl::Pem);
    QSslKey privateKey(*key, QSsl::Rsa, QSsl::Pem);
    if (privateKey.isNull()) {
      privateKey = QSslKey(*key, QSsl::Ec, QSsl::Pem);
    }
    if (chain.isEmpty() || privateKey.isNull()) {
      return std::unexpected(QStringLiteral("Invalid TLS certificate or RSA/EC private key"));
    }
    const auto now = QDateTime::currentDateTimeUtc();
    if (chain.first().effectiveDate() > now || chain.first().expiryDate() <= now) {
      return std::unexpected(QStringLiteral("TLS certificate is outside its validity period"));
    }
    credentials.tls = true;
    credentials.ssl = QSslConfiguration::defaultConfiguration();
    credentials.ssl.setProtocol(QSsl::TlsV1_2OrLater);
    credentials.ssl.setLocalCertificateChain(chain);
    credentials.ssl.setPrivateKey(privateKey);
    credentials.ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
#else
    return std::unexpected(QStringLiteral("TLS support is unavailable in this Qt build"));
#endif
  }
  return credentials;
}
}  // namespace qtPilot
