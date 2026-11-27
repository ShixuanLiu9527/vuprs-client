#ifndef VUPRS_CLIENT_CONTROL_CLIENT_H
#define VUPRS_CLIENT_CONTROL_CLIENT_H

#include <atomic>
#include <memory>
#include <mutex>
#include <QJsonObject>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QTcpSocket>
#include "core/beam_frame.h"
#include "core/frame_protocol.h"
#include "core/frame_queue.h"
#include "record/recording_writer.h"

class QTimer;

namespace vuprs
{
    namespace client
    {
        class ControlClient : public QObject
        {
            Q_OBJECT

        public:
            ControlClient(std::shared_ptr<FrameQueue<BeamFrame>> beamQueue,
                          std::shared_ptr<FrameQueue<ScanFrame>> scanQueue,
                          std::shared_ptr<FrameQueue<InferenceFrame>> inferenceQueue,
                          int commandTimeoutMs = 5000,
                          QObject *parent = nullptr);
            ~ControlClient() override;

            ControlClient(const ControlClient &) = delete;
            ControlClient &operator=(const ControlClient &) = delete;

            void connectToServer(const QString &host, quint16 port, int timeoutMs);
            void disconnectFromServer();
            void sendCommand(const QString &cmd, const QJsonObject &params = QJsonObject());
            void setRecorder(const std::shared_ptr<RecordingWriter> &recorder);

            std::shared_ptr<FrameQueue<BeamFrame>> beamQueue() const { return this->beamQueue_; }
            std::shared_ptr<FrameQueue<ScanFrame>> scanQueue() const { return this->scanQueue_; }
            std::shared_ptr<FrameQueue<InferenceFrame>> inferenceQueue() const { return this->inferenceQueue_; }
            quint64 receivedBytes() const { return this->receivedBytes_.load(); }
            quint64 sentBytes() const { return this->sentBytes_.load(); }
            bool isConnected() const { return this->connected_.load(); }

        signals:
            void connectionChanged(bool connected, const QString &detail);
            void commandFinished(quint32 requestId,
                                 const QString &cmd,
                                 bool ok,
                                 int error,
                                 const QString &info,
                                 const QJsonObject &params);
            void protocolError(const QString &detail);

        public slots:
            void initialize();
            void doShutdown();

        private:
            struct Command
            {
                quint32 requestId = 0;
                QString cmd;
                QJsonObject params;
            };

            void doConnect(const QString &host, quint16 port, int timeoutMs);
            void doDisconnect();
            void doSendCommand(const QString &cmd, const QJsonObject &params);
            void onConnected();
            void onDisconnected();
            void onReadyRead();
            void onSocketError(QAbstractSocket::SocketError error);
            void onFrame(const FrameHeader &header, const QByteArray &payload);
            void onParserError(const QString &error);
            void sendNext();
            void finishCurrent(bool ok, int error, const QString &info, const QJsonObject &params);
            void abortWithError(const QString &error);

            std::shared_ptr<FrameQueue<BeamFrame>> beamQueue_;
            std::shared_ptr<FrameQueue<ScanFrame>> scanQueue_;
            std::shared_ptr<FrameQueue<InferenceFrame>> inferenceQueue_;
            std::unique_ptr<FrameParser> parser_;

            QTcpSocket *socket_ = nullptr;
            QTimer *connectTimer_ = nullptr;
            QTimer *commandTimer_ = nullptr;
            int commandTimeoutMs_ = 5000;
            bool initialized_ = false;
            std::atomic<bool> connected_{false};

            QQueue<Command> commandQueue_;
            Command currentCommand_;
            bool busy_ = false;
            quint32 nextRequestId_ = 1;

            std::mutex recorderMutex_;
            std::shared_ptr<RecordingWriter> recorder_;

            std::atomic<quint64> receivedBytes_{0};
            std::atomic<quint64> sentBytes_{0};
        };
    }
}

#endif
