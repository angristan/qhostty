#include "SingleInstance.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QUuid>

#if defined(Q_OS_LINUX)
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
constexpr int kProtocolVersion = 1;
constexpr qsizetype kMaximumPayloadSize = 1024 * 1024;
}  // namespace

SingleInstance::SingleInstance(QString serverName, QObject* parent)
    : QObject(parent),
      m_serverName(QDir::isAbsolutePath(serverName)
                       ? std::move(serverName)
                       : QDir::temp().absoluteFilePath(serverName)),
      m_server(new QLocalServer(this)),
      m_lock(new QLockFile(m_serverName + QStringLiteral(".lock"))) {
  m_lock->setStaleLockTime(0);
  m_server->setSocketOptions(QLocalServer::UserAccessOption);
  connect(m_server, &QLocalServer::newConnection, this,
          &SingleInstance::acceptConnections);
}

SingleInstance::~SingleInstance() {
  if (m_primary) {
    m_server->close();
    QLocalServer::removeServer(m_serverName);
    m_lock->unlock();
  }
  delete m_lock;
}

SingleInstance::StartResult SingleInstance::start(
    const QStringList& arguments,
    const QString& workingDirectory,
    int timeoutMs) {
  // Probe before listening because QLocalServer may unlink an existing Unix
  // socket path when it binds.
  const ForwardResult initial = forward(arguments, workingDirectory, timeoutMs);
  if (initial == ForwardResult::Accepted) {
    return StartResult::Forwarded;
  }
  if (initial == ForwardResult::Failed) {
    return StartResult::Failed;
  }

  if (!m_lock->tryLock(timeoutMs)) {
    // Another process may still be between taking the lock and listening.
    const ForwardResult retry = forward(arguments, workingDirectory, timeoutMs);
    if (retry == ForwardResult::Accepted) {
      return StartResult::Forwarded;
    }
    m_error = retry == ForwardResult::Failed
                  ? m_error
                  : QStringLiteral("Another Qhostty instance is starting");
    return StartResult::Failed;
  }

  // The election lock proves no live primary owns this endpoint. Remove any
  // filesystem socket left by a crashed process, then bind as the primary.
  QLocalServer::removeServer(m_serverName);
  if (listen()) {
    return StartResult::Primary;
  }

  m_lock->unlock();
  m_error = m_server->errorString();
  return StartResult::Failed;
}

bool SingleInstance::listen() {
  if (!m_server->listen(m_serverName)) {
    m_error = m_server->errorString();
    return false;
  }
  m_primary = true;
  m_error.clear();
  return true;
}

SingleInstance::ForwardResult SingleInstance::forward(
    const QStringList& arguments,
    const QString& workingDirectory,
    int timeoutMs) {
  QLocalSocket socket;
  socket.connectToServer(m_serverName, QIODevice::ReadWrite);
  if (!socket.waitForConnected(timeoutMs)) {
    m_error = socket.errorString();
    return socket.error() == QLocalSocket::ServerNotFoundError ||
                   socket.error() == QLocalSocket::ConnectionRefusedError
               ? ForwardResult::NoServer
               : ForwardResult::Failed;
  }

  const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  QJsonObject payload;
  payload.insert(QStringLiteral("version"), kProtocolVersion);
  payload.insert(QStringLiteral("requestId"), requestId);
  payload.insert(QStringLiteral("arguments"),
                 QJsonArray::fromStringList(arguments));
  payload.insert(QStringLiteral("workingDirectory"),
                 QDir(workingDirectory).absolutePath());
  QByteArray data = QJsonDocument(payload).toJson(QJsonDocument::Compact);
  data.append('\n');

  if (data.size() > kMaximumPayloadSize || socket.write(data) != data.size()) {
    m_error = data.size() > kMaximumPayloadSize
                  ? QStringLiteral("Activation request is too large")
                  : socket.errorString();
    return ForwardResult::Failed;
  }
  socket.flush();
  if (socket.bytesToWrite() > 0 && !socket.waitForBytesWritten(timeoutMs)) {
    m_error = socket.errorString();
    return ForwardResult::Failed;
  }
  if (!socket.waitForReadyRead(timeoutMs) && socket.bytesAvailable() == 0) {
    m_error = QStringLiteral("The primary Qhostty instance did not reply");
    return ForwardResult::Failed;
  }

  const QByteArray replyData = socket.readLine().trimmed();
  QJsonParseError parseError{};
  const QJsonDocument reply = QJsonDocument::fromJson(replyData, &parseError);
  if (parseError.error != QJsonParseError::NoError || !reply.isObject() ||
      reply.object().value(QStringLiteral("version")).toInt() !=
          kProtocolVersion ||
      reply.object().value(QStringLiteral("requestId")).toString() !=
          requestId ||
      reply.object().value(QStringLiteral("status")).toString() !=
          QStringLiteral("accepted")) {
    m_error =
        QStringLiteral("The primary Qhostty instance rejected activation");
    return ForwardResult::Failed;
  }

  socket.disconnectFromServer();
  m_error.clear();
  return ForwardResult::Accepted;
}

