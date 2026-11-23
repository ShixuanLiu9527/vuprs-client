#include "core/frame_protocol.h"

#include <cstring>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonParseError>

namespace
{
    const QByteArray FRAME_MAGIC("VUPR", 4);

    void PutU16(QByteArray &bytes, int offset, quint16 value)
    {
        bytes[offset] = static_cast<char>(value & 0xFF);
        bytes[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
    }

    void PutU32(QByteArray &bytes, int offset, quint32 value)
    {
        bytes[offset] = static_cast<char>(value & 0xFF);
        bytes[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
        bytes[offset + 2] = static_cast<char>((value >> 16) & 0xFF);
        bytes[offset + 3] = static_cast<char>((value >> 24) & 0xFF);
    }

    void PutU64(QByteArray &bytes, int offset, quint64 value)
    {
        for (int i = 0; i < 8; i++)
            bytes[offset + i] = static_cast<char>((value >> (8 * i)) & 0xFF);
    }

    quint16 GetU16(const QByteArray &bytes, int offset)
    {
        return static_cast<quint16>(static_cast<quint8>(bytes[offset])) |
               static_cast<quint16>(static_cast<quint16>(static_cast<quint8>(bytes[offset + 1])) << 8);
    }

    quint32 GetU32(const QByteArray &bytes, int offset)
    {
        return static_cast<quint32>(static_cast<quint8>(bytes[offset])) |
               (static_cast<quint32>(static_cast<quint8>(bytes[offset + 1])) << 8) |
               (static_cast<quint32>(static_cast<quint8>(bytes[offset + 2])) << 16) |
               (static_cast<quint32>(static_cast<quint8>(bytes[offset + 3])) << 24);
    }

    quint64 GetU64(const QByteArray &bytes, int offset)
    {
        quint64 value = 0;
        for (int i = 7; i >= 0; i--)
            value = (value << 8) | static_cast<quint8>(bytes[offset + i]);
        return value;
    }

    bool IsKnownType(vuprs::client::MessageType type)
    {
        switch (type)
        {
        case vuprs::client::MessageType::ControlRequest:
        case vuprs::client::MessageType::ControlResponse:
        case vuprs::client::MessageType::BfData:
        case vuprs::client::MessageType::ScanData:
            return true;
        default:
            return false;
        }
    }

    void SetError(QString *error, const QString &message)
    {
        if (error != nullptr)
            *error = message;
    }
}

QString vuprs::client::messageTypeName(MessageType type)
{
    switch (type)
    {
    case MessageType::ControlRequest:
        return "control_request";
    case MessageType::ControlResponse:
        return "control_response";
    case MessageType::BfData:
        return "bf_data";
    case MessageType::ScanData:
        return "scan_data";
    default:
        return "unknown";
    }
}

QByteArray vuprs::client::encodeControlRequest(quint32 requestId, const QString &cmd, const QJsonObject &params)
{
    QJsonObject root;
    root["cmd"] = cmd;
    root["params"] = params;
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
    QByteArray frame(FRAME_HEADER_SIZE + payload.size(), Qt::Uninitialized);
    std::memcpy(frame.data(), FRAME_MAGIC.constData(), 4);
    PutU16(frame, 4, PROTOCOL_VERSION);
    PutU16(frame, 6, static_cast<quint16>(MessageType::ControlRequest));
    PutU32(frame, 8, 0);
    PutU32(frame, 12, static_cast<quint32>(payload.size()));
    PutU32(frame, 16, requestId);
    PutU32(frame, 20, 0);
    PutU64(frame, 24, 0);
    PutU64(frame, 32, 0);
    std::memcpy(frame.data() + FRAME_HEADER_SIZE, payload.constData(), static_cast<size_t>(payload.size()));
    return frame;
}

bool vuprs::client::decodeFrameHeader(const QByteArray &bytes, FrameHeader *header, QString *error)
{
    if (header == nullptr || bytes.size() < FRAME_HEADER_SIZE)
    {
        SetError(error, QStringLiteral("帧头长度不足"));
        return false;
    }
    if (std::memcmp(bytes.constData(), FRAME_MAGIC.constData(), 4) != 0)
    {
        SetError(error, QStringLiteral("帧 magic 错误"));
        return false;
    }
    header->version = GetU16(bytes, 4);
    header->type = static_cast<MessageType>(GetU16(bytes, 6));
    header->flags = GetU32(bytes, 8);
    header->payloadBytes = GetU32(bytes, 12);
    header->requestId = GetU32(bytes, 16);
    header->sequence = GetU64(bytes, 24);
    header->timestampNs = GetU64(bytes, 32);
    if (header->version != PROTOCOL_VERSION)
    {
        SetError(error, QStringLiteral("协议版本不匹配"));
        return false;
    }
    if (!IsKnownType(header->type))
    {
        SetError(error, QStringLiteral("未知消息类型"));
        return false;
    }
    if (header->payloadBytes > MAX_PAYLOAD_BYTES)
    {
        SetError(error, QStringLiteral("payload 超过最大长度"));
        return false;
    }
    return true;
}

bool vuprs::client::parseBfFrame(const FrameHeader &header,
                                 const QByteArray &payload,
                                 BeamFrame *frame,
                                 QString *error)
{
    if (frame == nullptr || header.type != MessageType::BfData)
    {
        SetError(error, QStringLiteral("不是 BF 帧"));
        return false;
    }
    if (payload.size() < DATA_HEADER_SIZE)
    {
        SetError(error, QStringLiteral("BF 数据头长度不足"));
        return false;
    }
    BfDataHeader data;
    data.sampleRateMilliHz = GetU32(payload, 0);
    data.sampleCount = GetU32(payload, 4);
    data.sampleFormat = GetU32(payload, 8);
    data.inferenceIdentity = static_cast<qint32>(GetU32(payload, 12));
    data.inferenceValid = GetU32(payload, 16);
    if (data.sampleFormat != 1)
    {
        SetError(error, QStringLiteral("不支持的 BF 量化格式"));
        return false;
    }
    const quint64 expected = static_cast<quint64>(DATA_HEADER_SIZE) + static_cast<quint64>(data.sampleCount) * 4ULL;
    if (expected != static_cast<quint64>(payload.size()))
    {
        SetError(error, QStringLiteral("BF 采样点数与 payload 不匹配"));
        return false;
    }
    frame->sequence = header.sequence;
    frame->timestampNs = header.timestampNs;
    frame->sampleRateMilliHz = data.sampleRateMilliHz;
    frame->inferenceIdentity = data.inferenceIdentity;
    frame->inferenceValid = data.inferenceValid != 0;
    frame->samples.resize(static_cast<int>(data.sampleCount));
    for (quint32 i = 0; i < data.sampleCount; i++)
        frame->samples[static_cast<int>(i)] = static_cast<qint32>(GetU32(payload, DATA_HEADER_SIZE + static_cast<int>(i) * 4));
    frame->arrivalMs = QDateTime::currentMSecsSinceEpoch();
    return true;
}

bool vuprs::client::parseScanFrame(const FrameHeader &header,
                                   const QByteArray &payload,
                                   ScanFrame *frame,
                                   QString *error)
{
    if (frame == nullptr || header.type != MessageType::ScanData)
    {
        SetError(error, QStringLiteral("不是扫描帧"));
        return false;
    }
    if (payload.size() < DATA_HEADER_SIZE)
    {
        SetError(error, QStringLiteral("扫描数据头长度不足"));
        return false;
    }
    ScanDataHeader data;
    data.pointsHalf = GetU32(payload, 0);
    data.dataPoints = GetU32(payload, 4);
    data.sampleRateMilliHz = GetU32(payload, 8);
    data.altMinMdeg = static_cast<qint32>(GetU32(payload, 12));
    data.minPowerMdb = static_cast<qint32>(GetU32(payload, 16));
    data.maxPowerMdb = static_cast<qint32>(GetU32(payload, 20));
    data.scanFormat = GetU32(payload, 24);
    if (data.scanFormat != 1)
    {
        SetError(error, QStringLiteral("不支持的扫描格式"));
        return false;
    }
    const quint64 expected = static_cast<quint64>(DATA_HEADER_SIZE) + static_cast<quint64>(data.dataPoints) * 2ULL;
    if (expected != static_cast<quint64>(payload.size()))
    {
        SetError(error, QStringLiteral("扫描点数与 payload 不匹配"));
        return false;
    }
    frame->sequence = header.sequence;
    frame->timestampNs = header.timestampNs;
    frame->pointsHalf = data.pointsHalf;
    frame->dataPoints = data.dataPoints;
    frame->sampleRateMilliHz = data.sampleRateMilliHz;
    frame->altMinDeg = static_cast<double>(data.altMinMdeg) / 1000.0;
    frame->minPowerDb = static_cast<double>(data.minPowerMdb) / 1000.0;
    frame->maxPowerDb = static_cast<double>(data.maxPowerMdb) / 1000.0;
    frame->values.resize(static_cast<int>(data.dataPoints));
    for (quint32 i = 0; i < data.dataPoints; i++)
        frame->values[static_cast<int>(i)] = GetU16(payload, DATA_HEADER_SIZE + static_cast<int>(i) * 2);
    frame->arrivalMs = QDateTime::currentMSecsSinceEpoch();
    return true;
}

bool vuprs::client::parseControlResponse(const FrameHeader &header,
                                         const QByteArray &payload,
                                         ControlResponse *response,
                                         QString *error)
{
    if (response == nullptr || header.type != MessageType::ControlResponse)
    {
        SetError(error, QStringLiteral("不是控制响应帧"));
        return false;
    }
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        SetError(error, QStringLiteral("控制响应 JSON 解析失败"));
        return false;
    }
    const QJsonObject object = document.object();
    response->requestId = header.requestId;
    response->cmd = object.value("cmd").toString();
    response->ok = object.value("ok").toBool(false);
    response->error = object.value("error").toInt(6);
    response->info = object.value("info").toString();
    response->params = object.value("params").toObject();
    if (!object.contains("ok"))
    {
        SetError(error, QStringLiteral("控制响应缺少 ok 字段"));
        return false;
    }
    return true;
}

vuprs::client::FrameParser::FrameParser(FrameHandler frameHandler, ErrorHandler errorHandler)
    : frameHandler_(std::move(frameHandler)), errorHandler_(std::move(errorHandler))
{
}

void vuprs::client::FrameParser::append(const QByteArray &data)
{
    if (data.isEmpty())
        return;
    this->buffer_.append(data);
    while (true)
    {
        if (this->buffer_.size() < FRAME_HEADER_SIZE)
            return;
        FrameHeader header;
        QString error;
        if (!decodeFrameHeader(this->buffer_.left(FRAME_HEADER_SIZE), &header, &error))
        {
            this->fail(error);
            return;
        }
        const int total = FRAME_HEADER_SIZE + static_cast<int>(header.payloadBytes);
        if (this->buffer_.size() < total)
            return;
        const QByteArray payload = this->buffer_.mid(FRAME_HEADER_SIZE, static_cast<int>(header.payloadBytes));
        this->buffer_.remove(0, total);
        if (this->frameHandler_)
            this->frameHandler_(header, payload);
    }
}

void vuprs::client::FrameParser::reset()
{
    this->buffer_.clear();
}

void vuprs::client::FrameParser::fail(const QString &error)
{
    this->buffer_.clear();
    if (this->errorHandler_)
        this->errorHandler_(error);
}
