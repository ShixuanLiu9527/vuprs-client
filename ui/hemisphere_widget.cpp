#include "ui/hemisphere_widget.h"

#include <algorithm>
#include <cmath>
#include <array>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

namespace
{
    constexpr double PI = 3.14159265358979323846;
    const QColor kBackground(16, 24, 32);
    const QColor kCircle(10, 18, 26);
    const QColor kGrid(76, 96, 116);
    const QColor kAxis(190, 205, 220);
    const QColor kWarning(255, 180, 60);
    const QColor kBeam(80, 210, 255);
    const QColor kMax(255, 90, 90);

    QColor Viridis(double t)
    {
        struct Stop
        {
            double t;
            int r;
            int g;
            int b;
        };
        static const std::array<Stop, 5> stops = {{
            {0.00, 68, 1, 84},
            {0.25, 59, 82, 139},
            {0.50, 33, 145, 140},
            {0.75, 94, 201, 98},
            {1.00, 253, 231, 37},
        }};
        t = std::max(0.0, std::min(1.0, t));
        for (size_t i = 1; i < stops.size(); i++)
        {
            if (t <= stops[i].t)
            {
                const Stop &a = stops[i - 1];
                const Stop &b = stops[i];
                const double ratio = (t - a.t) / std::max(1e-9, b.t - a.t);
                return QColor(static_cast<int>(a.r + (b.r - a.r) * ratio),
                              static_cast<int>(a.g + (b.g - a.g) * ratio),
                              static_cast<int>(a.b + (b.b - a.b) * ratio));
            }
        }
        return QColor(stops.back().r, stops.back().g, stops.back().b);
    }
}

vuprs::client::HemisphereWidget::HemisphereWidget(QWidget *parent)
    : QWidget(parent)
{
    this->setMouseTracking(true);
    this->setMinimumSize(430, 380);
    this->setAutoFillBackground(false);
}

void vuprs::client::HemisphereWidget::setScanFrame(const ScanFrame &frame)
{
    const bool grid_changed = !this->hasFrame_ ||
                              frame.pointsHalf != this->frame_.pointsHalf ||
                              std::abs(frame.altMinDeg - this->frame_.altMinDeg) > 1e-9;
    this->frame_ = frame;
    this->hasFrame_ = true;
    if (grid_changed)
        this->grid_ = fibonacciGrid(frame.pointsHalf, frame.altMinDeg);
    this->mismatch_ = this->grid_.isEmpty() || this->grid_.size() != frame.values.size() ||
                      frame.dataPoints != static_cast<quint32>(frame.values.size());
    this->hoverIndex_ = -1;
    this->updateColorScale();
    this->imageDirty_ = true;
    this->update();
}

void vuprs::client::HemisphereWidget::clear()
{
    this->frame_ = ScanFrame();
    this->grid_.clear();
    this->image_ = QImage();
    this->hasFrame_ = false;
    this->mismatch_ = false;
    this->hoverIndex_ = -1;
    this->imageDirty_ = true;
    this->update();
}

void vuprs::client::HemisphereWidget::setBeamDirection(double altDeg, double azDeg)
{
    this->beamAltDeg_ = altDeg;
    this->beamAzDeg_ = azDeg;
    this->hasBeam_ = true;
    this->update();
}

void vuprs::client::HemisphereWidget::setColorScaleLocked(bool locked, double minDb, double maxDb)
{
    this->scaleLocked_ = locked;
    if (locked)
    {
        this->scaleMinDb_ = minDb;
        this->scaleMaxDb_ = maxDb > minDb ? maxDb : minDb + 1.0;
    }
    this->updateColorScale();
    this->imageDirty_ = true;
    this->update();
}

void vuprs::client::HemisphereWidget::setResolution(int resolution)
{
    this->resolution_ = std::max(64, std::min(resolution, 1024));
    this->imageDirty_ = true;
    this->update();
}

