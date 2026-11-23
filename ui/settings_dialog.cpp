#include "ui/settings_dialog.h"

#include <algorithm>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace
{
    QSpinBox *MakeSpin(int minimum, int maximum, int value, const QString &suffix = QString())
    {
        auto *spin = new QSpinBox;
        spin->setRange(minimum, maximum);
        spin->setValue(value);
        if (!suffix.isEmpty())
            spin->setSuffix(suffix);
        return spin;
    }

    void SetFieldToolTip(QFormLayout *form, QWidget *field, const QString &text)
    {
        field->setToolTip(text);
        if (QWidget *label = form->labelForField(field))
            label->setToolTip(text);
    }
}

vuprs::client::SettingsDialog::SettingsDialog(const AppConfig &config, QWidget *parent)
    : QDialog(parent), config_(config)
{
    this->setWindowTitle(QStringLiteral("软件设置"));
    this->setMinimumWidth(520);

    auto *layout = new QVBoxLayout(this);

    auto *server_group = new QGroupBox(QStringLiteral("服务器"));
    auto *server_form = new QFormLayout(server_group);
    this->hostEdit_ = new QLineEdit(this->config_.host);
    this->portSpin_ = MakeSpin(1, 65535, this->config_.port);
    this->connectTimeoutSpin_ = MakeSpin(500, 30000, this->config_.connectTimeoutMs, QStringLiteral(" ms"));
    this->connectTimeoutSpin_->setSingleStep(500);
    this->commandTimeoutSpin_ = MakeSpin(500, 30000, this->config_.commandTimeoutMs, QStringLiteral(" ms"));
    this->commandTimeoutSpin_->setSingleStep(500);
    server_form->addRow(QStringLiteral("默认地址"), this->hostEdit_);
    server_form->addRow(QStringLiteral("默认端口"), this->portSpin_);
    server_form->addRow(QStringLiteral("连接超时"), this->connectTimeoutSpin_);
    server_form->addRow(QStringLiteral("命令超时"), this->commandTimeoutSpin_);
    SetFieldToolTip(server_form, this->hostEdit_,
                    QStringLiteral("客户端默认连接的服务器 IP 或主机名。"));
    SetFieldToolTip(server_form, this->portSpin_,
                    QStringLiteral("服务器 TCP 端口，默认 8080。"));
    SetFieldToolTip(server_form, this->connectTimeoutSpin_,
                    QStringLiteral("TCP 连接超时时间，下次连接时生效。"));
    SetFieldToolTip(server_form, this->commandTimeoutSpin_,
                    QStringLiteral("单条控制命令等待响应的超时时间，下次启动后生效。"));
    layout->addWidget(server_group);

    auto *waveform_group = new QGroupBox(QStringLiteral("波形"));
    auto *waveform_form = new QFormLayout(waveform_group);
    this->historySpin_ = MakeSpin(5, 300, this->config_.historySeconds, QStringLiteral(" s"));
    this->defaultWindowSpin_ = MakeSpin(1, 120, this->config_.defaultWindowSeconds, QStringLiteral(" s"));
    this->beamQueueSpin_ = MakeSpin(32, 4096, this->config_.beamQueueFrames);
    waveform_form->addRow(QStringLiteral("历史长度"), this->historySpin_);
    waveform_form->addRow(QStringLiteral("默认窗口"), this->defaultWindowSpin_);
    waveform_form->addRow(QStringLiteral("接收队列帧数"), this->beamQueueSpin_);
    SetFieldToolTip(waveform_form, this->historySpin_,
                    QStringLiteral("实时波形环形缓存保留的历史时长，单位秒。修改后会重建缓存。"));
    SetFieldToolTip(waveform_form, this->defaultWindowSpin_,
                    QStringLiteral("波形默认显示时间窗长度，不能超过历史长度。"));
    SetFieldToolTip(waveform_form, this->beamQueueSpin_,
                    QStringLiteral("网络接收与 UI 之间的 BF 帧队列容量，下次启动后生效。"));
    auto *waveform_note = new QLabel(QStringLiteral("接收队列帧数在下次启动后生效。"));
    waveform_note->setWordWrap(true);
    waveform_form->addRow(waveform_note);
    layout->addWidget(waveform_group);

    auto *recording_group = new QGroupBox(QStringLiteral("录制"));
    auto *recording_form = new QFormLayout(recording_group);
    auto *directory_layout = new QHBoxLayout;
    this->recordingDirectoryEdit_ = new QLineEdit(this->config_.recordingDirectory);
    auto *browse_button = new QPushButton(QStringLiteral("选择目录..."));
    directory_layout->addWidget(this->recordingDirectoryEdit_, 1);
    directory_layout->addWidget(browse_button);
    this->recordingQueueSpin_ = MakeSpin(32, 8192, this->config_.recordingQueueFrames);
    this->recordingDirectoryEdit_->setToolTip(
        QStringLiteral("录制文件保存目录。每次录制会在此新建 task<任务ID>_id<序号>.vbf。"));
    recording_form->addRow(QStringLiteral("录制目录"), directory_layout);
    recording_form->addRow(QStringLiteral("录制队列帧数"), this->recordingQueueSpin_);
    SetFieldToolTip(recording_form, this->recordingQueueSpin_,
                    QStringLiteral("录制线程队列容量，下一次录制开始时生效。"));
    auto *recording_note = new QLabel(QStringLiteral("录制队列帧数在下一次录制开始时生效。"));
    recording_note->setWordWrap(true);
    recording_form->addRow(recording_note);
    layout->addWidget(recording_group);

    auto *general_note = new QLabel(
        QStringLiteral("命令超时在下次启动后生效；其他设置在保存后立即应用。"));
    general_note->setWordWrap(true);
    layout->addWidget(general_note);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);

    connect(this->historySpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int history)
            {
                this->defaultWindowSpin_->setMaximum(std::max(1, std::min(120, history)));
            });
    this->defaultWindowSpin_->setMaximum(std::max(1, std::min(120, this->historySpin_->value())));
    this->defaultWindowSpin_->setValue(
        std::min(this->config_.defaultWindowSeconds, this->defaultWindowSpin_->maximum()));
    connect(browse_button, &QPushButton::clicked, this, &SettingsDialog::browseRecordingDirectory);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
}

void vuprs::client::SettingsDialog::browseRecordingDirectory()
{
    const QString directory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择录制目录"), this->recordingDirectoryEdit_->text());
    if (!directory.isEmpty())
        this->recordingDirectoryEdit_->setText(directory);
}

void vuprs::client::SettingsDialog::accept()
{
    const QString host = this->hostEdit_->text().trimmed();
    const QString directory = QDir::cleanPath(this->recordingDirectoryEdit_->text().trimmed());
    if (host.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("软件设置"), QStringLiteral("服务器地址不能为空"));
        return;
    }
    if (directory.isEmpty() || directory == QStringLiteral("."))
    {
        QMessageBox::warning(this, QStringLiteral("软件设置"), QStringLiteral("录制目录不能为空"));
        return;
    }

    this->config_.host = host;
    this->config_.port = static_cast<quint16>(this->portSpin_->value());
    this->config_.connectTimeoutMs = this->connectTimeoutSpin_->value();
    this->config_.commandTimeoutMs = this->commandTimeoutSpin_->value();
    this->config_.historySeconds = this->historySpin_->value();
    this->config_.defaultWindowSeconds = this->defaultWindowSpin_->value();
    this->config_.beamQueueFrames = this->beamQueueSpin_->value();
    this->config_.recordingDirectory = directory;
    this->config_.recordingQueueFrames = this->recordingQueueSpin_->value();
    QDialog::accept();
}
