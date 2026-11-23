#ifndef VUPRS_CLIENT_WAVEFORM_WIDGET_H
#define VUPRS_CLIENT_WAVEFORM_WIDGET_H

#include <QtGlobal>
#include <QPoint>
#include <QWidget>
#include "core/waveform_buffer.h"

namespace vuprs
{
    namespace client
    {
        class WaveformWidget : public QWidget
        {
            Q_OBJECT

        public:
            explicit WaveformWidget(QWidget *parent = nullptr);

            void setBuffer(WaveformBuffer *buffer);
            void setDefaultWindowSeconds(double seconds);
            void setViewWindowSeconds(double seconds);
            void setAutoFollow(bool enabled);
            bool autoFollow() const { return this->autoFollow_; }
            void setFrozen(bool frozen);
            bool frozen() const { return this->frozen_; }
            void resetView();

            void setViewRange(qint64 startSample, qint64 spanSamples);
            qint64 viewStartSamples() const { return this->viewStart_; }
            qint64 viewSpanSamples() const;
            void setPlayheadSample(qint64 sample, bool visible);
            void setSelectionEnabled(bool enabled);
            bool selectionEnabled() const { return this->selectionEnabled_; }

        signals:
            void autoFollowChanged(bool enabled);
            void selectionStarted();
            void rangeSelected(qint64 startSample, qint64 endSample);
            void resetViewRequested();

        protected:
            void paintEvent(QPaintEvent *event) override;
            void wheelEvent(QWheelEvent *event) override;
            void mousePressEvent(QMouseEvent *event) override;
            void mouseMoveEvent(QMouseEvent *event) override;
            void mouseReleaseEvent(QMouseEvent *event) override;
            void mouseDoubleClickEvent(QMouseEvent *event) override;
            void leaveEvent(QEvent *event) override;

        private:
            QRect plotRect() const;
            qint64 currentSpanSamples() const;
            void ensureView();
            void setAutoFollowInternal(bool enabled);
            void zoomX(double factor, int anchorX);
            void zoomY(double factor, int anchorY);
            qint64 xToSample(int x) const;
            int sampleToX(qint64 sample) const;
            void drawNoData(QPainter &painter);
            void drawPlayhead(QPainter &painter);
            void drawSelection(QPainter &painter);
            void drawOverlay(QPainter &painter);

            WaveformBuffer *buffer_ = nullptr;
            bool autoFollow_ = true;
            bool frozen_ = false;
            bool dragging_ = false;
            bool cursorVisible_ = false;
            QPoint lastMousePos_;
            QPoint cursorPos_;
            qint64 viewStart_ = 0;
            qint64 viewSpan_ = 0;
            bool yAuto_ = true;
            float yManualMin_ = -1.0f;
            float yManualMax_ = 1.0f;
            double defaultWindowSeconds_ = 10.0;
            double viewSampleRate_ = 0.0;
            qint64 playheadSample_ = 0;
            bool playheadVisible_ = false;
            bool selectionEnabled_ = false;
            bool selecting_ = false;
            qint64 selectionAnchorSample_ = 0;
            qint64 selectionCurrentSample_ = 0;
            int selectionAnchorX_ = 0;
        };
    }
}

#endif
