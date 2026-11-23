#ifndef VUPRS_CLIENT_RECORDING_WRITER_H
#define VUPRS_CLIENT_RECORDING_WRITER_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <QJsonObject>
#include <QQueue>
#include <QString>
#include "core/beam_frame.h"

namespace vuprs
{
    namespace client
    {
        /**
         * @brief Writes BF frames to a self-describing .vbf file from a worker thread.
         */
        class RecordingWriter
        {
        public:
            using ErrorCallback = std::function<void(const QString &)>;

            RecordingWriter();
            ~RecordingWriter();

            RecordingWriter(const RecordingWriter &) = delete;
            RecordingWriter &operator=(const RecordingWriter &) = delete;

            void setErrorCallback(ErrorCallback callback);

            bool start(const QString &path, const QJsonObject &metadata, int queueFrames = 512);
            void enqueue(const BeamFrame &frame);
            void stop();

            bool isActive() const;
            QString lastError() const;
            quint64 framesWritten() const;
            quint64 payloadBytes() const;
            quint64 droppedFrames() const;
            qint64 startedMs() const;

        private:
            void loop(const QString &partPath, const QString &finalPath, QJsonObject metadata);
            void setError(const QString &error);

            mutable std::mutex mutex_;
            std::condition_variable condition_;
            std::thread thread_;
            QQueue<BeamFrame> queue_;
            int queueCapacity_ = 512;
            bool active_ = false;
            bool stopRequested_ = false;
            bool running_ = false;
            QString lastError_;
            quint64 framesWritten_ = 0;
            quint64 payloadBytes_ = 0;
            quint64 droppedFrames_ = 0;
            qint64 startedMs_ = 0;
            ErrorCallback errorCallback_;
        };
    }
}

#endif
