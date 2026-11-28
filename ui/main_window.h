#ifndef VUPRS_CLIENT_MAIN_WINDOW_H
#define VUPRS_CLIENT_MAIN_WINDOW_H

#include <memory>
#include <QJsonObject>
#include <QMainWindow>
#include "core/app_config.h"
#include "core/beam_frame.h"
#include "core/frame_queue.h"
#include "core/parameter_limits.h"
#include "core/waveform_buffer.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QTabWidget;
class QThread;
class QTimer;

namespace vuprs
{
    namespace client
    {
        class ControlClient;
        class RecordingWriter;
        class WaveformWidget;
        class HemisphereWidget;
        class InferenceBarWidget;

        class MainWindow : public QMainWindow
        {
            Q_OBJECT

        public:
            explicit MainWindow(QWidget *parent = nullptr);
            ~MainWindow() override;

            void connectToServer(const QString &host, quint16 port);
            void openPlayback(const QString &path);
            void setTabIndex(int index);

        protected:
            void closeEvent(QCloseEvent *event) override;

        private slots:
            void onConnectClicked();
            void onMockClicked();
            void onBoardConnectClicked();
            void onConnectionChanged(bool connected, const QString &detail);
            void onCommandFinished(quint32 requestId,
                                   const QString &cmd,
                                   bool ok,
                                   int error,
                                   const QString &info,
                                   const QJsonObject &params);
            void onProtocolError(const QString &detail);
            void onDataTimer();
            void onStatusTimer();

            void onReadParameters();
            void onValidateParameters();
            void onApplyParameters();
            void onApplyBeamformer();
            void onStart();
            void onStop();
            void onReset();
            void onApplyRedirect();
            void onEnableScan();
            void onDisableScan();
            void onRecordClicked();
            void onBrowseRecordingDirectory();
            void onOpenPlayback();
            void onExportPlaybackCsv();
            void onOpenSettings();
            void onPlaybackToggle();
            void onPlaybackClose();
            void onPlaybackTimer();
            void onPlaybackSliderMoved(int ms);
            void onSelectPlaybackRange();
            void onWaveformRangeSelected(qint64 startSample, qint64 endSample);
            void onBeamDirectionPicked(double altDeg, double azDeg);
            void onAutoFollowChanged(bool enabled);
            void onFreezeChanged(bool frozen);
            void onResetView();
            void onWindowSecondsChanged(double seconds);

        private:
            void buildUi();
            void setupNetwork();
            void shutdownNetwork();
            void logMessage(const QString &message);
            void setConnectedUi(bool connected);
            void clearData();
            void updateConnectionStatus(const QString &detail);
            void updateRecordingUi();
            void updateStatusLabels();
            void sendCommand(const QString &cmd, const QJsonObject &params = QJsonObject());
            double currentSampleRateHz() const;
            QJsonObject algorithmParametersFromUi() const;
            QJsonObject recordingMetadata(int index) const;
            QString recordingDirectoryPath() const;
            QString numberString(double value) const;
            void applyAlgorithmParameters(const QJsonObject &params);
            void applyInferenceMetadata(const QJsonObject &params);
            void applyServerLimits();
            void updateParameterMetrics();
            void setSpinValueExpanding(QDoubleSpinBox *spin, double value, const QString &name);
            void applyConfig(const AppConfig &config);
            void stopRecording(const QString &reason, bool notify);
            void maybeFinishRecording();
            bool loadPlaybackFile(const QString &path);
            void enterPlayback();
            void exitPlayback();
            void pausePlayback();
            void seekPlayback(qint64 sample);
            void selectPlaybackRange(qint64 startSample, qint64 endSample);
            void clearPlaybackRange();
            void updatePlaybackUi();
            void updatePlaybackView();
            qint64 playbackDurationMs() const;
            QString formatPlaybackTime(qint64 ms) const;

            AppConfig config_;
            ServerLimits serverLimits_;
            ParameterMetrics parameterMetrics_;
            ControlClient *client_ = nullptr;
            QThread *networkThread_ = nullptr;
            std::shared_ptr<RecordingWriter> recorder_;
            std::shared_ptr<FrameQueue<BeamFrame>> beamQueue_;
            std::shared_ptr<FrameQueue<ScanFrame>> scanQueue_;
            std::shared_ptr<FrameQueue<InferenceFrame>> inferenceQueue_;

