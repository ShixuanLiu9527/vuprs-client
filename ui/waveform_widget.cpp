#include "ui/waveform_widget.h"

#include <algorithm>
#include <cmath>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygon>
#include <QWheelEvent>

namespace
{
    const QColor kBackground(16, 24, 32);
    const QColor kPlotBackground(12, 20, 28);
    const QColor kGrid(42, 58, 74);
    const QColor kAxis(170, 190, 210);
    const QColor kWaveform(60, 210, 140);
    const QColor kCursor(255, 185, 60);
    const QColor kPlayhead(80, 180, 255);
    const QColor kOverlay(0, 0, 0, 150);
}

vuprs::client::WaveformWidget::WaveformWidget(QWidget *parent)
    : QWidget(parent)
{
    this->setMouseTracking(true);
    this->setMinimumSize(480, 260);
    this->setAutoFillBackground(false);
    this->setFocusPolicy(Qt::StrongFocus);
}

void vuprs::client::WaveformWidget::setBuffer(WaveformBuffer *buffer)
{
    this->buffer_ = buffer;
    this->viewStart_ = 0;
    this->viewSpan_ = 0;
    this->viewSampleRate_ = 0.0;
    this->playheadVisible_ = false;
    this->selecting_ = false;
    this->update();
}

void vuprs::client::WaveformWidget::setDefaultWindowSeconds(double seconds)
{
    this->defaultWindowSeconds_ = std::max(0.1, seconds);
}

void vuprs::client::WaveformWidget::setViewWindowSeconds(double seconds)
{
    if (seconds <= 0.0)
        return;
    this->defaultWindowSeconds_ = seconds;
    const double fs = this->buffer_ != nullptr ? this->buffer_->sampleRateHz() : 0.0;
    if (fs > 0.0)
    {
        this->viewSpan_ = std::max<qint64>(16, static_cast<qint64>(seconds * fs));
        this->viewSampleRate_ = fs;
        this->ensureView();
    }
    this->update();
}

void vuprs::client::WaveformWidget::setAutoFollow(bool enabled)
{
    this->setAutoFollowInternal(enabled);
    this->update();
}

void vuprs::client::WaveformWidget::setAutoFollowInternal(bool enabled)
{
    if (this->autoFollow_ == enabled)
        return;
    this->autoFollow_ = enabled;
    emit this->autoFollowChanged(enabled);
}

void vuprs::client::WaveformWidget::setFrozen(bool frozen)
{
    this->frozen_ = frozen;
    this->update();
}

void vuprs::client::WaveformWidget::resetView()
{
    const double fs = this->buffer_ != nullptr ? this->buffer_->sampleRateHz() : 0.0;
    if (fs > 0.0)
    {
        this->viewSpan_ = std::max<qint64>(16, static_cast<qint64>(this->defaultWindowSeconds_ * fs));
        this->viewSampleRate_ = fs;
    }
    this->setAutoFollowInternal(true);
    this->yAuto_ = true;
    this->ensureView();
    this->update();
}

void vuprs::client::WaveformWidget::setViewRange(qint64 startSample, qint64 spanSamples)
{
    if (spanSamples <= 0)
        spanSamples = this->currentSpanSamples();
    this->viewSpan_ = std::max<qint64>(16, spanSamples);
    this->viewStart_ = startSample;
    if (this->buffer_ != nullptr)
        this->viewSampleRate_ = this->buffer_->sampleRateHz();
    this->setAutoFollowInternal(false);
    this->ensureView();
    this->update();
}

qint64 vuprs::client::WaveformWidget::viewSpanSamples() const
{
    return this->viewSpan_ > 0 ? this->viewSpan_ : this->currentSpanSamples();
}

void vuprs::client::WaveformWidget::setPlayheadSample(qint64 sample, bool visible)
{
    this->playheadSample_ = sample;
    this->playheadVisible_ = visible;
    this->update();
}

void vuprs::client::WaveformWidget::setSelectionEnabled(bool enabled)
{
    this->selectionEnabled_ = enabled;
    if (!enabled)
        this->selecting_ = false;
    this->setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
    this->update();
}

QRect vuprs::client::WaveformWidget::plotRect() const
{
    return QRect(96, 28, std::max(50, this->width() - 116), std::max(50, this->height() - 62));
}

