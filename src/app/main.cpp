#include "core/capture/Capture.h"
#include "core/settings/VaultSettings.h"
#include "ui/Appearance.h"
#include "ui/CaptureWindow.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QTimer>

#include <cstdio>

using namespace Qt::StringLiterals;

int main(int argc, char* argv[])
{
    QElapsedTimer startup;
    startup.start();
    // The UI is Qt Widgets with raster painting and never uses OpenGL. Skipping the
    // EGL/GLX integrations avoids loading Mesa and LLVM (about 55 MB of RSS).
    // Users can still override either variable. See docs/decisions.md.
    if (!qEnvironmentVariableIsSet("QT_WAYLAND_CLIENT_BUFFER_INTEGRATION"))
        qputenv("QT_WAYLAND_CLIENT_BUFFER_INTEGRATION", "none");
    if (!qEnvironmentVariableIsSet("QT_XCB_GL_INTEGRATION")) qputenv("QT_XCB_GL_INTEGRATION", "none");
#ifdef Q_OS_LINUX
    // Prefer Wayland, fall back to X11 (xcb) when there's no Wayland compositor.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "wayland;xcb");
#endif
    QApplication app(argc, argv);
    QApplication::setApplicationName(u"markdown-notes"_s);
    QApplication::setApplicationDisplayName(u"Markdown Notes"_s);
    QApplication::setOrganizationName(u"markdown-notes"_s);
    QApplication::setApplicationVersion(u"0.1.0"_s);
    QApplication::setDesktopFileName(u"markdown-notes"_s);

    QCommandLineParser cli;
    cli.setApplicationDescription(u"A lightweight editor for Obsidian vaults."_s);
    cli.addHelpOption();
    cli.addVersionOption();
    QCommandLineOption benchStartup(u"bench-startup"_s,
                                    u"Print the time until the window is usable, then exit (benchmarks)."_s);
    QCommandLineOption benchIdle(u"bench-idle"_s, u"Exit after <ms> milliseconds (memory benchmarks)."_s,
                                 u"ms"_s);
    QCommandLineOption capture(u"capture"_s, u"Open only the quick-capture box: the text is appended to the "
                                             "vault's inbox (or today's daily note), then the app exits."_s);
    cli.addOption(capture);
    cli.addOption(benchStartup);
    cli.addOption(benchIdle);
    cli.addPositionalArgument(u"vault"_s, u"Vault folder to open."_s, u"[vault]"_s);
    cli.addPositionalArgument(u"note"_s, u"Note to open (vault-relative)."_s, u"[note]"_s);
    cli.process(app);

    QString vault = cli.positionalArguments().value(0);
    if (vault.isEmpty()) vault = QSettings().value(u"lastVault"_s).toString();

    if (cli.isSet(capture)) {
        // Fast path: no main window, no index; just the box and one atomic append.
        if (vault.isEmpty() || !QFileInfo(vault).isDir()) {
            QMessageBox::warning(nullptr, u"Quick capture"_s,
                                 u"No vault yet: open one in Markdown Notes first, or pass its folder."_s);
            return 1;
        }
        const mn::ui::Appearance look = mn::ui::Appearance::load();
        if (!(look == mn::ui::Appearance{})) mn::ui::applyAppearance(look);
        const QString root = QFileInfo(vault).absoluteFilePath();
        const mn::VaultSettings settings = mn::loadVaultSettings(root).settings;
        mn::ui::CaptureWindow box(mn::captureTargetPath(settings, QDateTime::currentDateTime()));
        QObject::connect(&box, &mn::ui::CaptureWindow::submitted, &box, [&](const QString& text) {
            const mn::CaptureResult r = mn::appendCapture(root, settings, text);
            if (r.ok)
                box.accept();
            else
                box.showError(r.error);
        });
        box.show();
        box.raise();
        box.activateWindow();
        if (cli.isSet(benchStartup)) {
            QTimer::singleShot(0, &app, [&] {
                std::printf("startup_ms %lld\n", static_cast<long long>(startup.elapsed()));
                std::fflush(stdout);
                box.reject();
            });
        }
        return box.exec() == QDialog::Accepted ? 0 : 1;
    }

    mn::ui::MainWindow window;
    window.resize(1280, 820);
    window.show();

    if (!vault.isEmpty() && QFileInfo(vault).isDir()) {
        QString err;
        window.openVault(vault, &err);
        const QString note = cli.positionalArguments().value(1);
        if (!note.isEmpty()) window.openNote(note);
    } else {
        QTimer::singleShot(0, &window, [&window] { window.promptForVault(); });
    }

    if (cli.isSet(benchStartup)) {
        // "Usable": the window is shown and the event loop has processed the first paint.
        QTimer::singleShot(0, &app, [&] {
            std::printf("startup_ms %lld\n", static_cast<long long>(startup.elapsed()));
            std::fflush(stdout);
            if (!cli.isSet(benchIdle)) QApplication::quit();
        });
    }
    if (cli.isSet(benchIdle)) {
        QTimer::singleShot(cli.value(benchIdle).toInt(), &app, &QApplication::quit);
    }
    return QApplication::exec();
}