            WaveformBuffer waveformBuffer_;
            WaveformBuffer playbackBuffer_;
            bool playbackActive_ = false;
            bool playbackPlaying_ = false;
            qint64 playheadSample_ = 0;
            qint64 playbackAnchorSample_ = 0;
            qint64 playbackAnchorMs_ = 0;
            qint64 playbackTotalSamples_ = 0;
            double playbackSampleRateHz_ = 0.0;
            bool playbackRangeActive_ = false;
            qint64 playbackRangeStart_ = 0;
            qint64 playbackRangeEnd_ = 0;
            QString playbackPath_;
            bool connected_ = false;
            bool recordingUi_ = false;
            bool deviceRunning_ = false;
            bool inferenceReady_ = false;
            bool scanEnabled_ = false;
            bool faultValid_ = false;
            int faultIdentity_ = -1;
            QString beamformerName_;
            QString taskId_;
            int nextRecordingIndex_ = 1;
            QString currentRecordingPath_;
            QString lastRecordingPath_;
            qint64 recordingStartedMs_ = 0;
            qint64 lastBeamSequence_ = -1;
            qint64 lastScanSequence_ = -1;
            quint64 sequenceDropped_ = 0;
            int framesSinceStatus_ = 0;
            qint64 statusWindowStartMs_ = 0;
            ScanFrame latestScan_;
            bool hasScan_ = false;
            qint64 lastHeatmapUpdateMs_ = 0;
            InferenceFrame latestInference_;
            bool hasInference_ = false;
            qint64 lastInferenceSequence_ = -1;
            int inferenceTensorLength_ = 0;
            int lastInferenceWarnLength_ = -1;

            QTimer *dataTimer_ = nullptr;
            QTimer *statusTimer_ = nullptr;
            QTimer *playbackTimer_ = nullptr;

            QLineEdit *hostEdit_ = nullptr;
            QSpinBox *portSpin_ = nullptr;
            QPushButton *connectButton_ = nullptr;
            QPushButton *mockButton_ = nullptr;
            QPushButton *boardButton_ = nullptr;
            QLabel *connectionLabel_ = nullptr;
            QLabel *serverInfoLabel_ = nullptr;

            QDoubleSpinBox *fsSpin_ = nullptr;
            QDoubleSpinBox *waveVelocitySpin_ = nullptr;
            QDoubleSpinBox *lowerFrequencySpin_ = nullptr;
            QDoubleSpinBox *upperFrequencySpin_ = nullptr;
            QDoubleSpinBox *snapshotWindowSpin_ = nullptr;
            QDoubleSpinBox *covarianceIndexSpin_ = nullptr;
            QPushButton *readParametersButton_ = nullptr;
            QPushButton *validateParametersButton_ = nullptr;
            QPushButton *applyParametersButton_ = nullptr;
            QLabel *constraintsLabel_ = nullptr;

            QComboBox *beamformerCombo_ = nullptr;
            QPushButton *applyBeamformerButton_ = nullptr;
            QPushButton *startButton_ = nullptr;
            QPushButton *stopButton_ = nullptr;
            QPushButton *resetButton_ = nullptr;
            QDoubleSpinBox *altSpin_ = nullptr;
            QDoubleSpinBox *azSpin_ = nullptr;
            QPushButton *applyRedirectButton_ = nullptr;

            QSpinBox *scanPointsSpin_ = nullptr;
            QDoubleSpinBox *scanAltMinSpin_ = nullptr;
            QPushButton *enableScanButton_ = nullptr;
            QPushButton *disableScanButton_ = nullptr;
            QLabel *scanStatusLabel_ = nullptr;

            QLineEdit *recordDirectoryEdit_ = nullptr;
            QPushButton *browseRecordButton_ = nullptr;
            QPushButton *recordButton_ = nullptr;
            QPushButton *openPlaybackButton_ = nullptr;
            QLabel *recordStatusLabel_ = nullptr;

            QLabel *runningLabel_ = nullptr;
            QLabel *inferenceLabel_ = nullptr;
            QLabel *faultLabel_ = nullptr;
            QLabel *dataStatusLabel_ = nullptr;

            QCheckBox *autoFollowCheck_ = nullptr;
            QCheckBox *freezeCheck_ = nullptr;
            QDoubleSpinBox *windowSecondsSpin_ = nullptr;
            QWidget *playbackBar_ = nullptr;
            QPushButton *playbackToggleButton_ = nullptr;
            QPushButton *playbackRangeButton_ = nullptr;
            QPushButton *playbackCloseButton_ = nullptr;
            QPushButton *playbackExportButton_ = nullptr;
            QLabel *playbackFileLabel_ = nullptr;
            QSlider *playbackSlider_ = nullptr;
            QLabel *playbackTimeLabel_ = nullptr;
            WaveformWidget *waveformWidget_ = nullptr;
            HemisphereWidget *hemisphereWidget_ = nullptr;
            InferenceBarWidget *inferenceBarWidget_ = nullptr;
            QPlainTextEdit *logView_ = nullptr;
            QTabWidget *tabs_ = nullptr;
        };
    }
}

#endif
