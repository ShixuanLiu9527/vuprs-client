#include <QApplication>
#include <algorithm>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QPixmap>
#include <QTimer>
#include "ui/main_window.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("VUPRS"));
    QCoreApplication::setApplicationName(QStringLiteral("VUPRS Client"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setStyle(QStringLiteral("Fusion"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("VUPRS 上位机"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption host_option(QStringLiteral("host"), QStringLiteral("服务器地址"), QStringLiteral("host"));
    const QCommandLineOption port_option(QStringLiteral("port"), QStringLiteral("服务器端口"), QStringLiteral("port"));
    const QCommandLineOption connect_option(QStringLiteral("connect"), QStringLiteral("启动后自动连接"));
    const QCommandLineOption playback_option(QStringLiteral("playback"), QStringLiteral("启动后打开 .vbf 回放"), QStringLiteral("file"));
    const QCommandLineOption screenshot_option(QStringLiteral("screenshot"), QStringLiteral("截图到文件后退出"), QStringLiteral("file"));
    const QCommandLineOption screenshot_delay_option(QStringLiteral("screenshot-delay"), QStringLiteral("截图前等待毫秒"), QStringLiteral("ms"), QStringLiteral("1500"));
    const QCommandLineOption tab_option(QStringLiteral("tab"), QStringLiteral("截图时标签页: 0=波形 1=半球 2=日志"), QStringLiteral("index"), QStringLiteral("0"));
    parser.addOption(host_option);
    parser.addOption(port_option);
    parser.addOption(connect_option);
    parser.addOption(playback_option);
    parser.addOption(screenshot_option);
    parser.addOption(screenshot_delay_option);
    parser.addOption(tab_option);
    parser.process(app);

    vuprs::client::MainWindow window;
    window.show();
    if (parser.isSet(connect_option))
    {
        const QString host = parser.value(host_option);
        bool port_ok = false;
        const int port = parser.value(port_option).toInt(&port_ok);
        QTimer::singleShot(0, &window, [&window, host, port, port_ok]()
                           {
                               window.connectToServer(host.isEmpty() ? QStringLiteral("127.0.0.1") : host,
                                                      port_ok ? static_cast<quint16>(port) : 18080);
                           });
    }
    if (parser.isSet(playback_option))
    {
        const QString path = parser.value(playback_option);
        QTimer::singleShot(0, &window, [&window, path]()
                           { window.openPlayback(path); });
    }
    if (parser.isSet(tab_option))
    {
        bool tab_ok = false;
        const int tab = parser.value(tab_option).toInt(&tab_ok);
        window.setTabIndex(tab_ok ? tab : 0);
    }
    if (parser.isSet(screenshot_option))
    {
        bool delay_ok = false;
        const int delay = parser.value(screenshot_delay_option).toInt(&delay_ok);
        const QString path = parser.value(screenshot_option);
        QTimer::singleShot(delay_ok ? std::max(100, delay) : 1500, &window, [&window, &app, path]()
                           {
                               window.grab().save(path);
                               app.quit();
                           });
    }
    return app.exec();
}
