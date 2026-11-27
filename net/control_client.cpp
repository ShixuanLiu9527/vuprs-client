#include "net/control_client.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QThread>
#include <QTimer>

vuprs::client::ControlClient::ControlClient(std::shared_ptr<FrameQueue<BeamFrame>> beamQueue,
                                             std::shared_ptr<FrameQueue<ScanFrame>> scanQueue,
                                             std::shared_ptr<FrameQueue<InferenceFrame>> inferenceQueue,
                                             int commandTimeoutMs,
                                             QObject *parent)
    : QObject(parent),
      beamQueue_(std::move(beamQueue)),
      scanQueue_(std::move(scanQueue)),
      inferenceQueue_(std::move(inferenceQueue)),
      commandTimeoutMs_(commandTimeoutMs)
{
    this->parser_ = std::make_unique<FrameParser>(
        [this](const FrameHeader &header, const QByteArray &payload)
        { this->onFrame(header, payload); },
        [this](const QString &error)
        { this->onParserError(error); });
}

vuprs::client::ControlClient::~ControlClient()
{
    if (this->socket_ != nullptr)
        this->socket_->abort();
}

void vuprs::client::ControlClient::initialize()
{
    if (this->initialized_)
        return;
    this->initialized_ = true;

    this->socket_ = new QTcpSocket(this);
    QObject::connect(this->socket_, &QTcpSocket::connected, this, &ControlClient::onConnected);
    QObject::connect(this->socket_, &QTcpSocket::disconnected, this, &ControlClient::onDisconnected);
    QObject::connect(this->socket_, &QTcpSocket::readyRead, this, &ControlClient::onReadyRead);
    QObject::connect(this->socket_, &QAbstractSocket::errorOccurred, this, &ControlClient::onSocketError);

    this->connectTimer_ = new QTimer(this);
    this->connectTimer_->setSingleShot(true);
    QObject::connect(this->connectTimer_, &QTimer::timeout, this, [this]()
                     {
                         if (this->socket_ != nullptr &&
                             this->socket_->state() != QAbstractSocket::ConnectedState)
                             this->abortWithError(QStringLiteral("连接超时"));
                     });

    this->commandTimer_ = new QTimer(this);
    this->commandTimer_->setSingleShot(true);
    QObject::connect(this->commandTimer_, &QTimer::timeout, this, [this]()
                     {
                         if (this->busy_)
                             this->finishCurrent(false, -2, QStringLiteral("命令超时"), QJsonObject());
                     });
}

void vuprs::client::ControlClient::connectToServer(const QString &host, quint16 port, int timeoutMs)
{
    if (QThread::currentThread() != this->thread())
    {
        QMetaObject::invokeMethod(this, [this, host, port, timeoutMs]()
                                  { this->doConnect(host, port, timeoutMs); }, Qt::QueuedConnection);
        return;
    }
    this->doConnect(host, port, timeoutMs);
}

void vuprs::client::ControlClient::disconnectFromServer()
{
    if (QThread::currentThread() != this->thread())
    {
        QMetaObject::invokeMethod(this, [this]()
                                  { this->doDisconnect(); }, Qt::QueuedConnection);
        return;
    }
    this->doDisconnect();
}

void vuprs::client::ControlClient::sendCommand(const QString &cmd, const QJsonObject &params)
{
    if (QThread::currentThread() != this->thread())
    {
        QMetaObject::invokeMethod(this, [this, cmd, params]()
                                  { this->doSendCommand(cmd, params); }, Qt::QueuedConnection);
        return;
    }
    this->doSendCommand(cmd, params);
}

void vuprs::client::ControlClient::setRecorder(const std::shared_ptr<RecordingWriter> &recorder)
{
    std::lock_guard<std::mutex> lock(this->recorderMutex_);
    this->recorder_ = recorder;
}

void vuprs::client::ControlClient::doShutdown()
{
    this->doDisconnect();
    if (this->connectTimer_ != nullptr)
        this->connectTimer_->stop();
    if (this->commandTimer_ != nullptr)
        this->commandTimer_->stop();
    this->commandQueue_.clear();
    this->busy_ = false;
    this->currentCommand_ = Command();
    if (this->parser_)
        this->parser_->reset();
}