qint64 vuprs::client::WaveformWidget::currentSpanSamples() const
{
    if (this->buffer_ != nullptr && this->buffer_->sampleRateHz() > 0.0)
        return std::max<qint64>(16, static_cast<qint64>(this->defaultWindowSeconds_ * this->buffer_->sampleRateHz()));
    return 1024;
}

void vuprs::client::WaveformWidget::ensureView()
{
    if (this->buffer_ == nullptr)
        return;
    const double fs = this->buffer_->sampleRateHz();
    if (fs > 0.0 && std::abs(fs - this->viewSampleRate_) > 1e-6)
    {
        this->viewSpan_ = std::max<qint64>(16, static_cast<qint64>(this->defaultWindowSeconds_ * fs));
        this->viewSampleRate_ = fs;
    }
    if (this->viewSpan_ <= 0)
        this->viewSpan_ = this->currentSpanSamples();

    const qint64 first = this->buffer_->firstSampleIndex();
    const qint64 last = this->buffer_->lastSampleIndex();
    if (last <= first)
    {
        this->viewStart_ = 0;
        return;
    }
    if (this->autoFollow_ && !this->frozen_)
    {
        this->viewStart_ = std::max(first, last - this->viewSpan_);
        if (last - first < this->viewSpan_)
            this->viewStart_ = first;
    }
    else
    {
        const qint64 min_start = first - this->viewSpan_ / 2;
        const qint64 max_start = last;
        this->viewStart_ = std::max(min_start, std::min(this->viewStart_, max_start));
    }
}

void vuprs::client::WaveformWidget::zoomX(double factor, int anchorX)
{
    if (this->buffer_ == nullptr || factor <= 0.0)
        return;
    this->ensureView();
    const QRect plot = this->plotRect();
    const qint64 anchor_sample = this->xToSample(std::max(plot.left(), std::min(anchorX, plot.right())));
    const double relative = this->viewSpan_ > 0
                                ? static_cast<double>(anchor_sample - this->viewStart_) / static_cast<double>(this->viewSpan_)
                                : 0.5;
    const qint64 maximum = std::max<qint64>(this->viewSpan_, static_cast<qint64>(this->buffer_->capacity()));
    qint64 new_span = static_cast<qint64>(static_cast<double>(this->viewSpan_) * factor);
    new_span = std::max<qint64>(16, std::min(new_span, std::max<qint64>(maximum, 16)));
    const qint64 new_start = anchor_sample - static_cast<qint64>(relative * static_cast<double>(new_span));
    this->viewSpan_ = new_span;
    this->viewStart_ = new_start;
    this->setAutoFollowInternal(false);
    this->ensureView();
    this->update();
}

void vuprs::client::WaveformWidget::zoomY(double factor, int anchorY)
{
    if (this->buffer_ == nullptr)
        return;
    this->ensureView();
    const QRect plot = this->plotRect();
    float minimum = -1.0f;
    float maximum = 1.0f;
    if (!this->buffer_->minMax(this->viewStart_, this->viewStart_ + this->viewSpan_, &minimum, &maximum))
        return;
    const double range = std::max(1e-6, static_cast<double>(maximum - minimum));
    const double anchor_ratio = plot.height() > 0
                                    ? static_cast<double>(plot.bottom() - std::max(plot.top(), std::min(anchorY, plot.bottom()))) / static_cast<double>(plot.height())
                                    : 0.5;
    const double anchor_value = minimum + anchor_ratio * range;
    const double new_range = std::max(1e-6, range * factor);
    this->yManualMin_ = static_cast<float>(anchor_value - anchor_ratio * new_range);
    this->yManualMax_ = static_cast<float>(this->yManualMin_ + new_range);
    this->yAuto_ = false;
    this->update();
}

qint64 vuprs::client::WaveformWidget::xToSample(int x) const
{
    const QRect plot = this->plotRect();
    if (plot.width() <= 0)
        return this->viewStart_;
    const double ratio = std::max(0.0, std::min(1.0, static_cast<double>(x - plot.left()) / static_cast<double>(plot.width())));
    return this->viewStart_ + static_cast<qint64>(ratio * static_cast<double>(this->viewSpan_));
}

