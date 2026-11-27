#ifndef VUPRS_CLIENT_INFERENCE_BAR_WIDGET_H
#define VUPRS_CLIENT_INFERENCE_BAR_WIDGET_H

#include <QVector>
#include <QWidget>
#include "core/beam_frame.h"

class QMouseEvent;

namespace vuprs
{
    namespace client
    {
        /**
         * @brief Bar chart for the Q31 inference tensor received as INFERENCE_DATA.
         *
         * Bars are normalized per frame to [0, 100]%. The widget draws one bar per
         * tensor element and shows the argmax index in the title.
         */
        class InferenceBarWidget : public QWidget
        {
        public:
            explicit InferenceBarWidget(QWidget *parent = nullptr);

            void setInferenceFrame(const InferenceFrame &frame);
            void setExpectedLength(int length);
            void clear();

            QSize sizeHint() const override;
            QSize minimumSizeHint() const override;

        protected:
            void paintEvent(QPaintEvent *event) override;
            void mouseMoveEvent(QMouseEvent *event) override;
            void leaveEvent(QEvent *event) override;

        private:
            QRect plotRect() const;
            int barIndexAt(int x) const;

            InferenceFrame frame_;
            QVector<double> percentages_;
            int argMax_ = -1;
            int expectedLength_ = 0;
            int hoverIndex_ = -1;
            bool hasFrame_ = false;
        };
    }
}

#endif
