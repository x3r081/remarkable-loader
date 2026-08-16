#include "penmouse.h"
#include "penreader.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>

/* Written to the choice file, read by mode-toggle.sh after we exit. */
class Chooser : public QObject
{
    Q_OBJECT
public:
    explicit Chooser(QString path, QObject *parent = nullptr)
        : QObject(parent), m_path(std::move(path)) {}

    Q_INVOKABLE void choose(const QString &command)
    {
        QFile f(m_path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(command.toUtf8());
            f.close();
        }
        QCoreApplication::quit();
    }

private:
    QString m_path;
};

static QVariantList loadApps(const QString &configPath)
{
    QVariantList apps;
    QFile f(configPath);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("launcher: cannot read %s", qPrintable(configPath));
        return apps;
    }
    const auto doc = QJsonDocument::fromJson(f.readAll());
    for (const auto &v : doc.array()) {
        const auto o = v.toObject();
        if (o.contains("name") && o.contains("exec"))
            apps.append(QVariantMap{
                {"name", o["name"].toString()},
                {"description", o["description"].toString()},
                {"exec", o["exec"].toString()},
            });
    }
    return apps;
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("rm_launcher");

    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption configOption("config", "App list (JSON).", "path",
                                    "/home/root/apps/apps.json");
    QCommandLineOption choiceOption("choice-file", "Where to write the choice.",
                                    "path", "/tmp/launcher-choice");
    QCommandLineOption timeoutOption("timeout",
                                     "Auto-return to tablet after this many seconds.",
                                     "seconds", "120");
    QCommandLineOption screenshotOption("screenshot",
                                        "Render one frame to <file> and exit.", "file");
    parser.addOption(configOption);
    parser.addOption(choiceOption);
    parser.addOption(timeoutOption);
    parser.addOption(screenshotOption);
    parser.process(app);

    bool ok = false;
    int timeout = parser.value(timeoutOption).toInt(&ok);
    if (!ok || timeout < 5)
        timeout = 120;

    Chooser chooser(parser.value(choiceOption));

    // Pen taps work like finger taps everywhere.
    PenReader pen;
    PenMouse penMouse;
    QObject::connect(&pen, &PenReader::sample, &penMouse, &PenMouse::sample);
    pen.start();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("chooser", &chooser);
    engine.rootContext()->setContextProperty("cfgApps",
                                             loadApps(parser.value(configOption)));
    engine.rootContext()->setContextProperty("cfgTimeoutSeconds", timeout);
    engine.rootContext()->setContextProperty("cfgOffscreen",
                                             app.platformName() == "offscreen");
    engine.rootContext()->setContextProperty("cfgScreenshot",
                                             parser.value(screenshotOption));

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("rm_launcher_module", "Main");
    return app.exec();
}

#include "main.moc"