void vuprs::client::HemisphereWidget::updateColorScale()
{
    if (this->scaleLocked_)
        return;
    if (!this->hasFrame_)
    {
        this->scaleMinDb_ = 0.0;
        this->scaleMaxDb_ = 1.0;
        return;
    }
    this->scaleMinDb_ = this->frame_.minPowerDb;
    this->scaleMaxDb_ = this->frame_.maxPowerDb;
    if (this->scaleMaxDb_ <= this->scaleMinDb_)
    {
        this->scaleMinDb_ -= 0.5;
        this->scaleMaxDb_ += 0.5;
    }
}

QRect vuprs::client::HemisphereWidget::circleRect() const
{
    const int available = std::max(120, std::min(this->width() - 190, this->height() - 100));
    const int side = std::max(100, available);
    return QRect(28, 62, side, side);
}

QRectF vuprs::client::HemisphereWidget::colorBarRect() const
{
    const QRect circle = this->circleRect();
    const double height = std::max(80.0, circle.height() * 0.74);
    return QRectF(circle.right() + 34, circle.top(), 22.0, height);
}

bool vuprs::client::HemisphereWidget::pointToWidget(const GridPoint &point, QPointF *target) const
{
    if (target == nullptr)
        return false;
    const QRect circle = this->circleRect();
    const double radius = circle.width() / 2.0;
    const double r = 1.0 - point.alt / 90.0;
    const double az = point.az * PI / 180.0;
    target->setX(circle.center().x() + r * std::sin(az) * radius);
    target->setY(circle.center().y() - r * std::cos(az) * radius);
    return true;
}

int vuprs::client::HemisphereWidget::nearestPoint(const QPointF &position, double *distance) const
{
    const QRect circle = this->circleRect();
    const double radius = circle.width() / 2.0;
    const double dx = (position.x() - circle.center().x()) / radius;
    const double dy = (position.y() - circle.center().y()) / radius;
    int best = -1;
    double best_distance = 1e100;
    for (int i = 0; i < this->grid_.size(); i++)
    {
        const GridPoint &point = this->grid_[i];
        const double r = 1.0 - point.alt / 90.0;
        const double az = point.az * PI / 180.0;
        const double x = r * std::sin(az);
        const double y = -r * std::cos(az);
        const double d2 = (dx - x) * (dx - x) + (dy - y) * (dy - y);
        if (d2 < best_distance)
        {
            best_distance = d2;
            best = i;
        }
    }
    if (distance != nullptr)
        *distance = std::sqrt(best_distance);
    return best;
}

QColor vuprs::client::HemisphereWidget::colorFor(double normalized) const
{
    return Viridis(normalized);
}

