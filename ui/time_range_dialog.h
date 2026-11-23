#ifndef VUPRS_CLIENT_TIME_RANGE_DIALOG_H
#define VUPRS_CLIENT_TIME_RANGE_DIALOG_H

#include <QDialog>

class QDoubleSpinBox;

namespace vuprs
{
    namespace client
    {
        class TimeRangeDialog : public QDialog
        {
            Q_OBJECT

        public:
            explicit TimeRangeDialog(double durationSeconds,
                                     double startSeconds,
                                     double endSeconds,
                                     QWidget *parent = nullptr);

            double startSeconds() const;
            double endSeconds() const;

        public slots:
            void accept() override;

        private:
            double durationSeconds_ = 0.0;
            QDoubleSpinBox *startSpin_ = nullptr;
            QDoubleSpinBox *endSpin_ = nullptr;
        };
    }
}

#endif
