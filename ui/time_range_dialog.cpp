#include "ui/time_range_dialog.h"

#include <algorithm>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>

vuprs::client::TimeRangeDialog::TimeRangeDialog(double durationSeconds,
                                                double startSeconds,
                                                double endSeconds,
                                                QWidget *parent)
    : QDialog(parent), durationSeconds_(std::max(0.0, durationSeconds))
{
    this->setWindowTitle(QStringLiteral("选择回放时间段"));
    this->setMinimumWidth(460);

    auto *layout = new QVBoxLayout(this);
    auto *duration_label = new QLabel(
        QStringLiteral("录制总时长: %1 s").arg(this->durationSeconds_, 0, 'f', 3));
    layout->addWidget(duration_label);

    auto *form = new QFormLayout;
    this->startSpin_ = new QDoubleSpinBox;
    this->startSpin_->setRange(0.0, this->durationSeconds_);
    this->startSpin_->setDecimals(3);
    this->startSpin_->setSuffix(QStringLiteral(" s"));
    this->startSpin_->setValue(std::clamp(startSeconds, 0.0, this->durationSeconds_));
    this->startSpin_->setToolTip(QStringLiteral("回放时间段起点（秒），必须小于结束时刻。"));

    this->endSpin_ = new QDoubleSpinBox;
    this->endSpin_->setRange(0.0, this->durationSeconds_);
    this->endSpin_->setDecimals(3);
    this->endSpin_->setSuffix(QStringLiteral(" s"));
    this->endSpin_->setValue(std::clamp(endSeconds, 0.0, this->durationSeconds_));
    this->endSpin_->setToolTip(QStringLiteral("回放时间段终点（秒），不能超过录制总时长。"));
    this->endSpin_->setMinimum(this->startSpin_->value());
    if (this->endSpin_->value() < this->startSpin_->value())
        this->endSpin_->setValue(this->startSpin_->value());

    form->addRow(QStringLiteral("开始时刻"), this->startSpin_);
    form->addRow(QStringLiteral("结束时刻"), this->endSpin_);
    layout->addLayout(form);

    auto *hint = new QLabel(QStringLiteral(
        "也可以直接在波形上按住鼠标左键拖动选择时间段；回放时按住右键可平移视图。"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(this->startSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value)
            {
                this->endSpin_->setMinimum(value);
                if (this->endSpin_->value() < value)
                    this->endSpin_->setValue(value);
            });
    connect(buttons, &QDialogButtonBox::accepted, this, &TimeRangeDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &TimeRangeDialog::reject);
}

double vuprs::client::TimeRangeDialog::startSeconds() const
{
    return this->startSpin_->value();
}

double vuprs::client::TimeRangeDialog::endSeconds() const
{
    return this->endSpin_->value();
}

void vuprs::client::TimeRangeDialog::accept()
{
    if (this->endSpin_->value() <= this->startSpin_->value())
    {
        QMessageBox::warning(this, QStringLiteral("选择回放时间段"),
                             QStringLiteral("结束时刻必须大于开始时刻"));
        return;
    }
    QDialog::accept();
}
