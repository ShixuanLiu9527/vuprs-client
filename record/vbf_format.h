#ifndef VUPRS_CLIENT_VBF_FORMAT_H
#define VUPRS_CLIENT_VBF_FORMAT_H

#include <QtGlobal>
#include <QByteArray>
#include <QJsonObject>
#include "core/beam_frame.h"

namespace vuprs
{
    namespace client
    {
        constexpr int VBF_RECORD_HEADER_SIZE = 48;
        constexpr int VBF_FOOTER_SIZE = 32;
        constexpr quint8 VBF_RECORD_BF = 1;
        constexpr quint8 VBF_RECORD_END = 0xFF;

        QByteArray makeVbfFileHeader(const QJsonObject &metadata);
        QByteArray makeVbfRecord(const BeamFrame &frame);
        QByteArray makeVbfFooter(quint64 frameCount, quint64 payloadBytes, quint64 endTimestampNs);
    }
}

#endif
