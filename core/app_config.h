#ifndef VUPRS_CLIENT_APP_CONFIG_H
#define VUPRS_CLIENT_APP_CONFIG_H

#include <QtGlobal>
#include <QString>

namespace vuprs
{
    namespace client
    {
        /** Direct-link DHCP mode: the board keeps this address and serves DHCP. */
        inline QString defaultServerHost() { return QStringLiteral("192.168.50.10"); }
        inline quint16 defaultServerPort() { return 8080; }

        struct AppConfig
        {
            QString host = defaultServerHost();
            quint16 port = defaultServerPort();
            int connectTimeoutMs = 3000;
            int commandTimeoutMs = 5000;
            int historySeconds = 60;
            int defaultWindowSeconds = 10;
            int beamQueueFrames = 256;
            QString recordingDirectory = QStringLiteral("recordings");
            int recordingQueueFrames = 512;
            int heatmapResolution = 256;
        };

        /** Per-user config path, e.g. %LOCALAPPDATA%/VUPRS/VUPRS Client/client_config.json. */
        QString appConfigFilePath();
        QString defaultRecordingDirectory();

        /** Loads the per-user config, creating it from the bundled template on first run. */
        AppConfig loadAppConfig(bool *created = nullptr, QString *error = nullptr);
        AppConfig loadAppConfigFrom(const QString &path, bool *created = nullptr, QString *error = nullptr);

        bool saveAppConfig(const AppConfig &config, QString *error = nullptr);
        bool saveAppConfigTo(const AppConfig &config, const QString &path, QString *error = nullptr);
    }
}

#endif