int vuprs::client::WaveformWidget::sampleToX(qint64 sample) const
{
    const QRect plot = this->plotRect();
    if (this->viewSpan_ <= 0)
        return plot.left();
    const double ratio = static_cast<double>(sample - this->viewStart_) / static_cast<double>(this->viewSpan_);
    return plot.left() + static_cast<int>(ratio * plot.width());
}

void vuprs::client::WaveformWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(this->rect(), kBackground);
    const QRect plot = this->plotRect();
    painter.fillRect(plot, kPlotBackground);

    if (this->buffer_ == nullptr || this->buffer_->count() <= 0)
    {
        this->drawNoData(painter);
        return;
    }
    this->ensureView();

    const qint64 first = std::max(this->viewStart_, this->buffer_->firstSampleIndex());
    const qint64 last = std::min(this->viewStart_ + this->viewSpan_, this->buffer_->lastSampleIndex());
    if (last <= first)
    {
        this->drawNoData(painter);
        return;
    }

    float minimum = -1.0f;
    float maximum = 1.0f;
    if (this->yAuto_)
    {
        if (!this->buffer_->minMax(first, last, &minimum, &maximum))
        {
            minimum = -1.0f;
            maximum = 1.0f;
        }
        const float range = maximum - minimum;
        const float margin = std::max(0.01f, range * 0.1f);
        minimum -= margin;
        maximum += margin;
        if (maximum - minimum < 0.02f)
        {
            const float center = (minimum + maximum) * 0.5f;
            minimum = center - 0.01f;
            maximum = center + 0.01f;
        }
    }
    else
    {
        minimum = this->yManualMin_;
        maximum = this->yManualMax_;
        if (maximum <= minimum)
            maximum = minimum + 1.0f;
    }

    const double fs = this->buffer_->sampleRateHz();
    painter.setPen(QPen(kGrid, 1));
    painter.setRenderHint(QPainter::Antialiasing, false);
    for (int i = 0; i <= 5; i++)
    {
        const int x = plot.left() + plot.width() * i / 5;
        painter.drawLine(x, plot.top(), x, plot.bottom());
        if (fs > 0.0)
        {
            const double seconds = (this->viewStart_ + this->viewSpan_ * i / 5.0) / fs;
            painter.setPen(kAxis);
            painter.drawText(QRect(x - 40, plot.bottom() + 4, 80, 18), Qt::AlignHCenter | Qt::AlignTop,
                             QString::number(seconds, 'f', seconds >= 100.0 ? 0 : (seconds >= 10.0 ? 1 : 2)));
            painter.setPen(QPen(kGrid, 1));
        }
    }
    for (int i = 0; i <= 4; i++)
    {
        const int y = plot.top() + plot.height() * i / 4;
        painter.drawLine(plot.left(), y, plot.right(), y);
        const double value = maximum - (maximum - minimum) * i / 4.0;
        painter.setPen(kAxis);
        painter.drawText(QRect(0, y - 9, plot.left() - 8, 18), Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(value, 'f', 1));
        painter.setPen(QPen(kGrid, 1));
    }

    painter.setClipRect(plot);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(kWaveform, 1.2));
    const qint64 visible = last - first;
    if (visible > plot.width())
    {
        for (int x = plot.left(); x <= plot.right(); x++)
        {
            const qint64 begin = this->xToSample(x);
            const qint64 end = std::max(begin + 1, this->xToSample(x + 1));
            float min_value = 0.0f;
            float max_value = 0.0f;
            if (!this->buffer_->minMax(begin, end, &min_value, &max_value))
                continue;
            const int y1 = plot.bottom() - static_cast<int>((min_value - minimum) / (maximum - minimum) * plot.height());
            const int y2 = plot.bottom() - static_cast<int>((max_value - minimum) / (maximum - minimum) * plot.height());
            painter.drawLine(x, y1, x, y2);
        }
    }
    else
    {
        QPainterPath path;
        bool first_point = true;
        for (qint64 index = first; index < last; ++index)
        {
            float value = 0.0f;
            if (!this->buffer_->sampleAt(index, &value))
                continue;
            const int x = this->sampleToX(index);
            const int y = plot.bottom() - static_cast<int>((value - minimum) / (maximum - minimum) * plot.height());
            if (first_point)
            {
                path.moveTo(x, y);
                first_point = false;
            }
            else
            {
                path.lineTo(x, y);
            }
        }
        painter.drawPath(path);
    }
    painter.setClipping(false);

    this->drawSelection(painter);
    this->drawPlayhead(painter);
    this->drawOverlay(painter);

    painter.setPen(kAxis);
    painter.setFont(this->font());
    const QString info = QStringLiteral("fs=%1 Hz  窗口=%2 s  样本=%3")
                             .arg(fs, 0, 'f', 0)
                             .arg(this->viewSpan_ / std::max(1.0, fs), 0, 'f', 2)
                             .arg(this->buffer_->count());
    painter.drawText(QRect(plot.left(), 4, plot.width(), 20), Qt::AlignLeft | Qt::AlignVCenter, info);
    if (this->frozen_)
    {
        painter.setPen(kCursor);
        painter.drawText(QRect(plot.left(), 4, plot.width(), 20), Qt::AlignRight | Qt::AlignVCenter,
                         QStringLiteral("已冻结（后台仍在接收）"));
    }
}

