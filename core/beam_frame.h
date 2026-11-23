#ifndef VUPRS_CLIENT_BEAM_FRAME_H
#define VUPRS_CLIENT_BEAM_FRAME_H

#include <QtGlobal>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace vuprs
{
    namespace client
    {
        struct BeamFrame
        {
            quint32 sequence = 0;
            quint64 timestampNs = 0;
            quint32 sampleRateMilliHz = 0;
            qint32 inferenceIdentity = -1;
            bool inferenceValid = false;
            QVector<qint32> samples; /* raw Q16.16 */
            qint64 arrivalMs = 0;

            double sampleRateHz() const { return static_cast<double>(this->sampleRateMilliHz) / 1000.0; }
            int sampleCount() const { return this->samples.size(); }
        };

        struct ScanFrame
        {
            quint32 sequence = 0;
            quint64 timestampNs = 0;
            quint32 pointsHalf = 0;
            quint32 dataPoints = 0;
            quint32 sampleRateMilliHz = 0;
            double altMinDeg = 0.0;
            double minPowerDb = 0.0;
            double maxPowerDb = 0.0;
            QVector<quint16> values;
            qint64 arrivalMs = 0;

            double sampleRateHz() const { return static_cast<double>(this->sampleRateMilliHz) / 1000.0; }
        };

        struct ControlResponse
        {
            quint32 requestId = 0;
            QString cmd;
            bool ok = false;
            int error = 0;
            QString info;
            QJsonObject params;
        };
    }
}

Q_DECLARE_METATYPE(vuprs::client::BeamFrame)
Q_DECLARE_METATYPE(vuprs::client::ScanFrame)

#endif
