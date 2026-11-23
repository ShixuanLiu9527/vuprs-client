#ifndef VUPRS_CLIENT_SETTINGS_DIALOG_H
#define VUPRS_CLIENT_SETTINGS_DIALOG_H

#include <QDialog>
#include "core/app_config.h"

class QLineEdit;
class QSpinBox;

namespace vuprs
{
    namespace client
    {
        class SettingsDialog : public QDialog
        {
            Q_OBJECT

        public:
            explicit SettingsDialog(const AppConfig &config, QWidget *parent = nullptr);

            AppConfig config() const { return this->config_; }

        public slots:
            void accept() override;

        private slots:
            void browseRecordingDirectory();

        private:
            AppConfig config_;
            QLineEdit *hostEdit_ = nullptr;
            QSpinBox *portSpin_ = nullptr;
            QSpinBox *connectTimeoutSpin_ = nullptr;
            QSpinBox *commandTimeoutSpin_ = nullptr;
            QSpinBox *historySpin_ = nullptr;
            QSpinBox *defaultWindowSpin_ = nullptr;
            QSpinBox *beamQueueSpin_ = nullptr;
            QLineEdit *recordingDirectoryEdit_ = nullptr;
            QSpinBox *recordingQueueSpin_ = nullptr;
        };
    }
}

#endif
