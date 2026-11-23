#include "record/vbf_format.h"

#include <cstring>
#include <QJsonDocument>

namespace
{
    const QByteArray VBF_MAGIC("VUPRSBF1", 8);

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
}

QByteArray vuprs::client::makeVbfFileHeader(const QJsonObject &metadata)
{
    QJsonObject header = metadata;
    header["format"] = "VUPRS-BF";
    header["version"] = 1;
    header["sample_format"] = "int32_q16.16";
    const QByteArray json = QJsonDocument(header).toJson(QJsonDocument::Compact);
    QByteArray bytes(8 + 2 + 2 + 4 + json.size(), Qt::Uninitialized);
    std::memcpy(bytes.data(), VBF_MAGIC.constData(), 8);
    PutU16(bytes, 8, 1);
    PutU16(bytes, 10, 0);
    PutU32(bytes, 12, static_cast<quint32>(json.size()));
    std::memcpy(bytes.data() + 16, json.constData(), static_cast<size_t>(json.size()));
    return bytes;
}

QByteArray vuprs::client::makeVbfRecord(const BeamFrame &frame)
{
    const quint32 payload_bytes = static_cast<quint32>(frame.samples.size() * sizeof(qint32));
    QByteArray bytes(VBF_RECORD_HEADER_SIZE + static_cast<int>(payload_bytes), Qt::Uninitialized);
    bytes[0] = static_cast<char>(VBF_RECORD_BF);
    bytes[1] = frame.inferenceValid ? 1 : 0;
    PutU16(bytes, 2, 0);
    PutU32(bytes, 4, static_cast<quint32>(frame.samples.size()));
    PutU32(bytes, 8, static_cast<quint32>(frame.inferenceIdentity));
    PutU32(bytes, 12, payload_bytes);
    PutU64(bytes, 16, frame.sequence);
    PutU64(bytes, 24, frame.timestampNs);
    PutU32(bytes, 32, frame.sampleRateMilliHz);
    PutU32(bytes, 36, 0);
    PutU64(bytes, 40, static_cast<quint64>(frame.arrivalMs));
    char *sample_data = bytes.data() + VBF_RECORD_HEADER_SIZE;
    for (int i = 0; i < frame.samples.size(); i++)
    {
        const quint32 value = static_cast<quint32>(frame.samples[i]);
        sample_data[i * 4 + 0] = static_cast<char>(value & 0xFF);
        sample_data[i * 4 + 1] = static_cast<char>((value >> 8) & 0xFF);
        sample_data[i * 4 + 2] = static_cast<char>((value >> 16) & 0xFF);
        sample_data[i * 4 + 3] = static_cast<char>((value >> 24) & 0xFF);
    }
    return bytes;
}

QByteArray vuprs::client::makeVbfFooter(quint64 frameCount, quint64 payloadBytes, quint64 endTimestampNs)
{
    QByteArray bytes(VBF_FOOTER_SIZE, Qt::Uninitialized);
    bytes[0] = static_cast<char>(VBF_RECORD_END);
    for (int i = 1; i < 8; i++)
        bytes[i] = 0;
    PutU64(bytes, 8, frameCount);
    PutU64(bytes, 16, payloadBytes);
    PutU64(bytes, 24, endTimestampNs);
    return bytes;
}
