#include "ui/inference_bar_widget.h"

#include <algorithm>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include "core/inference_utils.h"

namespace
{
    const QColor kBackground(16, 24, 32);
    const QColor kPlotBackground(12, 20, 28);
    const QColor kGrid(42, 58, 74);
    const QColor kAxis(170, 190, 210);
    const QColor kText(225, 235, 245);
    const QColor kBar(60, 210, 140);
    const QColor kMaxBar(255, 185, 60);
}

vuprs::client::InferenceBarWidget::InferenceBarWidget(QWidget *parent)
    : QWidget(parent)
{
    this->setMouseTracking(true);
    this->setMinimumHeight(160);
    this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void vuprs::client::InferenceBarWidget::setInferenceFrame(const InferenceFrame &frame)
{
    this->frame_ = frame;
    this->percentages_ = normalizeInferencePercent(frame.values);
    this->argMax_ = inferenceArgMax(frame.values);
    this->hasFrame_ = !frame.values.isEmpty();
    this->hoverIndex_ = -1;
    this->setMinimumWidth(std::max(360, 76 + static_cast<int>(frame.values.size()) * 42));
    this->updateGeometry();
    this->update();
}

void vuprs::client::InferenceBarWidget::setExpectedLength(int length)
{
    this->expectedLength_ = std::max(0, length);
    this->update();
}

void vuprs::client::InferenceBarWidget::clear()
{
    this->frame_ = InferenceFrame();
    this->percentages_.clear();
    this->argMax_ = -1;
    this->hoverIndex_ = -1;
    this->hasFrame_ = false;
    this->setMinimumWidth(360);
    this->updateGeometry();
    this->update();
}

QSize vuprs::client::InferenceBarWidget::sizeHint() const
{
    const int count = std::max(1, static_cast<int>(this->frame_.values.size()));
    return QSize(std::max(360, 76 + count * 42), 200);
}

QSize vuprs::client::InferenceBarWidget::minimumSizeHint() const
{
    const int count = std::max(1, static_cast<int>(this->frame_.values.size()));
    return QSize(std::max(320, 76 + count * 32), 150);
}

QRect vuprs::client::InferenceBarWidget::plotRect() const
{
    return QRect(58, 62, std::max(60, this->width() - 76), std::max(50, this->height() - 96));
}

int vuprs::client::InferenceBarWidget::barIndexAt(int x) const
{
    if (!this->hasFrame_ || this->frame_.values.isEmpty())
        return -1;
    const QRect plot = this->plotRect();
    if (x < plot.left() || x > plot.right())
        return -1;
    const int count = static_cast<int>(this->frame_.values.size());
    const double slot = static_cast<double>(plot.width()) / count;
    if (slot <= 0.0)
        return -1;
    return std::max(0, std::min(count - 1, static_cast<int>((x - plot.left()) / slot)));
}

void vuprs::client::InferenceBarWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(this->rect(), kBackground);
    const QRect plot = this->plotRect();
    painter.fillRect(plot, kPlotBackground);

    painter.setPen(kText);
    painter.drawText(QRect(16, 8, this->width() - 32, 20), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("推理结果"));
    painter.setPen(kAxis);
    QString subtitle;
    if (this->hasFrame_)
    {
        subtitle = QStringLiteral("N=%1  seq=%2  argmax=#%3  0-100% (min-max)")
                       .arg(this->frame_.values.size())
                       .arg(this->frame_.sequence)
                       .arg(this->argMax_);
        if (this->expectedLength_ > 0 && this->expectedLength_ != this->frame_.values.size())
        {
            subtitle += QStringLiteral("  期望 N=%1").arg(this->expectedLength_);
        }
    }
    else if (this->expectedLength_ > 0)
    {
        subtitle = QStringLiteral("等待推理数据, 期望 N=%1").arg(this->expectedLength_);
    }
    else
    {
        subtitle = QStringLiteral("等待推理数据");
    }
    painter.drawText(QRect(16, 26, this->width() - 32, 18), Qt::AlignLeft | Qt::AlignVCenter, subtitle);

    for (int i = 0; i <= 4; ++i)
    {
        const int y = plot.bottom() - plot.height() * i / 4;
        painter.setPen(QPen(kGrid, 1));
        painter.drawLine(plot.left(), y, plot.right(), y);
        painter.setPen(kAxis);
        painter.drawText(QRect(0, y - 9, plot.left() - 8, 18),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QStringLiteral("%1%").arg(i * 25));
    }

    if (!this->hasFrame_ || this->percentages_.isEmpty())
    {
        painter.setPen(kAxis);
        painter.drawText(plot, Qt::AlignCenter, QStringLiteral("等待推理数据…"));
        return;
    }

    const int count = static_cast<int>(this->percentages_.size());
    const double slot = static_cast<double>(plot.width()) / count;
    const double bar_width = std::max(2.0, std::min(32.0, slot * 0.62));
    const bool show_value = slot >= 34.0;
    const bool show_index = slot >= 18.0;

    for (int i = 0; i < count; ++i)
    {
        const double percent = std::max(0.0, std::min(100.0, this->percentages_.at(i)));
        const int bar_height = std::max(1, static_cast<int>(percent * plot.height() / 100.0));
        const int bar_left = plot.left() + static_cast<int>(slot * i + (slot - bar_width) / 2.0);
        const QRect bar(bar_left, plot.bottom() - bar_height,
                        std::max(1, static_cast<int>(bar_width)), bar_height);
        QColor color = (i == this->argMax_) ? kMaxBar : kBar;
        if (i == this->hoverIndex_)
            color = color.lighter(125);
        painter.fillRect(bar, color);

        painter.setPen(kAxis);
        if (show_value)
        {
            painter.drawText(QRect(bar_left - 20, bar.top() - 18,
                                   static_cast<int>(bar_width) + 40, 16),
                             Qt::AlignCenter,
                             QStringLiteral("%1%").arg(percent, 0, 'f', 0));
        }
        if (show_index)
        {
            painter.drawText(QRect(static_cast<int>(plot.left() + slot * i),
                                   plot.bottom() + 4, static_cast<int>(slot), 16),
                             Qt::AlignCenter,
                             QStringLiteral("#%1").arg(i));
        }
    }
}

void vuprs::client::InferenceBarWidget::mouseMoveEvent(QMouseEvent *event)
{
    const int index = this->barIndexAt(event->position().toPoint().x());
    if (index != this->hoverIndex_)
    {
        this->hoverIndex_ = index;
        this->update();
    }
    if (index >= 0 && index < this->frame_.values.size())
    {
        const qint32 raw = this->frame_.values.at(index);
        QToolTip::showText(
            event->globalPosition().toPoint(),
            QStringLiteral("index: #%1\nraw Q31: %2\nQ31 value: %3\nbar: %4%")
                .arg(index)
                .arg(raw)
                .arg(inferenceQ31ToDouble(raw), 0, 'f', 6)
                .arg(index < this->percentages_.size() ? this->percentages_.at(index) : 0.0,
                     0, 'f', 1),
            this);
    }
    else
    {
        QToolTip::hideText();
    }
    QWidget::mouseMoveEvent(event);
}

void vuprs::client::InferenceBarWidget::leaveEvent(QEvent *event)
{
    this->hoverIndex_ = -1;
    this->update();
    QToolTip::hideText();
    QWidget::leaveEvent(event);
}