void vuprs::client::WaveformWidget::drawNoData(QPainter &painter)
{
    painter.setPen(kAxis);
    painter.drawText(this->plotRect(), Qt::AlignCenter, QStringLiteral("等待波束形成数据…"));
}

void vuprs::client::WaveformWidget::drawSelection(QPainter &painter)
{
    if (!this->selecting_ || this->buffer_ == nullptr)
        return;

    const QRect plot = this->plotRect();
    const qint64 start = std::min(this->selectionAnchorSample_, this->selectionCurrentSample_);
    const qint64 end = std::max(this->selectionAnchorSample_, this->selectionCurrentSample_);
    if (end <= start)
        return;

    const int anchor_x = std::max(plot.left(), std::min(plot.right(), this->sampleToX(start)));
    const int current_x = std::max(plot.left(), std::min(plot.right(), this->sampleToX(end)));
    const int left_x = std::min(anchor_x, current_x);
    const int right_x = std::max(anchor_x, current_x);
    const QRect selection(QPoint(left_x, plot.top()), QPoint(right_x, plot.bottom()));
    painter.fillRect(selection, QColor(80, 160, 255, 60));
    painter.setPen(QPen(QColor(80, 160, 255), 1, Qt::DashLine));
    painter.drawRect(selection);

    const double fs = this->buffer_->sampleRateHz();
    if (fs <= 0.0)
        return;
    const QString text = QStringLiteral("段 %1 – %2 s")
                             .arg(static_cast<double>(start) / fs, 0, 'f', 3)
                             .arg(static_cast<double>(end) / fs, 0, 'f', 3);
    const QFontMetrics metrics(this->font());
    const int text_width = metrics.horizontalAdvance(text) + 16;
    int text_x = left_x + 6;
    if (text_x + text_width > plot.right())
        text_x = std::max(plot.left() + 4, plot.right() - text_width - 4);
    const QRect text_rect(text_x, plot.top() + 6, text_width, 24);
    painter.fillRect(text_rect, kOverlay);
    painter.setPen(QColor(120, 200, 255));
    painter.drawText(text_rect.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
}

void vuprs::client::WaveformWidget::drawPlayhead(QPainter &painter)
{
    if (!this->playheadVisible_ || this->buffer_ == nullptr || this->viewSpan_ <= 0)
        return;
    if (this->playheadSample_ < this->viewStart_ ||
        this->playheadSample_ > this->viewStart_ + this->viewSpan_)
        return;

    const QRect plot = this->plotRect();
    const int x = this->sampleToX(this->playheadSample_);
    if (x < plot.left() || x > plot.right())
        return;

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(kPlayhead, 1.4));
    painter.drawLine(x, plot.top(), x, plot.bottom());
    painter.setPen(Qt::NoPen);
    painter.setBrush(kPlayhead);
    QPolygon marker;
    marker << QPoint(x - 5, plot.top()) << QPoint(x + 5, plot.top()) << QPoint(x, plot.top() + 8);
    painter.drawPolygon(marker);
}

