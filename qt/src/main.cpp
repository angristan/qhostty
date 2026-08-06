#include "GhosttyApp.h"
#include "MainWindow.h"

#include <QApplication>
#include <QColorSpace>
#include <QOpenGLContext>
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
  QCoreApplication::setApplicationName(QStringLiteral("Qhostty"));
  QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
  QCoreApplication::setOrganizationName(QStringLiteral("angristan"));
  QApplication::setDesktopFileName(
      QStringLiteral("io.github.angristan.qhostty"));

  GhosttyApp ghostty;
  if (!ghostty.initialize()) {
    return EXIT_FAILURE;
  }

  QObject::connect(&application, &QGuiApplication::applicationStateChanged,
                   &ghostty, [&ghostty](Qt::ApplicationState state) {
                     ghostty_app_set_focus(ghostty.handle(),
                                           state == Qt::ApplicationActive);
                   });

  MainWindow window(&ghostty);
  window.show();

  return application.exec();
}
