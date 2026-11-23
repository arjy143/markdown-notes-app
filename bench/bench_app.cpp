// Launch time and idle memory of the real app (Linux reads /proc for RSS).
#include "BenchUtil.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>

using namespace Qt::StringLiterals;

namespace {

double rssMb(qint64 pid)
{
    QFile f(u"/proc/%1/status"_s.arg(pid));
    if (!f.open(QIODevice::ReadOnly)) return -1;
    for (const QByteArray& line : f.readAll().split('\n')) {
        if (line.startsWith("VmRSS:")) return line.mid(6).trimmed().split(' ').first().toDouble() / 1024.0;
    }
    return -1;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({u"vault"_s, u"Vault to open."_s, u"path"_s});
    cli.addOption({u"note"_s, u"Note to open in the editor."_s, u"path"_s, u"Typical note.md"_s});
    cli.addOption({u"runs"_s, u"Launches to time."_s, u"n"_s, u"5"_s});
    cli.addOption({u"platform"_s, u"QT_QPA_PLATFORM for the app (default: inherit)."_s, u"name"_s});
    cli.process(app);
    const QString vault = cli.value(u"vault"_s);
    if (vault.isEmpty()) {
        std::fprintf(stderr, "usage: bench_app --vault <path>\n");
        return 2;
    }
    // Private config and cache, so the user's settings and caches are never touched.
    QTemporaryDir home;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(u"XDG_CONFIG_HOME"_s, home.path() + u"/config"_s);
    env.insert(u"XDG_CACHE_HOME"_s, home.path() + u"/cache"_s);
    if (cli.isSet(u"platform"_s)) env.insert(u"QT_QPA_PLATFORM"_s, cli.value(u"platform"_s));
    const QStringList baseArgs{vault, cli.value(u"note"_s)};

    // Warm-up run builds the index cache.
    {
        QProcess p;
        p.setProcessEnvironment(env);
        p.start(QStringLiteral(MN_APP_PATH), baseArgs + QStringList{u"--bench-idle"_s, u"6000"_s});
        p.waitForFinished(60000);
    }
    std::vector<double> starts;
    for (int i = 0; i < cli.value(u"runs"_s).toInt(); ++i) {
        QProcess p;
        p.setProcessEnvironment(env);
        p.start(QStringLiteral(MN_APP_PATH), baseArgs + QStringList{u"--bench-startup"_s});
        p.waitForFinished(30000);
        for (const QByteArray& line : p.readAllStandardOutput().split('\n')) {
            if (line.startsWith("startup_ms")) starts.push_back(line.mid(10).trimmed().toDouble());
        }
    }
    mn::bench::report("launch to usable window (median)", mn::bench::median(starts), "ms", 300, 1000);

    QProcess p;
    p.setProcessEnvironment(env);
    p.start(QStringLiteral(MN_APP_PATH), baseArgs + QStringList{u"--bench-idle"_s, u"8000"_s});
    p.waitForStarted();
    QThread::msleep(6000);
    const double rss = rssMb(p.processId());
    p.waitForFinished(30000);
    if (rss >= 0)
        mn::bench::report("idle memory (RSS), one note open", rss, "MB", 60, 100);
    else
        std::printf("idle memory: not measured on this platform\n");
    return 0;
}
