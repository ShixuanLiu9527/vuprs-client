#include "core/waveform_buffer.h"

#include <algorithm>
#include <cmath>
#include <limits>

bool vuprs::client::WaveformBuffer::configureSamples(double sampleRateHz, qint64 capacitySamples)
{
    if (!std::isfinite(sampleRateHz) || sampleRateHz <= 0.0 || capacitySamples <= 0 ||
        capacitySamples > static_cast<qint64>(std::numeric_limits<int>::max()))
        return false;
    const int capacity = static_cast<int>(std::max<qint64>(1024, capacitySamples));
    if (qFuzzyCompare(this->sampleRateHz_, sampleRateHz) && this->data_.size() == capacity)
        return true;
    this->data_.resize(capacity);
    this->head_ = 0;
    this->count_ = 0;
    this->totalSamples_ = 0;
    this->sampleRateHz_ = sampleRateHz;
    return true;
}

void vuprs::client::WaveformBuffer::configure(double sampleRateHz, int historySeconds)
{
    if (!std::isfinite(sampleRateHz) || sampleRateHz <= 0.0)
        return;
    const int seconds = std::max(1, historySeconds);
    const qint64 capacity = std::max<qint64>(1024, static_cast<qint64>(std::ceil(sampleRateHz * seconds)));
    if (qFuzzyCompare(this->sampleRateHz_, sampleRateHz) &&
        static_cast<qint64>(this->data_.size()) == capacity &&
        this->historySeconds_ == seconds)
        return;

    this->data_.resize(static_cast<int>(capacity));
    this->head_ = 0;
    this->count_ = 0;
    this->totalSamples_ = 0;
    this->sampleRateHz_ = sampleRateHz;
    this->historySeconds_ = seconds;
}

void vuprs::client::WaveformBuffer::clear()
{
    this->head_ = 0;
    this->count_ = 0;
    this->totalSamples_ = 0;
}

bool vuprs::client::WaveformBuffer::append(const BeamFrame &frame)
{
    if (frame.samples.isEmpty())
        return false;
    const double rate = frame.sampleRateHz();
    if (rate > 0.0 &&
        (!std::isfinite(this->sampleRateHz_) ||
         std::abs(rate - this->sampleRateHz_) > 1e-6 ||
         this->data_.isEmpty()))
        this->configure(rate, this->historySeconds_);
    if (this->data_.isEmpty())
        return false;

    const int capacity = this->data_.size();
    for (const qint32 raw : frame.samples)
    {
        const float value = static_cast<float>(raw) / 65536.0f;
        if (this->count_ < capacity)
        {
            this->data_[(this->head_ + this->count_) % capacity] = value;
            ++this->count_;
        }
        else
        {
            this->data_[this->head_] = value;
            this->head_ = (this->head_ + 1) % capacity;
        }
        ++this->totalSamples_;
    }
    return true;
}

bool vuprs::client::WaveformBuffer::sampleAt(qint64 absoluteIndex, float *value) const
{
    if (value == nullptr || this->count_ <= 0)
        return false;
    const qint64 first = this->firstSampleIndex();
    if (absoluteIndex < first || absoluteIndex >= this->lastSampleIndex())
        return false;
    const qint64 offset = absoluteIndex - first;
    const int capacity = this->data_.size();
    *value = this->data_[static_cast<int>((this->head_ + offset) % capacity)];
    return true;
}

bool vuprs::client::WaveformBuffer::minMax(qint64 begin, qint64 end, float *minimum, float *maximum) const
{
    if (minimum == nullptr || maximum == nullptr || this->count_ <= 0)
        return false;
    const qint64 first = this->firstSampleIndex();
    const qint64 last = this->lastSampleIndex();
    begin = std::max(begin, first);
    end = std::min(end, last);
    if (begin >= end)
        return false;
    float min_value = 0.0f;
    float max_value = 0.0f;
    bool initialized = false;
    for (qint64 index = begin; index < end; ++index)
    {
        float value = 0.0f;
        if (!this->sampleAt(index, &value))
            continue;
        if (!initialized)
        {
            min_value = max_value = value;
            initialized = true;
        }
        else
        {
            min_value = std::min(min_value, value);
            max_value = std::max(max_value, value);
        }
    }
    if (!initialized)
        return false;
    *minimum = min_value;
    *maximum = max_value;
    return true;
}