void vuprs::client::ControlClient::doConnect(const QString &host, quint16 port, int timeoutMs)
{
    this->initialize();
    this->beamQueue_->clear();
    this->scanQueue_->clear();
    this->inferenceQueue_->clear();
    this->parser_->reset();
    this->commandQueue_.clear();
    this->busy_ = false;
    this->currentCommand_ = Command();
    if (this->connectTimer_ != nullptr)
        this->connectTimer_->stop();
    if (this->commandTimer_ != nullptr)
        this->commandTimer_->stop();

    if (this->socket_->state() != QAbstractSocket::UnconnectedState)
        this->socket_->abort();
    if (host.trimmed().isEmpty())
    {
        emit this->connectionChanged(false, QStringLiteral("服务器地址为空"));
        return;
    }

    this->socket_->connectToHost(host.trimmed(), port);
    if (this->connectTimer_ != nullptr)
        this->connectTimer_->start(timeoutMs <= 0 ? 3000 : timeoutMs);
    emit this->connectionChanged(false, QStringLiteral("连接中..."));
}

void vuprs::client::ControlClient::doDisconnect()
{
    if (this->socket_ != nullptr && this->socket_->state() != QAbstractSocket::UnconnectedState)
    {
        this->socket_->abort();
    }
    if (this->connectTimer_ != nullptr)
        this->connectTimer_->stop();
    if (this->commandTimer_ != nullptr)
        this->commandTimer_->stop();
    this->commandQueue_.clear();
    this->busy_ = false;
    this->currentCommand_ = Command();
    if (this->parser_)
        this->parser_->reset();
    if (!this->connected_.load())
        emit this->connectionChanged(false, QStringLiteral("未连接"));
}

void vuprs::client::ControlClient::doSendCommand(const QString &cmd, const QJsonObject &params)
{
    this->initialize();
    quint32 request_id = this->nextRequestId_++;
    if (request_id == 0)
        request_id = this->nextRequestId_++;
    if (!this->connected_.load())
    {
        emit this->commandFinished(request_id, cmd, false, -1, QStringLiteral("未连接服务器"), QJsonObject());
        return;
    }
    Command command;
    command.requestId = request_id;
    command.cmd = cmd;
    command.params = params;
    this->commandQueue_.enqueue(command);
    this->sendNext();
}

void vuprs::client::ControlClient::onConnected()
{
    this->connected_ = true;
    if (this->connectTimer_ != nullptr)
        this->connectTimer_->stop();
    if (this->socket_ != nullptr)
        this->socket_->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    emit this->connectionChanged(true, QStringLiteral("已连接"));
    this->sendNext();
}

void vuprs::client::ControlClient::onDisconnected()
{
    this->connected_ = false;
    if (this->connectTimer_ != nullptr)
        this->connectTimer_->stop();
    if (this->commandTimer_ != nullptr)
        this->commandTimer_->stop();
    if (this->busy_)
        this->finishCurrent(false, -3, QStringLiteral("连接已断开"), QJsonObject());
    this->commandQueue_.clear();
    this->currentCommand_ = Command();
    this->busy_ = false;
    if (this->parser_)
        this->parser_->reset();
    emit this->connectionChanged(false, QStringLiteral("连接已断开"));
}

void vuprs::client::ControlClient::onReadyRead()
{
    if (this->socket_ == nullptr)
        return;
    const QByteArray data = this->socket_->readAll();
    if (data.isEmpty())
        return;
    this->receivedBytes_ += static_cast<quint64>(data.size());
    this->parser_->append(data);
}

void vuprs::client::ControlClient::onSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    if (this->socket_ == nullptr)
        return;
    const QString detail = QStringLiteral("网络错误: ") + this->socket_->errorString();
    if (this->connectTimer_ != nullptr)
        this->connectTimer_->stop();
    if (!this->connected_.load())
        emit this->connectionChanged(false, detail);
    else
        qWarning() << "[client]" << detail;
}

