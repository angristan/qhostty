#pragma once

#include <QObject>
#include <QStringList>

class QLocalServer;
class QLocalSocket;
class QLockFile;

class SingleInstance final : public QObject {
  Q_OBJECT

 public:
  enum class StartResult {
    Primary,
    Forwarded,
    Failed,
  };
  Q_ENUM(StartResult)

  explicit SingleInstance(QString serverName, QObject* parent = nullptr);
  ~SingleInstance() override;

  StartResult start(const QStringList& arguments,
                    const QString& workingDirectory,
                    int timeoutMs = 1000);
  [[nodiscard]] bool isPrimary() const { return m_primary; }
  [[nodiscard]] QString errorString() const { return m_error; }

 signals:
  void activationRequested(const QStringList& arguments,
                           const QString& workingDirectory);

 private:
  enum class ForwardResult {
    NoServer,
    Accepted,
    Failed,
  };

  bool listen();
  ForwardResult forward(const QStringList& arguments,
                        const QString& workingDirectory,
                        int timeoutMs);
  void acceptConnections();
  void readSocket(QLocalSocket* socket);

  QString m_serverName;
  QLocalServer* m_server;
  QLockFile* m_lock;
  bool m_primary = false;
  QString m_error;
};
