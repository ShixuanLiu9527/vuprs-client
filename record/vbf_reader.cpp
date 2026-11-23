#include "record/vbf_reader.h"

#include <QJsonDocument>
#include <QJsonParseError>

namespace
{
    quint16 GetU16(const QByteArray &bytes, int offset)
    {
        return static_cast<quint16>(static_cast<quint8>(bytes.at(offset))) |
               static_cast<quint16>(static_cast<quint8>(bytes.at(offset + 1)) << 8);
    }

    quint32 GetU32(const QByteArray &bytes, int offset)
    {
        quint32 value = 0;
        for (int i = 0; i < 4; i++)
            value |= static_cast<quint32>(static_cast<quint8>(bytes.at(offset + i))) << (8 * i);
        return value;
    }

    quint64 GetU64(const QByteArray &bytes, int offset)
    {
        quint64 value = 0;
        for (int i = 0; i < 8; i++)
            value |= static_cast<quint64>(static_cast<quint8>(bytes.at(offset + i))) << (8 * i);
        return value;
    }
}

vuprs::client::VbfReader::~VbfReader()
{
    this->close();
}

bool vuprs::client::VbfReader::open(const QString &path, QString *error)
{
    this->close();
    this->file_.setFileName(path);
    if (!this->file_.open(QIODevice::ReadOnly))
    {
        if (error != nullptr)
            *error = QStringLiteral("无法打开 %1: %2").arg(path, this->file_.errorString());
        return false;
    }

    const auto fail = [this, error](const QString &message)
    {
        if (error != nullptr)
            *error = message;
        this->file_.close();
        return false;
    };

    const QByteArray prefix = this->file_.read(16);
    if (prefix.size() != 16)
        return fail(QStringLiteral("文件头不完整"));
    if (prefix.left(8) != QByteArrayLiteral("VUPRSBF1"))
        return fail(QStringLiteral("不是 VUPRS .vbf 文件"));

    const quint16 version = GetU16(prefix, 8);
    if (version != 1)
        return fail(QStringLiteral("不支持的 .vbf 版本: %1").arg(version));

    const quint32 json_bytes = GetU32(prefix, 12);
    if (json_bytes == 0 || json_bytes > 4u * 1024u * 1024u)
        return fail(QStringLiteral("元数据长度异常"));

    const QByteArray json = this->file_.read(json_bytes);
    if (json.size() != static_cast<int>(json_bytes))
        return fail(QStringLiteral("元数据不完整"));

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        return fail(QStringLiteral("元数据 JSON 无效: %1").arg(parse_error.errorString()));

    this->metadata_ = document.object();
    this->path_ = path;
    this->fileSize_ = this->file_.size();
    this->statistics_ = VbfStatistics();
    this->open_ = true;
    this->ended_ = false;
    if (error != nullptr)
        error->clear();
    return true;
}

void vuprs::client::VbfReader::close()
{
    this->file_.close();
    this->metadata_ = QJsonObject();
    this->path_.clear();
    this->fileSize_ = 0;
    this->open_ = false;
    this->ended_ = false;
    this->statistics_ = VbfStatistics();
}

qint64 vuprs::client::VbfReader::position() const
{
    return this->open_ ? this->file_.pos() : 0;
}

vuprs::client::VbfReader::Result vuprs::client::VbfReader::next(BeamFrame *frame, QString *error)
{
    if (error != nullptr)
        error->clear();
    if (frame != nullptr)
        *frame = BeamFrame();
    if (!this->open_ || this->ended_)
        return Result::End;

    const auto fail = [this, error](const QString &message)
    {
        this->ended_ = true;
        if (error != nullptr)
            *error = message;
        return Result::Error;
    };
    const auto truncate = [this]()
    {
        this->statistics_.truncated = true;
        this->ended_ = true;
        return Result::End;
    };

    const QByteArray type_bytes = this->file_.read(1);
    if (type_bytes.isEmpty())
    {
        if (this->file_.error() != QFileDevice::NoError)
            return fail(this->file_.errorString());
        this->ended_ = true;
        return Result::End;
    }

    const quint8 type = static_cast<quint8>(type_bytes.at(0));
    if (type == VBF_RECORD_END)
    {
        const QByteArray rest = this->file_.read(VBF_FOOTER_SIZE - 1);
        this->ended_ = true;
        if (rest.size() != VBF_FOOTER_SIZE - 1)
        {
            this->statistics_.truncated = true;
            return Result::End;
        }
        this->statistics_.hasFooter = true;
        return Result::End;
    }

    if (type != VBF_RECORD_BF)
        return fail(QStringLiteral("未知记录类型 %1（偏移 %2）")
                        .arg(type)
                        .arg(this->file_.pos() - 1));

    const QByteArray rest = this->file_.read(VBF_RECORD_HEADER_SIZE - 1);
    if (rest.size() != VBF_RECORD_HEADER_SIZE - 1)
        return truncate();
    const QByteArray header = type_bytes + rest;

    const quint32 sample_count = GetU32(header, 4);
    const quint64 payload_bytes = GetU32(header, 12);
    if (sample_count > 16u * 1024u * 1024u ||
        payload_bytes != static_cast<quint64>(sample_count) * sizeof(qint32))
    {
        return fail(QStringLiteral("BF 记录长度无效（偏移 %1）")
                        .arg(this->file_.pos() - VBF_RECORD_HEADER_SIZE));
    }

    const QByteArray payload = this->file_.read(static_cast<qint64>(payload_bytes));
    if (static_cast<quint64>(payload.size()) != payload_bytes)
        return truncate();

    if (frame != nullptr)
    {
        frame->sequence = static_cast<quint32>(GetU64(header, 16));
        frame->timestampNs = GetU64(header, 24);
        frame->sampleRateMilliHz = GetU32(header, 32);
        frame->inferenceIdentity = static_cast<qint32>(GetU32(header, 8));
        frame->inferenceValid = (static_cast<quint8>(header.at(1)) & 0x01u) != 0;
        frame->arrivalMs = static_cast<qint64>(GetU64(header, 40));
        frame->samples.resize(static_cast<int>(sample_count));
        for (quint32 i = 0; i < sample_count; ++i)
        {
            frame->samples[static_cast<int>(i)] =
                static_cast<qint32>(GetU32(payload, static_cast<int>(i) * 4));
        }
    }

    ++this->statistics_.frameCount;
    this->statistics_.sampleCount += sample_count;
    this->statistics_.payloadBytes += payload_bytes;
    return Result::Frame;
}
