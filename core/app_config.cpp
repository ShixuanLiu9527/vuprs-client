#include "core/app_config.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>

namespace
{
    int ReadInt(const QJsonObject &object, const QString &key, int fallback)
    {
        const QJsonValue value = object.value(key);
        if (value.isDouble())
            return value.toInt(fallback);
        if (value.isString())
        {
            bool ok = false;
            const int parsed = value.toString().toInt(&ok);
            return ok ? parsed : fallback;
        }
        return fallback;
    }

    QString ReadString(const QJsonObject &object, const QString &key, const QString &fallback)
    {
        const QJsonValue value = object.value(key);
        return value.isString() ? value.toString() : fallback;
    }

    QString FindSeedConfig()
    {
        const QString app_dir = QCoreApplication::applicationDirPath();
        const QStringList candidates = {
            app_dir + QStringLiteral("/client_config.json"),
            app_dir + QStringLiteral("/../doc/client_config.json"),
            app_dir + QStringLiteral("/../client_config.json"),
        };
        for (const QString &path : candidates)
        {
            if (QFile::exists(path))
                return path;
        }
        return QString();
    }

    void ApplyJson(const QJsonObject &root, vuprs::client::AppConfig &config)
    {
        const QJsonObject server = root.value(QStringLiteral("server")).toObject();
        config.host = ReadString(server, QStringLiteral("host"), config.host);
        config.port = static_cast<quint16>(ReadInt(server, QStringLiteral("port"), config.port));
        config.connectTimeoutMs = ReadInt(server, QStringLiteral("connect_timeout_ms"), config.connectTimeoutMs);
        config.commandTimeoutMs = ReadInt(server, QStringLiteral("command_timeout_ms"), config.commandTimeoutMs);

        const QJsonObject waveform = root.value(QStringLiteral("waveform")).toObject();
        config.historySeconds = ReadInt(waveform, QStringLiteral("history_seconds"), config.historySeconds);
        config.defaultWindowSeconds = ReadInt(waveform, QStringLiteral("default_window_seconds"), config.defaultWindowSeconds);
        config.beamQueueFrames = ReadInt(waveform, QStringLiteral("queue_frames"), config.beamQueueFrames);

        const QJsonObject recording = root.value(QStringLiteral("recording")).toObject();
        config.recordingDirectory = ReadString(recording, QStringLiteral("directory"), config.recordingDirectory);
        config.recordingQueueFrames = ReadInt(recording, QStringLiteral("queue_frames"), config.recordingQueueFrames);

        const QJsonObject heatmap = root.value(QStringLiteral("heatmap")).toObject();
        config.heatmapResolution = ReadInt(heatmap, QStringLiteral("resolution"), config.heatmapResolution);
    }

    void Normalize(vuprs::client::AppConfig &config)
    {
        config.host = config.host.trimmed();
        if (config.host.isEmpty())
            config.host = vuprs::client::defaultServerHost();
        if (config.port == 0)
            config.port = vuprs::client::defaultServerPort();
        config.connectTimeoutMs = qBound(500, config.connectTimeoutMs, 30000);
        config.commandTimeoutMs = qBound(500, config.commandTimeoutMs, 30000);
        config.historySeconds = qBound(5, config.historySeconds, 300);
        config.defaultWindowSeconds = qBound(1, config.defaultWindowSeconds, config.historySeconds);
        config.beamQueueFrames = qBound(32, config.beamQueueFrames, 4096);
        config.recordingQueueFrames = qBound(32, config.recordingQueueFrames, 8192);
        config.heatmapResolution = qBound(64, config.heatmapResolution, 1024);
        config.recordingDirectory = config.recordingDirectory.trimmed();
        if (config.recordingDirectory.isEmpty())
            config.recordingDirectory = vuprs::client::defaultRecordingDirectory();
    }