void vuprs::client::HemisphereWidget::rebuildImage()
{
    if (!this->hasFrame_ || this->mismatch_ || this->grid_.isEmpty())
    {
        this->image_ = QImage();
        this->imageDirty_ = false;
        return;
    }
    const int resolution = this->resolution_;
    this->image_ = QImage(resolution, resolution, QImage::Format_ARGB32_Premultiplied);
    this->image_.fill(Qt::transparent);

    QVector<QPointF> unit_points;
    unit_points.reserve(this->grid_.size());
    for (const GridPoint &point : this->grid_)
    {
        const double r = 1.0 - point.alt / 90.0;
        const double az = point.az * PI / 180.0;
        unit_points.push_back(QPointF(r * std::sin(az), -r * std::cos(az)));
    }

    const double scale = std::max(1e-9, this->scaleMaxDb_ - this->scaleMinDb_);
    for (int py = 0; py < resolution; py++)
    {
        QRgb *line = reinterpret_cast<QRgb *>(this->image_.scanLine(py));
        const double dy = (static_cast<double>(py) + 0.5) / resolution * 2.0 - 1.0;
        for (int px = 0; px < resolution; px++)
        {
            const double dx = (static_cast<double>(px) + 0.5) / resolution * 2.0 - 1.0;
            const double radius = std::sqrt(dx * dx + dy * dy);
            if (radius > 1.0)
                continue;
            const double alt = 90.0 * (1.0 - radius);
            if (alt < this->frame_.altMinDeg)
                continue;

            std::array<double, 3> best_distance = {1e100, 1e100, 1e100};
            std::array<int, 3> best_index = {-1, -1, -1};
            for (int i = 0; i < unit_points.size(); i++)
            {
                const double ddx = dx - unit_points[i].x();
                const double ddy = dy - unit_points[i].y();
                const double d2 = ddx * ddx + ddy * ddy;
                if (d2 < best_distance[0])
                {
                    best_distance[2] = best_distance[1];
                    best_index[2] = best_index[1];
                    best_distance[1] = best_distance[0];
                    best_index[1] = best_index[0];
                    best_distance[0] = d2;
                    best_index[0] = i;
                }
                else if (d2 < best_distance[1])
                {
                    best_distance[2] = best_distance[1];
                    best_index[2] = best_index[1];
                    best_distance[1] = d2;
                    best_index[1] = i;
                }
                else if (d2 < best_distance[2])
                {
                    best_distance[2] = d2;
                    best_index[2] = i;
                }
            }

            double weight_sum = 0.0;
            double normalized = 0.0;
            for (int i = 0; i < 3; i++)
            {
                if (best_index[i] < 0)
                    continue;
                const double weight = 1.0 / (best_distance[i] + 1e-7);
                weight_sum += weight;
                normalized += weight * static_cast<double>(this->frame_.values[best_index[i]]) / 65535.0;
            }
            if (weight_sum <= 0.0)
                continue;
            normalized /= weight_sum;
            double color_t = normalized;
            if (this->scaleLocked_)
            {
                const double db = this->frame_.minPowerDb + normalized * (this->frame_.maxPowerDb - this->frame_.minPowerDb);
                color_t = (db - this->scaleMinDb_) / scale;
            }
            const QColor color = this->colorFor(std::max(0.0, std::min(1.0, color_t)));
            line[px] = color.rgba();
        }
    }
    this->imageDirty_ = false;
}

double vuprs::client::HemisphereWidget::displayValue(int index) const
{
    if (index < 0 || index >= this->frame_.values.size())
        return 0.0;
    const double normalized = static_cast<double>(this->frame_.values[index]) / 65535.0;
    return this->frame_.minPowerDb + normalized * (this->frame_.maxPowerDb - this->frame_.minPowerDb);
}

QString vuprs::client::HemisphereWidget::hoverText() const
{
    if (this->hoverIndex_ < 0 || this->hoverIndex_ >= this->grid_.size())
        return QString();
    const GridPoint &point = this->grid_[this->hoverIndex_];
    return QStringLiteral("alt=%1°  az=%2°  %3 dB")
        .arg(point.alt, 0, 'f', 1)
        .arg(point.az, 0, 'f', 1)
        .arg(this->displayValue(this->hoverIndex_), 0, 'f', 2);
}