void vuprs::client::ControlClient::onFrame(const FrameHeader &header, const QByteArray &payload)
{
    QString error;
    if (header.type == MessageType::ControlResponse)
    {
        ControlResponse response;
        if (!parseControlResponse(header, payload, &response, &error))
        {
            this->abortWithError(error);
            return;
        }
        if (!this->busy_ || response.requestId != this->currentCommand_.requestId)
        {
            qWarning() << "[client] unexpected control response id" << response.requestId;
            return;
        }
        if (this->commandTimer_ != nullptr)
            this->commandTimer_->stop();
        this->busy_ = false;
        const Command command = this->currentCommand_;
        this->currentCommand_ = Command();
        emit this->commandFinished(command.requestId, response.cmd.isEmpty() ? command.cmd : response.cmd,
                                   response.ok, response.error, response.info, response.params);
        this->sendNext();
    }
    else if (header.type == MessageType::BfData)
    {
        BeamFrame frame;
        if (!parseBfFrame(header, payload, &frame, &error))
        {
            this->abortWithError(error);
            return;
        }
        this->beamQueue_->push(frame);
        std::shared_ptr<RecordingWriter> recorder;
        {
            std::lock_guard<std::mutex> lock(this->recorderMutex_);
            recorder = this->recorder_;
        }
        if (recorder && recorder->isActive())
            recorder->enqueue(frame);
    }
    else if (header.type == MessageType::ScanData)
    {
        ScanFrame frame;
        if (!parseScanFrame(header, payload, &frame, &error))
        {
            this->abortWithError(error);
            return;
        }
        this->scanQueue_->push(frame);
    }
    else if (header.type == MessageType::InferenceData)
    {
        InferenceFrame frame;
        if (!parseInferenceFrame(header, payload, &frame, &error))
        {
            this->abortWithError(error);
            return;
        }
        this->inferenceQueue_->push(frame);
    }
    else
    {
        this->abortWithError(QStringLiteral("收到非法消息类型"));
    }
}

void vuprs::client::ControlClient::onParserError(const QString &error)
{
    this->abortWithError(error);
}

void vuprs::client::ControlClient::sendNext()
{
    if (this->busy_ || !this->connected_.load() || this->commandQueue_.isEmpty() || this->socket_ == nullptr)
        return;
    this->currentCommand_ = this->commandQueue_.dequeue();
    this->busy_ = true;
    const QByteArray frame = encodeControlRequest(this->currentCommand_.requestId,
                                                   this->currentCommand_.cmd,
                                                   this->currentCommand_.params);
    const qint64 written = this->socket_->write(frame);
    if (written != frame.size())
    {
        this->abortWithError(QStringLiteral("命令发送失败"));
        return;
    }
    this->sentBytes_ += static_cast<quint64>(frame.size());
    if (this->commandTimer_ != nullptr)
        this->commandTimer_->start(this->commandTimeoutMs_ <= 0 ? 5000 : this->commandTimeoutMs_);
}

void vuprs::client::ControlClient::finishCurrent(bool ok, int error, const QString &info, const QJsonObject &params)
{
    if (!this->busy_)
        return;
    if (this->commandTimer_ != nullptr)
        this->commandTimer_->stop();
    this->busy_ = false;
    const Command command = this->currentCommand_;
    this->currentCommand_ = Command();
    emit this->commandFinished(command.requestId, command.cmd, ok, error, info, params);
    this->sendNext();
}

void vuprs::client::ControlClient::abortWithError(const QString &error)
{
    this->connected_ = false;
    if (this->connectTimer_ != nullptr)
        this->connectTimer_->stop();
    if (this->commandTimer_ != nullptr)
        this->commandTimer_->stop();
    if (this->busy_)
        this->finishCurrent(false, -4, error, QJsonObject());
    this->commandQueue_.clear();
    this->currentCommand_ = Command();
    this->busy_ = false;
    if (this->parser_)
        this->parser_->reset();
    if (this->socket_ != nullptr)
        this->socket_->abort();
    emit this->protocolError(error);
    emit this->connectionChanged(false, error);
}
