#ifndef VUPRS_CLIENT_APP_CONFIG_H
#define VUPRS_CLIENT_APP_CONFIG_H

#include <QtGlobal>
#include <QString>

namespace vuprs
{
    namespace client
    {
        struct AppConfig
        {
            QString host = QStringLiteral("192.168.1.100");
            quint16 port = 8080;
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
