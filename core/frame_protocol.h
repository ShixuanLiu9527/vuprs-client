#ifndef VUPRS_CLIENT_FRAME_PROTOCOL_H
#define VUPRS_CLIENT_FRAME_PROTOCOL_H

#include <functional>
#include <QtGlobal>
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include "core/beam_frame.h"

namespace vuprs
{
    namespace client
    {
        constexpr int FRAME_HEADER_SIZE = 40;
        constexpr int DATA_HEADER_SIZE = 32;
        constexpr quint16 PROTOCOL_VERSION = 1;
        constexpr quint32 MAX_PAYLOAD_BYTES = 8U * 1024U * 1024U;

        enum class MessageType : quint16
        {
            ControlRequest = 0x0001,
            ControlResponse = 0x0002,
            BfData = 0x0003,
            ScanData = 0x0004,
            InferenceData = 0x0005
        };

        struct FrameHeader
        {
            quint16 version = PROTOCOL_VERSION;
            MessageType type = MessageType::ControlRequest;
            quint32 flags = 0;
            quint32 payloadBytes = 0;
            quint32 requestId = 0;
            quint64 sequence = 0;
            quint64 timestampNs = 0;
        };

        struct BfDataHeader
        {
            quint32 sampleRateMilliHz = 0;
            quint32 sampleCount = 0;
            quint32 sampleFormat = 1;
            quint32 reserved[5] = {0, 0, 0, 0, 0};
        };

        struct InferenceDataHeader
        {
            quint32 tensorLength = 0;
            quint32 dataFormat = 1; /* 1 = int32 Q31 */
            quint32 reserved[6] = {0, 0, 0, 0, 0, 0};
        };

        struct ScanDataHeader
        {
            quint32 pointsHalf = 0;
            quint32 dataPoints = 0;
            quint32 sampleRateMilliHz = 0;
            qint32 altMinMdeg = 0;
            qint32 minPowerMdb = 0;
            qint32 maxPowerMdb = 0;
            quint32 scanFormat = 1;
        };

        QString messageTypeName(MessageType type);
        QByteArray encodeControlRequest(quint32 requestId,
                                        const QString &cmd,
                                        const QJsonObject &params = QJsonObject());

        bool decodeFrameHeader(const QByteArray &bytes, FrameHeader *header, QString *error);
        bool parseBfFrame(const FrameHeader &header, const QByteArray &payload, BeamFrame *frame, QString *error);
        bool parseScanFrame(const FrameHeader &header, const QByteArray &payload, ScanFrame *frame, QString *error);
        bool parseInferenceFrame(const FrameHeader &header,
                                 const QByteArray &payload,
                                 InferenceFrame *frame,
                                 QString *error);
        bool parseControlResponse(const FrameHeader &header,
                                  const QByteArray &payload,
                                  ControlResponse *response,
                                  QString *error);

        class FrameParser
        {
        public:
            using FrameHandler = std::function<void(const FrameHeader &, const QByteArray &)>;
            using ErrorHandler = std::function<void(const QString &)>;

            FrameParser(FrameHandler frameHandler, ErrorHandler errorHandler);

            void append(const QByteArray &data);
            void reset();

        private:
            void fail(const QString &error);

            FrameHandler frameHandler_;
            ErrorHandler errorHandler_;
            QByteArray buffer_;
        };
    }
}

#endif
