#include "record/recording_writer.h"

#include <chrono>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include "record/vbf_format.h"

vuprs::client::RecordingWriter::RecordingWriter() = default;

vuprs::client::RecordingWriter::~RecordingWriter()
{
    this->stop();
}

void vuprs::client::RecordingWriter::setErrorCallback(ErrorCallback callback)
{
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->errorCallback_ = std::move(callback);
}

bool vuprs::client::RecordingWriter::start(const QString &path, const QJsonObject &metadata, int queueFrames)
{
    if (path.trimmed().isEmpty())
    {
        this->setError(QStringLiteral("录制路径为空"));
        return false;
    }
    if (this->thread_.joinable())
        this->thread_.join();

    QString finalPath = path;
    if (!finalPath.endsWith(QStringLiteral(".vbf"), Qt::CaseInsensitive))
        finalPath += QStringLiteral(".vbf");
    const QFileInfo info(finalPath);
    if (!QDir().mkpath(info.absolutePath()))
    {
        this->setError(QStringLiteral("无法创建录制目录: ") + info.absolutePath());
        return false;
    }
    if (QFile::exists(finalPath))
    {
        this->setError(QStringLiteral("录制文件已存在: ") + finalPath);
        return false;
    }
    const QString partPath = finalPath + QStringLiteral(".part");

    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        if (this->running_ || this->active_)
            return false;
        this->queue_.clear();
        this->queueCapacity_ = queueFrames <= 0 ? 1 : queueFrames;
        this->active_ = true;
        this->stopRequested_ = false;
        this->running_ = true;
        this->lastError_.clear();
        this->framesWritten_ = 0;
        this->payloadBytes_ = 0;
        this->droppedFrames_ = 0;
        this->startedMs_ = QDateTime::currentMSecsSinceEpoch();
    }

    try
    {
        this->thread_ = std::thread([this, partPath, finalPath, metadata]()
                                    { this->loop(partPath, finalPath, metadata); });
    }
    catch (const std::exception &e)
    {
        {
            std::lock_guard<std::mutex> lock(this->mutex_);
            this->active_ = false;
            this->running_ = false;
        }
        this->setError(QStringLiteral("无法启动录制线程: ") + QString::fromUtf8(e.what()));
        return false;
    }
    return true;
}

void vuprs::client::RecordingWriter::enqueue(const BeamFrame &frame)
{
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (!this->active_)
        return;
    while (this->queue_.size() >= this->queueCapacity_)
    {
        this->queue_.dequeue();
        ++this->droppedFrames_;
    }
    this->queue_.enqueue(frame);
    this->condition_.notify_one();
}

void vuprs::client::RecordingWriter::stop()
{
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        if (!this->running_ && !this->thread_.joinable())
            return;
        this->active_ = false;
        this->stopRequested_ = true;
        this->condition_.notify_all();
    }
    if (this->thread_.joinable())
        this->thread_.join();
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->active_ = false;
    this->running_ = false;
}

bool vuprs::client::RecordingWriter::isActive() const
{
    std::lock_guard<std::mutex> lock(this->mutex_);
    return this->active_;
}

QString vuprs::client::RecordingWriter::lastError() const
{
    std::lock_guard<std::mutex> lock(this->mutex_);
    return this->lastError_;
}

quint64 vuprs::client::RecordingWriter::framesWritten() const
{
    std::lock_guard<std::mutex> lock(this->mutex_);
    return this->framesWritten_;
}

quint64 vuprs::client::RecordingWriter::payloadBytes() const
{
    std::lock_guard<std::mutex> lock(this->mutex_);
    return this->payloadBytes_;
}

quint64 vuprs::client::RecordingWriter::droppedFrames() const
{
    std::lock_guard<std::mutex> lock(this->mutex_);
    return this->droppedFrames_;
}

qint64 vuprs::client::RecordingWriter::startedMs() const
{
    std::lock_guard<std::mutex> lock(this->mutex_);
    return this->startedMs_;
}

void vuprs::client::RecordingWriter::setError(const QString &error)
{
    ErrorCallback callback;
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        if (this->lastError_.isEmpty())
            this->lastError_ = error;
        callback = this->errorCallback_;
    }
    if (callback)
        callback(error);
}

void vuprs::client::RecordingWriter::loop(const QString &partPath, const QString &finalPath, QJsonObject metadata)
{
    QFile file(partPath);
    bool ok = file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    if (!ok)
    {
        this->setError(QStringLiteral("无法打开录制文件: ") + partPath);
    }

    quint64 lastTimestampNs = 0;
    if (ok)
    {
        const QByteArray header = makeVbfFileHeader(metadata);
        if (file.write(header) != header.size())
        {
            this->setError(QStringLiteral("写入录制文件头失败"));
            ok = false;
        }
    }

    while (ok)
    {
        BeamFrame frame;
        bool has_frame = false;
        {
            std::unique_lock<std::mutex> lock(this->mutex_);
            this->condition_.wait(lock, [this]()
                                  { return !this->active_ || !this->queue_.isEmpty(); });
            if (!this->active_ && this->queue_.isEmpty())
                break;
            if (!this->queue_.isEmpty())
            {
                frame = this->queue_.dequeue();
                has_frame = true;
            }
        }
        if (!has_frame)
            continue;

        const QByteArray record = makeVbfRecord(frame);
        if (file.write(record) != record.size())
        {
            this->setError(QStringLiteral("写入录制帧失败"));
            ok = false;
            break;
        }
        lastTimestampNs = frame.timestampNs;
        {
            std::lock_guard<std::mutex> lock(this->mutex_);
            ++this->framesWritten_;
            this->payloadBytes_ += static_cast<quint64>(frame.samples.size() * sizeof(qint32));
        }
    }

    if (ok)
    {
        quint64 frames = 0;
        quint64 bytes = 0;
        {
            std::lock_guard<std::mutex> lock(this->mutex_);
            frames = this->framesWritten_;
            bytes = this->payloadBytes_;
        }
        if (lastTimestampNs == 0)
            lastTimestampNs = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000000ULL;
        const QByteArray footer = makeVbfFooter(frames, bytes, lastTimestampNs);
        if (file.write(footer) != footer.size())
        {
            this->setError(QStringLiteral("写入录制文件尾失败"));
            ok = false;
        }
    }

    if (file.isOpen())
    {
        file.flush();
        file.close();
    }
    if (ok)
    {
        QFile::remove(finalPath);
        if (!QFile::rename(partPath, finalPath))
            this->setError(QStringLiteral("无法完成录制文件: ") + finalPath);
    }

    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        this->active_ = false;
        this->running_ = false;
    }
    this->condition_.notify_all();
}