void vuprs::client::HemisphereWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(this->rect(), kBackground);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRect circle = this->circleRect();
    painter.setPen(QPen(kAxis, 1.2));
    painter.setBrush(kCircle);
    painter.drawEllipse(circle);

    if (!this->hasFrame_)
    {
        painter.setPen(kAxis);
        painter.drawText(circle, Qt::AlignCenter, QStringLiteral("等待空间扫描数据…"));
        this->drawOverlay(painter);
        return;
    }

    if (this->imageDirty_ || this->image_.isNull())
        this->rebuildImage();
    if (!this->image_.isNull())
    {
        painter.save();
        QPainterPath clip;
        clip.addEllipse(circle);
        painter.setClipPath(clip);
        painter.drawImage(circle, this->image_);
        painter.restore();
    }

    if (this->mismatch_)
    {
        painter.setPen(kWarning);
        painter.drawText(QRect(circle.left(), circle.bottom() + 10, circle.width(), 22), Qt::AlignCenter,
                         QStringLiteral("扫描网格与数据点数不一致，已停止绘制"));
    }

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(kGrid, 1));
    for (int alt = 30; alt < 90; alt += 30)
    {
        const double radius = circle.width() / 2.0 * (1.0 - alt / 90.0);
        const QRectF ring(circle.center().x() - radius, circle.center().y() - radius, radius * 2.0, radius * 2.0);
        painter.drawEllipse(ring);
    }
    painter.drawLine(circle.center().x(), circle.top(), circle.center().x(), circle.bottom());
    painter.drawLine(circle.left(), circle.center().y(), circle.right(), circle.center().y());

    const double alt_min_radius = circle.width() / 2.0 * (1.0 - this->frame_.altMinDeg / 90.0);
    if (this->frame_.altMinDeg > 0.01 && alt_min_radius > 1.0)
    {
        painter.setPen(QPen(kWarning, 1, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QRectF(circle.center().x() - alt_min_radius,
                                   circle.center().y() - alt_min_radius,
                                   alt_min_radius * 2.0,
                                   alt_min_radius * 2.0));
    }

    painter.setPen(kAxis);
    painter.drawText(QRect(circle.center().x() - 20, circle.top() - 22, 40, 20), Qt::AlignCenter, "N");
    painter.drawText(QRect(circle.right() + 2, circle.center().y() - 10, 20, 20), Qt::AlignCenter, "E");
    painter.drawText(QRect(circle.center().x() - 20, circle.bottom() + 2, 40, 20), Qt::AlignCenter, "S");
    painter.drawText(QRect(circle.left() - 22, circle.center().y() - 10, 20, 20), Qt::AlignCenter, "W");

    if (this->hasBeam_)
    {
        QPointF beam;
        if (this->pointToWidget(GridPoint{this->beamAltDeg_, this->beamAzDeg_}, &beam))
        {
            painter.setPen(QPen(kBeam, 2));
            painter.drawLine(QPointF(beam.x() - 8, beam.y()), QPointF(beam.x() + 8, beam.y()));
            painter.drawLine(QPointF(beam.x(), beam.y() - 8), QPointF(beam.x(), beam.y() + 8));
            painter.drawEllipse(beam, 6, 6);
        }
    }

    if (!this->frame_.values.isEmpty())
    {
        int max_index = 0;
        for (int i = 1; i < this->frame_.values.size(); i++)
        {
            if (this->frame_.values[i] > this->frame_.values[max_index])
                max_index = i;
        }
        if (max_index < this->grid_.size())
        {
            QPointF max_point;
            if (this->pointToWidget(this->grid_[max_index], &max_point))
            {
                painter.setPen(QPen(kMax, 1.5));
                painter.drawLine(QPointF(max_point.x() - 7, max_point.y() - 7), QPointF(max_point.x() + 7, max_point.y() + 7));
                painter.drawLine(QPointF(max_point.x() - 7, max_point.y() + 7), QPointF(max_point.x() + 7, max_point.y() - 7));
            }
        }
    }

    if (this->hoverIndex_ >= 0 && this->hoverIndex_ < this->grid_.size())
    {
        QPointF hover;
        if (this->pointToWidget(this->grid_[this->hoverIndex_], &hover))
        {
            painter.setPen(QPen(Qt::white, 1));
            painter.drawEllipse(hover, 5, 5);
        }
    }

    const QRectF bar = this->colorBarRect();
    for (int y = 0; y < static_cast<int>(bar.height()); y++)
    {
        const double t = 1.0 - static_cast<double>(y) / std::max(1.0, bar.height() - 1.0);
        painter.setPen(this->colorFor(t));
        painter.drawLine(QPointF(bar.left(), bar.top() + y), QPointF(bar.right(), bar.top() + y));
    }
    painter.setPen(QPen(kAxis, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(bar);
    painter.setPen(kAxis);
    painter.drawText(QRectF(bar.right() + 6, bar.top() - 18, 70, 18), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("dB"));
    painter.drawText(QRectF(bar.right() + 6, bar.top() - 4, 70, 18), Qt::AlignLeft | Qt::AlignVCenter,
                     QString::number(this->scaleMaxDb_, 'f', 2));
    painter.drawText(QRectF(bar.right() + 6, bar.bottom() - 14, 70, 18), Qt::AlignLeft | Qt::AlignVCenter,
                     QString::number(this->scaleMinDb_, 'f', 2));

    painter.setPen(kAxis);
    painter.setFont(this->font());
    const QString title = QStringLiteral("空间扫描能量   points=%1  data=%2")
                              .arg(this->frame_.pointsHalf)
                              .arg(this->frame_.dataPoints);
    painter.drawText(QRect(circle.left(), 14, this->width() - 40, 20), Qt::AlignLeft | Qt::AlignVCenter, title);
    const QString subtitle = QStringLiteral("alt_min=%1°  min=%2 dB  max=%3 dB")
                                 .arg(this->frame_.altMinDeg, 0, 'f', 1)
                                 .arg(this->frame_.minPowerDb, 0, 'f', 2)
                                 .arg(this->frame_.maxPowerDb, 0, 'f', 2);
    painter.drawText(QRect(circle.left(), 36, this->width() - 40, 20), Qt::AlignLeft | Qt::AlignVCenter, subtitle);

    this->drawOverlay(painter);
}

void vuprs::client::HemisphereWidget::drawOverlay(QPainter &painter) const
{
    if (!this->hasFrame_ || !this->cursorInside_)
        return;
    const QString text = this->hoverText();
    if (text.isEmpty())
        return;
    const QFontMetrics metrics(this->font());
    const QRect rect(24, this->height() - 34, metrics.horizontalAdvance(text) + 18, 24);
    painter.fillRect(rect, QColor(0, 0, 0, 160));
    painter.setPen(QColor(230, 240, 250));
    painter.drawText(rect.adjusted(9, 0, -9, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
}

void vuprs::client::HemisphereWidget::mouseMoveEvent(QMouseEvent *event)
{
    this->cursorPos_ = event->position().toPoint();
    this->cursorInside_ = this->circleRect().contains(this->cursorPos_);
    if (this->cursorInside_ && this->hasFrame_ && !this->mismatch_)
    {
        double distance = 0.0;
        this->hoverIndex_ = this->nearestPoint(this->cursorPos_, &distance);
        const QRect circle = this->circleRect();
        const double normalized_radius = std::hypot(this->cursorPos_.x() - circle.center().x(),
                                                    this->cursorPos_.y() - circle.center().y()) /
                                         (circle.width() / 2.0);
        if (normalized_radius > 1.0 || distance > 0.15)
            this->hoverIndex_ = -1;
    }
    else
    {
        this->hoverIndex_ = -1;
    }
    this->update();
    QWidget::mouseMoveEvent(event);
}

void vuprs::client::HemisphereWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !this->hasFrame_)
    {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }
    const QRect circle = this->circleRect();
    if (!circle.contains(event->position().toPoint()))
        return;
    const double radius = circle.width() / 2.0;
    const double dx = (event->position().x() - circle.center().x()) / radius;
    const double dy = (event->position().y() - circle.center().y()) / radius;
    const double r = std::min(1.0, std::hypot(dx, dy));
    const double alt = 90.0 * (1.0 - r);
    const double az = std::atan2(dx, -dy) * 180.0 / PI;
    emit this->beamDirectionPicked(alt, az);
    event->accept();
}

void vuprs::client::HemisphereWidget::leaveEvent(QEvent *event)
{
    this->cursorInside_ = false;
    this->hoverIndex_ = -1;
    this->update();
    QWidget::leaveEvent(event);
}

void vuprs::client::HemisphereWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    this->update();
}