void vuprs::client::WaveformWidget::drawOverlay(QPainter &painter)
{
    if (!this->cursorVisible_ || this->buffer_ == nullptr)
        return;
    const QRect plot = this->plotRect();
    if (!plot.contains(this->cursorPos_))
        return;
    const qint64 sample = this->xToSample(this->cursorPos_.x());
    float value = 0.0f;
    const bool has_value = this->buffer_->sampleAt(sample, &value);
    painter.setPen(QPen(kCursor, 1, Qt::DashLine));
    painter.drawLine(this->cursorPos_.x(), plot.top(), this->cursorPos_.x(), plot.bottom());
    if (!has_value)
        return;
    const double fs = this->buffer_->sampleRateHz();
    const QString text = QStringLiteral("t=%1 s  sample=%2  A=%3")
                             .arg(fs > 0.0 ? sample / fs : 0.0, 0, 'f', 5)
                             .arg(sample)
                             .arg(value, 0, 'f', 5);
    const QFontMetrics metrics(this->font());
    const QRect text_rect(plot.left() + 8, plot.top() + 6, metrics.horizontalAdvance(text) + 16, 24);
    painter.fillRect(text_rect, kOverlay);
    painter.setPen(kCursor);
    painter.drawText(text_rect.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
}

void vuprs::client::WaveformWidget::wheelEvent(QWheelEvent *event)
{
    const int delta = event->angleDelta().y();
    if (delta == 0)
        return;
    const double factor = delta > 0 ? 0.8 : 1.25;
    if (event->modifiers() & Qt::ControlModifier)
        this->zoomY(factor, event->position().toPoint().y());
    else
        this->zoomX(factor, event->position().toPoint().x());
    event->accept();
}

void vuprs::client::WaveformWidget::mousePressEvent(QMouseEvent *event)
{
    const QPoint position = event->position().toPoint();
    if (this->plotRect().contains(position))
    {
        if (event->button() == Qt::LeftButton && this->selectionEnabled_)
        {
            this->selecting_ = true;
            this->selectionAnchorSample_ = this->xToSample(position.x());
            this->selectionCurrentSample_ = this->selectionAnchorSample_;
            this->selectionAnchorX_ = position.x();
            this->cursorPos_ = position;
            this->cursorVisible_ = true;
            this->update();
            emit this->selectionStarted();
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton ||
            event->button() == Qt::RightButton ||
            event->button() == Qt::MiddleButton)
        {
            this->dragging_ = true;
            this->lastMousePos_ = position;
            this->cursorPos_ = position;
            this->cursorVisible_ = true;
            this->update();
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void vuprs::client::WaveformWidget::mouseMoveEvent(QMouseEvent *event)
{
    this->cursorPos_ = event->position().toPoint();
    this->cursorVisible_ = this->plotRect().contains(this->cursorPos_);
    if (this->selecting_)
    {
        this->selectionCurrentSample_ = this->xToSample(this->cursorPos_.x());
        this->update();
        QWidget::mouseMoveEvent(event);
        return;
    }
    if (this->dragging_ && this->buffer_ != nullptr)
    {
        const QRect plot = this->plotRect();
        const int dx = this->cursorPos_.x() - this->lastMousePos_.x();
        if (plot.width() > 0 && dx != 0)
        {
            const qint64 delta = -static_cast<qint64>(static_cast<double>(dx) * this->viewSpan_ / plot.width());
            this->viewStart_ += delta;
            this->setAutoFollowInternal(false);
            this->ensureView();
            this->lastMousePos_ = this->cursorPos_;
        }
    }
    this->update();
    QWidget::mouseMoveEvent(event);
}

void vuprs::client::WaveformWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        if (this->selecting_)
        {
            this->selecting_ = false;
            const int distance = qAbs(event->position().toPoint().x() - this->selectionAnchorX_);
            if (distance >= 3)
            {
                const qint64 start = std::min(this->selectionAnchorSample_, this->selectionCurrentSample_);
                const qint64 end = std::max(this->selectionAnchorSample_, this->selectionCurrentSample_);
                if (end > start)
                    emit this->rangeSelected(start, end);
            }
            this->update();
        }
        this->dragging_ = false;
    }
    else if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton)
    {
        this->dragging_ = false;
    }
    QWidget::mouseReleaseEvent(event);
}

void vuprs::client::WaveformWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        emit this->resetViewRequested();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void vuprs::client::WaveformWidget::leaveEvent(QEvent *event)
{
    this->cursorVisible_ = false;
    this->update();
    QWidget::leaveEvent(event);
}