void SingleInstance::acceptConnections() {
  while (QLocalSocket* socket = m_server->nextPendingConnection()) {
#if defined(Q_OS_LINUX)
    ucred credentials{};
    socklen_t length = sizeof(credentials);
    const int descriptor = static_cast<int>(socket->socketDescriptor());
    if (descriptor < 0 ||
        getsockopt(descriptor, SOL_SOCKET, SO_PEERCRED, &credentials,
                   &length) != 0 ||
        credentials.uid != geteuid()) {
      socket->abort();
      socket->deleteLater();
      continue;
    }
#endif
    connect(socket, &QLocalSocket::readyRead, this,
            [this, socket]() { readSocket(socket); });
    connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
    readSocket(socket);
  }
}

void SingleInstance::readSocket(QLocalSocket* socket) {
  if (socket == nullptr) {
    return;
  }
  if (socket->bytesAvailable() > kMaximumPayloadSize) {
    socket->abort();
    return;
  }

  while (socket->canReadLine()) {
    const QByteArray line = socket->readLine().trimmed();
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(line, &error);
    const QJsonObject payload =
        document.isObject() ? document.object() : QJsonObject();
    const QString requestId =
        payload.value(QStringLiteral("requestId")).toString();
    const QJsonArray argumentValues =
        payload.value(QStringLiteral("arguments")).toArray();
    const bool valid =
        line.size() <= kMaximumPayloadSize &&
        error.error == QJsonParseError::NoError && document.isObject() &&
        !requestId.isEmpty() &&
        payload.value(QStringLiteral("version")).toInt() == kProtocolVersion &&
        argumentValues.size() <= 4096;

    QStringList arguments;
    if (valid) {
      arguments.reserve(argumentValues.size());
      for (const QJsonValue& value : argumentValues) {
        if (!value.isString()) {
          arguments.clear();
          break;
        }
        arguments.append(value.toString());
      }
    }
    const QString workingDirectory =
        payload.value(QStringLiteral("workingDirectory")).toString();
    const bool accepted = valid && arguments.size() == argumentValues.size() &&
                          !workingDirectory.isEmpty();
    if (accepted) {
      emit activationRequested(arguments, workingDirectory);
    }

    QJsonObject reply;
    reply.insert(QStringLiteral("version"), kProtocolVersion);
    reply.insert(QStringLiteral("requestId"), requestId);
    reply.insert(QStringLiteral("status"), accepted
                                               ? QStringLiteral("accepted")
                                               : QStringLiteral("rejected"));
    QByteArray response = QJsonDocument(reply).toJson(QJsonDocument::Compact);
    response.append('\n');
    socket->write(response);
    socket->flush();
    socket->disconnectFromServer();
    return;
  }
}