    QJsonObject ConfigToJson(const vuprs::client::AppConfig &config)
    {
        QJsonObject server;
        server["host"] = config.host;
        server["port"] = static_cast<int>(config.port);
        server["connect_timeout_ms"] = config.connectTimeoutMs;
        server["command_timeout_ms"] = config.commandTimeoutMs;

        QJsonObject waveform;
        waveform["history_seconds"] = config.historySeconds;
        waveform["default_window_seconds"] = config.defaultWindowSeconds;
        waveform["queue_frames"] = config.beamQueueFrames;

        QJsonObject recording;
        recording["directory"] = config.recordingDirectory;
        recording["queue_frames"] = config.recordingQueueFrames;

        QJsonObject heatmap;
        heatmap["resolution"] = config.heatmapResolution;

        QJsonObject root;
        root["config_version"] = 1;
        root["server"] = server;
        root["waveform"] = waveform;
        root["recording"] = recording;
        root["heatmap"] = heatmap;
        return root;
    }
}

QString vuprs::client::appConfigFilePath()
{
    QString directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (directory.isEmpty())
        directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (directory.isEmpty())
        directory = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("config"));
    return QDir(directory).filePath(QStringLiteral("client_config.json"));
}

QString vuprs::client::defaultRecordingDirectory()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (base.isEmpty())
        base = QCoreApplication::applicationDirPath();
    return QDir(base).filePath(QStringLiteral("VUPRS/recordings"));
}

vuprs::client::AppConfig vuprs::client::loadAppConfig(bool *created, QString *error)
{
    return loadAppConfigFrom(appConfigFilePath(), created, error);
}

vuprs::client::AppConfig vuprs::client::loadAppConfigFrom(const QString &path,
                                                          bool *created,
                                                          QString *error)
{
    if (error != nullptr)
        error->clear();
    if (created != nullptr)
        *created = false;

    AppConfig config;
    if (QFile::exists(path))
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
        {
            if (error != nullptr)
                *error = QStringLiteral("无法读取配置文件 %1: %2").arg(path, file.errorString());
            Normalize(config);
            return config;
        }
        QJsonParseError parse_error;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        {
            if (error != nullptr)
                *error = QStringLiteral("配置文件 JSON 无效 (%1): %2").arg(path, parse_error.errorString());
            Normalize(config);
            return config;
        }
        ApplyJson(document.object(), config);
        Normalize(config);
        return config;
    }

    const QString seed_path = FindSeedConfig();
    if (!seed_path.isEmpty())
    {
        QFile seed(seed_path);
        if (seed.open(QIODevice::ReadOnly))
        {
            const QJsonDocument document = QJsonDocument::fromJson(seed.readAll());
            if (document.isObject())
                ApplyJson(document.object(), config);
        }
    }
    else
    {
        config.recordingDirectory = defaultRecordingDirectory();
    }
    Normalize(config);

    QString save_error;
    if (!saveAppConfigTo(config, path, &save_error))
    {
        if (error != nullptr)
            *error = QStringLiteral("创建配置文件失败: %1").arg(save_error);
        return config;
    }
    if (created != nullptr)
        *created = true;
    return config;
}

bool vuprs::client::saveAppConfig(const AppConfig &config, QString *error)
{
    return saveAppConfigTo(config, appConfigFilePath(), error);
}

bool vuprs::client::saveAppConfigTo(const AppConfig &config, const QString &path, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (path.trimmed().isEmpty())
    {
        if (error != nullptr)
            *error = QStringLiteral("配置路径为空");
        return false;
    }

    AppConfig normalized = config;
    Normalize(normalized);

    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath()))
    {
        if (error != nullptr)
            *error = QStringLiteral("无法创建配置目录: %1").arg(info.absolutePath());
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (error != nullptr)
            *error = QStringLiteral("无法写入配置文件 %1: %2").arg(path, file.errorString());
        return false;
    }
    QByteArray json = QJsonDocument(ConfigToJson(normalized)).toJson(QJsonDocument::Indented);
    json.append('\n');
    if (file.write(json) != json.size())
    {
        const QString message = file.errorString();
        file.cancelWriting();
        if (error != nullptr)
            *error = QStringLiteral("写入配置文件失败: %1").arg(message);
        return false;
    }
    if (!file.commit())
    {
        if (error != nullptr)
            *error = QStringLiteral("保存配置文件失败: %1").arg(file.errorString());
        return false;
    }
    return true;
}
