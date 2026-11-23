#ifndef VUPRS_CLIENT_HEMISPHERE_WIDGET_H
#define VUPRS_CLIENT_HEMISPHERE_WIDGET_H

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QWidget>
#include "core/beam_frame.h"
#include "core/fibonacci.h"

namespace vuprs
{
    namespace client
    {
        class HemisphereWidget : public QWidget
        {
            Q_OBJECT

        public:
            explicit HemisphereWidget(QWidget *parent = nullptr);

            void setScanFrame(const ScanFrame &frame);
            void clear();
            void setBeamDirection(double altDeg, double azDeg);
            void setColorScaleLocked(bool locked, double minDb = 0.0, double maxDb = 0.0);
            void setResolution(int resolution);

        signals:
            void beamDirectionPicked(double altDeg, double azDeg);

        protected:
            void paintEvent(QPaintEvent *event) override;
            void mouseMoveEvent(QMouseEvent *event) override;
            void mouseDoubleClickEvent(QMouseEvent *event) override;
            void leaveEvent(QEvent *event) override;
            void resizeEvent(QResizeEvent *event) override;

        private:
            QRect circleRect() const;
            void rebuildImage();
            QColor colorFor(double normalized) const;
            bool pointToWidget(const GridPoint &point, QPointF *target) const;
            int nearestPoint(const QPointF &position, double *distance) const;
            QRectF colorBarRect() const;
            QString hoverText() const;
            void drawOverlay(QPainter &painter) const;
            double displayValue(int index) const;
            void updateColorScale();

            ScanFrame frame_;
            QVector<GridPoint> grid_;
            QImage image_;
            bool hasFrame_ = false;
            bool mismatch_ = false;
            int resolution_ = 256;
            bool imageDirty_ = true;

            bool hasBeam_ = true;
            double beamAltDeg_ = 90.0;
            double beamAzDeg_ = 0.0;

            bool scaleLocked_ = false;
            double scaleMinDb_ = 0.0;
            double scaleMaxDb_ = 1.0;

            QPoint cursorPos_;
            bool cursorInside_ = false;
            int hoverIndex_ = -1;
        };
    }
}

#endif
