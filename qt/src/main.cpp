#include "GhosttyApp.h"
#include "SingleInstance.h"

#include <QApplication>
#include <QColorSpace>
#include <QDebug>
#include <QDir>
#include <QIcon>
#include <QOpenGLContext>
#include <QStandardPaths>
#include <QSurfaceFormat>

#include <cstdlib>

#include <ghostty.h>

int main(int argc, char** argv) {
  if (ghostty_init(static_cast<uintptr_t>(argc), argv) != 0) {
    return EXIT_FAILURE;
  }
  ghostty_cli_try_action();

  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

  QSurfaceFormat format;
  format.setRenderableType(QSurfaceFormat::OpenGL);
  format.setVersion(4, 3);
  format.setProfile(QSurfaceFormat::CoreProfile);
  format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
  format.setColorSpace(QColorSpace::SRgb);
  QSurfaceFormat::setDefaultFormat(format);

  QApplication application(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("qhostty"));
  QGuiApplication::setApplicationDisplayName(QStringLiteral("Qhostty"));
  QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
  QCoreApplication::setOrganizationName(QStringLiteral("angristan"));
  QCoreApplication::setOrganizationDomain(
      QStringLiteral("angristan.github.io"));
  QApplication::setDesktopFileName(
      QStringLiteral("io.github.angristan.qhostty"));
  QApplication::setWindowIcon(
      QIcon::fromTheme(QStringLiteral("io.github.angristan.qhostty")));

  QString runtimeDirectory =
      QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
  if (runtimeDirectory.isEmpty()) {
    runtimeDirectory = QDir::tempPath();
  }
  SingleInstance instance(
      QDir(runtimeDirectory)
          .filePath(QStringLiteral("io.github.angristan.qhostty.v1")));
  const SingleInstance::StartResult instanceResult =
      instance.start(application.arguments(), QDir::currentPath());
  if (instanceResult == SingleInstance::StartResult::Forwarded) {
    return EXIT_SUCCESS;
  }
  if (instanceResult == SingleInstance::StartResult::Failed) {
    qCritical().noquote() << "Unable to start Qhostty:"
                          << instance.errorString();
    return EXIT_FAILURE;
  }

  GhosttyApp ghostty;
  if (!ghostty.initialize()) {
    return EXIT_FAILURE;
  }

  QObject::connect(&instance, &SingleInstance::activationRequested, &ghostty,
                   &GhosttyApp::activate);
  ghostty.createWindow();

  return application.exec();
}
