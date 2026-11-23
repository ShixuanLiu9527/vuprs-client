#ifndef VUPRS_CLIENT_WAVEFORM_BUFFER_H
#define VUPRS_CLIENT_WAVEFORM_BUFFER_H

#include <QtGlobal>
#include <QVector>
#include "core/beam_frame.h"

namespace vuprs
{
    namespace client
    {
        /**
         * @brief Ring buffer of normalized BF samples for continuous drawing.
         */
        class WaveformBuffer
        {
        public:
            WaveformBuffer() = default;

            void configure(double sampleRateHz, int historySeconds);
            bool configureSamples(double sampleRateHz, qint64 capacitySamples);
            void clear();

            bool append(const BeamFrame &frame);

            double sampleRateHz() const { return this->sampleRateHz_; }
            int historySeconds() const { return this->historySeconds_; }
            qint64 capacity() const { return this->data_.size(); }
            qint64 count() const { return this->count_; }
            qint64 totalSamples() const { return this->totalSamples_; }
            qint64 firstSampleIndex() const { return this->totalSamples_ - this->count_; }
            qint64 lastSampleIndex() const { return this->totalSamples_; }

            bool sampleAt(qint64 absoluteIndex, float *value) const;
            bool minMax(qint64 begin, qint64 end, float *minimum, float *maximum) const;

        private:
            QVector<float> data_;
            int head_ = 0;
            int count_ = 0;
            qint64 totalSamples_ = 0;
            double sampleRateHz_ = 0.0;
            int historySeconds_ = 60;
        };
    }
}

#endif
