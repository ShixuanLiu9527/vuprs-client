#include "ui/main_window.h"

#include <QDebug>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonValue>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringList>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include "core/inference_utils.h"
#include "net/control_client.h"
#include "record/recording_naming.h"
#include "record/recording_writer.h"
#include "record/vbf_csv.h"
#include "record/vbf_reader.h"
#include "ui/hemisphere_widget.h"
#include "ui/inference_bar_widget.h"
#include "ui/settings_dialog.h"
#include "ui/time_range_dialog.h"
#include "ui/waveform_widget.h"

namespace
{
    QDoubleSpinBox *MakeDoubleSpin(double minimum, double maximum, double value, int decimals,
                                   const QString &suffix = QString())
    {
        auto *spin = new QDoubleSpinBox;
        spin->setRange(minimum, maximum);
        spin->setDecimals(decimals);
        spin->setValue(value);
        spin->setSingleStep(std::max(0.001, (maximum - minimum) / 1000.0));
        if (!suffix.isEmpty())
            spin->setSuffix(suffix);
        return spin;
    }

    QPushButton *MakeButton(const QString &text)
    {
        auto *button = new QPushButton(text);
        button->setMinimumWidth(84);
        return button;
    }

    void SetFieldToolTip(QFormLayout *form, QWidget *field, const QString &text)
    {
        field->setToolTip(text);
        if (QWidget *label = form->labelForField(field))
            label->setToolTip(text);
    }
}

vuprs::client::MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    bool config_created = false;
    QString config_error;
    this->config_ = loadAppConfig(&config_created, &config_error);
    this->taskId_ = makeTaskId(QDateTime::currentSecsSinceEpoch());

    QSettings settings;
    qRegisterMetaType<QJsonObject>("QJsonObject");

    this->beamQueue_ = std::make_shared<FrameQueue<BeamFrame>>(this->config_.beamQueueFrames);
    this->scanQueue_ = std::make_shared<FrameQueue<ScanFrame>>(32);
    this->inferenceQueue_ = std::make_shared<FrameQueue<InferenceFrame>>(16);
    this->recorder_ = std::make_shared<RecordingWriter>();
    this->recorder_->setErrorCallback([this](const QString &error)
                                      {
                                          QMetaObject::invokeMethod(this, [this, error]()
                                                                    {
                                                                        this->logMessage(QStringLiteral("录制错误: ") + error);
                                                                    }, Qt::QueuedConnection);
                                      });

    this->buildUi();
    this->recordDirectoryEdit_->setText(this->recordingDirectoryPath());
    this->updateRecordingUi();
    this->updateParameterMetrics();

    this->waveformBuffer_.configure(10000.0, this->config_.historySeconds);
    this->waveformWidget_->setBuffer(&this->waveformBuffer_);
    this->waveformWidget_->setDefaultWindowSeconds(this->config_.defaultWindowSeconds);
    this->waveformWidget_->setViewWindowSeconds(this->config_.defaultWindowSeconds);
    this->windowSecondsSpin_->setValue(this->config_.defaultWindowSeconds);

    this->dataTimer_ = new QTimer(this);
    this->dataTimer_->setInterval(30);
    connect(this->dataTimer_, &QTimer::timeout, this, &MainWindow::onDataTimer);
    this->dataTimer_->start();

    this->statusWindowStartMs_ = QDateTime::currentMSecsSinceEpoch();
    this->statusTimer_ = new QTimer(this);
    this->statusTimer_->setInterval(500);
    connect(this->statusTimer_, &QTimer::timeout, this, &MainWindow::onStatusTimer);
    this->statusTimer_->start();

    this->playbackTimer_ = new QTimer(this);
    this->playbackTimer_->setInterval(30);
    connect(this->playbackTimer_, &QTimer::timeout, this, &MainWindow::onPlaybackTimer);

    this->setupNetwork();
    this->setConnectedUi(false);
    this->updateConnectionStatus(QStringLiteral("未连接"));
    this->updateStatusLabels();
    this->logMessage(QStringLiteral("VUPRS 上位机已启动；协议版本 1，单连接控制 + 数据推送。"));
    this->logMessage(QStringLiteral("任务ID: task%1，录制目录: %2")
                         .arg(this->taskId_, this->recordingDirectoryPath()));
    this->logMessage(QStringLiteral("配置文件: %1%2")
                         .arg(appConfigFilePath(),
                              config_created ? QStringLiteral("（已创建）") : QString()));
    if (!config_error.isEmpty())
        this->logMessage(QStringLiteral("配置读取: ") + config_error);
    this->resize(1220, 820);

    const QByteArray geometry = settings.value(QStringLiteral("window/geometry")).toByteArray();
    if (!geometry.isEmpty())
        this->restoreGeometry(geometry);
}

vuprs::client::MainWindow::~MainWindow()
{
    if (this->recorder_)
    {
        this->recorder_->stop();
        this->recorder_->setErrorCallback(nullptr);
    }
    this->shutdownNetwork();
}

void vuprs::client::MainWindow::buildUi()
{
    this->setWindowTitle(QStringLiteral("VUPRS 上位机"));
    auto *central = new QWidget(this);
    auto *central_layout = new QHBoxLayout(central);
    central_layout->setContentsMargins(8, 8, 8, 8);

    auto *left = new QWidget;
    auto *left_layout = new QVBoxLayout(left);
    left_layout->setContentsMargins(0, 0, 0, 0);
    left_layout->setSpacing(8);

    /* Connection */
    auto *connection_group = new QGroupBox(QStringLiteral("连接"));
    auto *connection_form = new QFormLayout(connection_group);
    this->hostEdit_ = new QLineEdit(this->config_.host);
    this->portSpin_ = new QSpinBox;
    this->portSpin_->setRange(1, 65535);
    this->portSpin_->setValue(this->config_.port);
    connection_form->addRow(QStringLiteral("服务器地址"), this->hostEdit_);
    connection_form->addRow(QStringLiteral("端口"), this->portSpin_);
    auto *connection_buttons = new QHBoxLayout;
    this->connectButton_ = MakeButton(QStringLiteral("连接"));
    this->mockButton_ = MakeButton(QStringLiteral("本地 Mock"));
    this->boardButton_ = MakeButton(QStringLiteral("直连板卡"));
    connection_buttons->addWidget(this->connectButton_);
    connection_buttons->addWidget(this->mockButton_);
    connection_buttons->addWidget(this->boardButton_);
    connection_form->addRow(connection_buttons);
    this->connectionLabel_ = new QLabel(QStringLiteral("未连接"));
    this->serverInfoLabel_ = new QLabel(QStringLiteral("协议版本 1"));
    this->serverInfoLabel_->setWordWrap(true);
    connection_form->addRow(this->connectionLabel_);
    connection_form->addRow(this->serverInfoLabel_);
    SetFieldToolTip(connection_form, this->hostEdit_,
                    QStringLiteral("服务器 IP 地址或主机名。DHCP 直连默认 192.168.50.10; 修改后点击“连接”生效。"));
    SetFieldToolTip(connection_form, this->portSpin_,
                    QStringLiteral("服务器 TCP 控制/数据端口，默认 8080。"));
    this->boardButton_->setToolTip(
        QStringLiteral("填入 DHCP 直连默认地址 192.168.50.10:8080 并立即连接。"
                       "Windows 以太网适配器需保持自动获得 IP。"));
    this->mockButton_->setToolTip(
        QStringLiteral("填入本地 mock 地址 127.0.0.1:18080 并立即连接。"));
    left_layout->addWidget(connection_group);

    /* Algorithm parameters */
    auto *algorithm_group = new QGroupBox(QStringLiteral("算法参数"));
    auto *algorithm_form = new QFormLayout(algorithm_group);
    this->fsSpin_ = MakeDoubleSpin(10.0, 120000.0, 10000.0, 3, QStringLiteral(" Hz"));
    this->waveVelocitySpin_ = MakeDoubleSpin(1.0, 100000.0, 346.0, 3, QStringLiteral(" m/s"));
    this->lowerFrequencySpin_ = MakeDoubleSpin(10.0, 60000.0, 500.0, 3, QStringLiteral(" Hz"));
    this->upperFrequencySpin_ = MakeDoubleSpin(10.0, 60000.0, 3000.0, 3, QStringLiteral(" Hz"));
    this->snapshotWindowSpin_ = MakeDoubleSpin(1.0, 1000000.0, 200.0, 3);
    this->covarianceIndexSpin_ = MakeDoubleSpin(0.0, 0.999, 0.8, 3);
    algorithm_form->addRow(QStringLiteral("采样率 fs"), this->fsSpin_);
    algorithm_form->addRow(QStringLiteral("声速"), this->waveVelocitySpin_);
    algorithm_form->addRow(QStringLiteral("通带下限"), this->lowerFrequencySpin_);
    algorithm_form->addRow(QStringLiteral("通带上限"), this->upperFrequencySpin_);
    algorithm_form->addRow(QStringLiteral("快照窗口"), this->snapshotWindowSpin_);
    algorithm_form->addRow(QStringLiteral("协方差平均"), this->covarianceIndexSpin_);
    auto *algorithm_buttons = new QHBoxLayout;
    this->readParametersButton_ = MakeButton(QStringLiteral("读取"));
    this->validateParametersButton_ = MakeButton(QStringLiteral("检查"));
    this->applyParametersButton_ = MakeButton(QStringLiteral("应用"));
    algorithm_buttons->addWidget(this->readParametersButton_);
    algorithm_buttons->addWidget(this->validateParametersButton_);
    algorithm_buttons->addWidget(this->applyParametersButton_);
    algorithm_form->addRow(algorithm_buttons);
    auto *parameter_warning = new QLabel(QStringLiteral("应用算法参数会重启波束形成器。"));
    parameter_warning->setWordWrap(true);
    algorithm_form->addRow(parameter_warning);
    this->constraintsLabel_ = new QLabel;
    this->constraintsLabel_->setWordWrap(true);
    this->constraintsLabel_->setTextFormat(Qt::PlainText);
    this->constraintsLabel_->setStyleSheet(QStringLiteral("color: #4a5560;"));
    algorithm_form->addRow(this->constraintsLabel_);
    SetFieldToolTip(algorithm_form, this->fsSpin_,
                    QStringLiteral("波束形成器采样率，单位 Hz。范围由服务端能力决定；"
                                   "通带上限必须低于 fs/2。"));
    SetFieldToolTip(algorithm_form, this->waveVelocitySpin_,
                    QStringLiteral("介质声速，单位 m/s，用于波束形成和空间扫描。"
                                   "上限由服务端能力决定。"));
    SetFieldToolTip(algorithm_form, this->lowerFrequencySpin_,
                    QStringLiteral("带通滤波下限，单位 Hz。要求 0 < 下限 < 上限 < fs/2。"));
    SetFieldToolTip(algorithm_form, this->upperFrequencySpin_,
                    QStringLiteral("带通滤波上限，单位 Hz。要求 0 < 下限 < 上限 < fs/2。"));
    SetFieldToolTip(algorithm_form, this->snapshotWindowSpin_,
                    QStringLiteral("协方差估计的等效快照数。实际收敛时间还取决于 fs 和快照周期。"));
    SetFieldToolTip(algorithm_form, this->covarianceIndexSpin_,
                    QStringLiteral("协方差矩阵平均系数，范围 [0, 1)。"));
    this->validateParametersButton_->setToolTip(
        QStringLiteral("请求服务端检查当前参数，不重启波束形成器；"
                       "会返回频率分辨率、通带频点数和警告。"));
    left_layout->addWidget(algorithm_group);

    /* Beam control */
    auto *beam_group = new QGroupBox(QStringLiteral("波束控制"));
    auto *beam_form = new QFormLayout(beam_group);
    this->beamformerCombo_ = new QComboBox;
    this->beamformerCombo_->addItems({QStringLiteral("dcrcb"), QStringLiteral("mvdr"), QStringLiteral("cbf")});
    beam_form->addRow(QStringLiteral("波束算法"), this->beamformerCombo_);
    this->applyBeamformerButton_ = MakeButton(QStringLiteral("应用算法"));
    beam_form->addRow(this->applyBeamformerButton_);
    auto *run_buttons = new QHBoxLayout;
    this->startButton_ = MakeButton(QStringLiteral("启动"));
    this->stopButton_ = MakeButton(QStringLiteral("停止"));
    this->resetButton_ = MakeButton(QStringLiteral("复位"));
    run_buttons->addWidget(this->startButton_);
    run_buttons->addWidget(this->stopButton_);
    run_buttons->addWidget(this->resetButton_);
    beam_form->addRow(run_buttons);
    this->altSpin_ = MakeDoubleSpin(-90.0, 90.0, 90.0, 2, QStringLiteral(" °"));
    this->azSpin_ = MakeDoubleSpin(-180.0, 180.0, 0.0, 2, QStringLiteral(" °"));
    beam_form->addRow(QStringLiteral("指向 alt"), this->altSpin_);
    beam_form->addRow(QStringLiteral("指向 az"), this->azSpin_);
    this->applyRedirectButton_ = MakeButton(QStringLiteral("应用指向"));
    beam_form->addRow(this->applyRedirectButton_);
    this->beamformerCombo_->setToolTip(
        QStringLiteral("波束形成算法：dcrcb、mvdr、cbf。切换会重启波束形成器。"));
    if (QWidget *label = beam_form->labelForField(this->beamformerCombo_))
        label->setToolTip(this->beamformerCombo_->toolTip());
    SetFieldToolTip(beam_form, this->altSpin_,
                    QStringLiteral("波束期望指向的俯仰角，范围 -90°..90°；90° 表示天顶。"));
    SetFieldToolTip(beam_form, this->azSpin_,
                    QStringLiteral("波束期望指向的方位角，范围 -180°..180°。"));
    left_layout->addWidget(beam_group);

    /* Scan */
    auto *scan_group = new QGroupBox(QStringLiteral("空间扫描"));
    auto *scan_form = new QFormLayout(scan_group);
    this->scanPointsSpin_ = new QSpinBox;
    this->scanPointsSpin_->setRange(1, 10000);
    this->scanPointsSpin_->setValue(100);
    this->scanAltMinSpin_ = MakeDoubleSpin(10.01, 90.0, 15.0, 2, QStringLiteral(" °"));
    scan_form->addRow(QStringLiteral("points (half)"), this->scanPointsSpin_);
    scan_form->addRow(QStringLiteral("alt_min"), this->scanAltMinSpin_);
    auto *scan_buttons = new QHBoxLayout;
    this->enableScanButton_ = MakeButton(QStringLiteral("开启扫描"));
    this->disableScanButton_ = MakeButton(QStringLiteral("关闭扫描"));
    scan_buttons->addWidget(this->enableScanButton_);
    scan_buttons->addWidget(this->disableScanButton_);
    scan_form->addRow(scan_buttons);
    this->scanStatusLabel_ = new QLabel(QStringLiteral("扫描: 未开启"));
    this->scanStatusLabel_->setWordWrap(true);
    scan_form->addRow(this->scanStatusLabel_);
    SetFieldToolTip(scan_form, this->scanPointsSpin_,
                    QStringLiteral("斐波那契半球网格的 points(half) 参数，决定扫描点总数。"));
    SetFieldToolTip(scan_form, this->scanAltMinSpin_,
                    QStringLiteral("扫描半球最低俯仰角，必须大于 10°；低于该角度的网格点不生成。"));
    left_layout->addWidget(scan_group);

    /* Recording */
    auto *record_group = new QGroupBox(QStringLiteral("录制"));
    auto *record_layout = new QVBoxLayout(record_group);
    auto *record_dir_label = new QLabel(QStringLiteral("录制目录"));
    record_layout->addWidget(record_dir_label);
    this->recordDirectoryEdit_ = new QLineEdit(this->config_.recordingDirectory);
    this->recordDirectoryEdit_->setToolTip(QStringLiteral("每次录制会在此目录新建 task<任务ID>_id<序号>.vbf"));
    record_layout->addWidget(this->recordDirectoryEdit_);
    auto *record_buttons = new QHBoxLayout;
    this->browseRecordButton_ = MakeButton(QStringLiteral("选择目录..."));
    this->browseRecordButton_->setToolTip(QStringLiteral("选择录制文件的保存目录，选择后立即写入配置。"));
    this->recordButton_ = MakeButton(QStringLiteral("开始录制"));
    this->recordButton_->setToolTip(
        QStringLiteral("开始/停止录制 BF 数据；文件名为 task<任务ID>_id<序号>.vbf。"));
    record_buttons->addWidget(this->browseRecordButton_);
    record_buttons->addWidget(this->recordButton_);
    record_layout->addLayout(record_buttons);
    this->recordStatusLabel_ = new QLabel(QStringLiteral("任务ID: task%1\n未录制").arg(this->taskId_));
    this->recordStatusLabel_->setWordWrap(true);
    record_layout->addWidget(this->recordStatusLabel_);
    auto *file_buttons = new QHBoxLayout;
    this->openPlaybackButton_ = MakeButton(QStringLiteral("回放..."));
    this->openPlaybackButton_->setToolTip(QStringLiteral("载入 .vbf 录制文件并在波形页回放"));
    file_buttons->addWidget(this->openPlaybackButton_);
    record_layout->addLayout(file_buttons);
    left_layout->addWidget(record_group);

    /* Status */
    auto *status_group = new QGroupBox(QStringLiteral("状态"));
    auto *status_layout = new QVBoxLayout(status_group);
    this->runningLabel_ = new QLabel(QStringLiteral("运行: 否"));
    this->inferenceLabel_ = new QLabel(QStringLiteral("推理: 未就绪"));
    this->faultLabel_ = new QLabel(QStringLiteral("故障ID: -"));
    this->dataStatusLabel_ = new QLabel(QStringLiteral("接收: 0.0 fps  队列: 0  丢帧: 0"));
    this->dataStatusLabel_->setWordWrap(true);
    status_layout->addWidget(this->runningLabel_);
    status_layout->addWidget(this->inferenceLabel_);
    status_layout->addWidget(this->faultLabel_);
    status_layout->addWidget(this->dataStatusLabel_);
    left_layout->addWidget(status_group);
    left_layout->addStretch(1);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(left);
    scroll->setMinimumWidth(350);
    scroll->setMaximumWidth(430);

    /* Right tabs */
    this->tabs_ = new QTabWidget;

    auto *wave_page = new QWidget;
    auto *wave_layout = new QVBoxLayout(wave_page);
    auto *wave_toolbar = new QHBoxLayout;
    this->autoFollowCheck_ = new QCheckBox(QStringLiteral("自动跟随"));
    this->autoFollowCheck_->setChecked(true);
    this->freezeCheck_ = new QCheckBox(QStringLiteral("冻结"));
    auto *window_label = new QLabel(QStringLiteral("窗口"));
    this->windowSecondsSpin_ = MakeDoubleSpin(0.5, 120.0, this->config_.defaultWindowSeconds, 1, QStringLiteral(" s"));
    auto *reset_view_button = MakeButton(QStringLiteral("重置视图"));
    this->autoFollowCheck_->setToolTip(QStringLiteral("实时波形自动滚动到最新数据；回放时该选项禁用。"));
    this->freezeCheck_->setToolTip(QStringLiteral("冻结当前视图，后台仍继续接收数据；回放时冻结自动跟随。"));
    this->windowSecondsSpin_->setToolTip(
        QStringLiteral("波形显示时间窗长度；回放时改变窗口会取消时间段选择。"));
    reset_view_button->setToolTip(
        QStringLiteral("实时模式恢复自动跟随；回放模式恢复完整录制视图并清除已选时间段。"));
    wave_toolbar->addWidget(this->autoFollowCheck_);
    wave_toolbar->addWidget(this->freezeCheck_);
    wave_toolbar->addSpacing(16);
    wave_toolbar->addWidget(window_label);
    wave_toolbar->addWidget(this->windowSecondsSpin_);
    wave_toolbar->addWidget(reset_view_button);
    wave_toolbar->addStretch(1);
    wave_layout->addLayout(wave_toolbar);

    this->playbackBar_ = new QWidget;
    auto *playback_layout = new QHBoxLayout(this->playbackBar_);
    playback_layout->setContentsMargins(0, 0, 0, 0);
    this->playbackToggleButton_ = MakeButton(QStringLiteral("播放"));
    this->playbackToggleButton_->setToolTip(
        QStringLiteral("播放/暂停当前回放；已选择时间段时只播放该段。"));
    this->playbackRangeButton_ = MakeButton(QStringLiteral("时间段..."));
    this->playbackRangeButton_->setToolTip(QStringLiteral("输入开始/结束时刻，或直接在波形上拖动选择"));
    this->playbackCloseButton_ = MakeButton(QStringLiteral("退出回放"));
    this->playbackCloseButton_->setToolTip(QStringLiteral("退出回放并返回实时波形。"));
    this->playbackExportButton_ = MakeButton(QStringLiteral("导出 CSV..."));
    this->playbackExportButton_->setToolTip(QStringLiteral("将当前回放的 .vbf 逐采样导出为 CSV"));
    this->playbackFileLabel_ = new QLabel;
    this->playbackFileLabel_->setMinimumWidth(0);
    this->playbackFileLabel_->setMaximumWidth(260);
    this->playbackFileLabel_->setToolTip(QStringLiteral("当前打开的 .vbf 回放文件。"));
    this->playbackSlider_ = new QSlider(Qt::Horizontal);
    this->playbackSlider_->setRange(0, 0);
    this->playbackSlider_->setToolTip(
        QStringLiteral("拖动定位回放位置；移到已选时间段之外会清除时间段。"));
    this->playbackTimeLabel_ = new QLabel(QStringLiteral("00:00.000 / 00:00.000"));
    this->playbackTimeLabel_->setToolTip(QStringLiteral("当前回放位置 / 录制总时长。"));
    playback_layout->addWidget(this->playbackFileLabel_);
    playback_layout->addWidget(this->playbackToggleButton_);
    playback_layout->addWidget(this->playbackRangeButton_);
    playback_layout->addWidget(this->playbackCloseButton_);
    playback_layout->addWidget(this->playbackExportButton_);
    playback_layout->addWidget(this->playbackSlider_, 1);
    playback_layout->addWidget(this->playbackTimeLabel_);
    this->playbackBar_->setVisible(false);
    wave_layout->addWidget(this->playbackBar_);

    this->waveformWidget_ = new WaveformWidget;
    wave_layout->addWidget(this->waveformWidget_, 1);
    this->tabs_->addTab(wave_page, QStringLiteral("BF 波形"));

    auto *heat_page = new QWidget;
    auto *heat_layout = new QVBoxLayout(heat_page);
    this->hemisphereWidget_ = new HemisphereWidget;
    heat_layout->addWidget(this->hemisphereWidget_, 3);
    this->inferenceBarWidget_ = new InferenceBarWidget;
    auto *inference_scroll = new QScrollArea;
    inference_scroll->setWidgetResizable(true);
    inference_scroll->setMinimumHeight(180);
    inference_scroll->setMaximumHeight(230);
    inference_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    inference_scroll->setWidget(this->inferenceBarWidget_);
    heat_layout->addWidget(inference_scroll, 1);
    auto *heat_hint = new QLabel(QStringLiteral("鼠标悬停查看 alt/az/dB；双击热力图可将位置填入波束指向。下方为推理 tensor 的 0-100% 柱状图。"));
    heat_hint->setWordWrap(true);
    heat_layout->addWidget(heat_hint);
    this->tabs_->addTab(heat_page, QStringLiteral("半球能量"));

    this->logView_ = new QPlainTextEdit;
    this->logView_->setReadOnly(true);
    this->logView_->setMaximumBlockCount(5000);
    this->logView_->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont log_font(QStringLiteral("Consolas"));
    log_font.setStyleHint(QFont::Monospace);
    this->logView_->setFont(log_font);
    this->tabs_->addTab(this->logView_, QStringLiteral("协议日志"));

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(scroll);
    splitter->addWidget(this->tabs_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({380, 840});
    central_layout->addWidget(splitter);
    this->setCentralWidget(central);

    connect(this->connectButton_, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(this->mockButton_, &QPushButton::clicked, this, &MainWindow::onMockClicked);
    connect(this->boardButton_, &QPushButton::clicked, this, &MainWindow::onBoardConnectClicked);
    connect(this->readParametersButton_, &QPushButton::clicked, this, &MainWindow::onReadParameters);
    connect(this->validateParametersButton_, &QPushButton::clicked, this, &MainWindow::onValidateParameters);
    connect(this->applyParametersButton_, &QPushButton::clicked, this, &MainWindow::onApplyParameters);
    connect(this->fsSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { this->updateParameterMetrics(); });
    connect(this->lowerFrequencySpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { this->updateParameterMetrics(); });
    connect(this->upperFrequencySpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { this->updateParameterMetrics(); });
    connect(this->snapshotWindowSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { this->updateParameterMetrics(); });
    connect(this->applyBeamformerButton_, &QPushButton::clicked, this, &MainWindow::onApplyBeamformer);
    connect(this->startButton_, &QPushButton::clicked, this, &MainWindow::onStart);
    connect(this->stopButton_, &QPushButton::clicked, this, &MainWindow::onStop);
    connect(this->resetButton_, &QPushButton::clicked, this, &MainWindow::onReset);
    connect(this->applyRedirectButton_, &QPushButton::clicked, this, &MainWindow::onApplyRedirect);
    connect(this->enableScanButton_, &QPushButton::clicked, this, &MainWindow::onEnableScan);
    connect(this->disableScanButton_, &QPushButton::clicked, this, &MainWindow::onDisableScan);
    connect(this->browseRecordButton_, &QPushButton::clicked, this, &MainWindow::onBrowseRecordingDirectory);
    connect(this->recordButton_, &QPushButton::clicked, this, &MainWindow::onRecordClicked);
    connect(this->openPlaybackButton_, &QPushButton::clicked, this, &MainWindow::onOpenPlayback);
    connect(this->playbackToggleButton_, &QPushButton::clicked, this, &MainWindow::onPlaybackToggle);
    connect(this->playbackRangeButton_, &QPushButton::clicked, this, &MainWindow::onSelectPlaybackRange);
    connect(this->playbackCloseButton_, &QPushButton::clicked, this, &MainWindow::onPlaybackClose);
    connect(this->playbackExportButton_, &QPushButton::clicked, this, &MainWindow::onExportPlaybackCsv);
    connect(this->waveformWidget_, &WaveformWidget::rangeSelected, this, &MainWindow::onWaveformRangeSelected);
    connect(this->waveformWidget_, &WaveformWidget::selectionStarted, this, [this]()
            { this->pausePlayback(); });
    connect(this->waveformWidget_, &WaveformWidget::resetViewRequested, this, &MainWindow::onResetView);
    connect(this->playbackSlider_, &QSlider::valueChanged, this, &MainWindow::onPlaybackSliderMoved);
    connect(this->autoFollowCheck_, &QCheckBox::toggled, this, &MainWindow::onAutoFollowChanged);
    connect(this->freezeCheck_, &QCheckBox::toggled, this, &MainWindow::onFreezeChanged);
    connect(this->windowSecondsSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &MainWindow::onWindowSecondsChanged);
    connect(reset_view_button, &QPushButton::clicked, this, &MainWindow::onResetView);
    connect(this->waveformWidget_, &WaveformWidget::autoFollowChanged, this, [this](bool enabled)
            {
                const QSignalBlocker blocker(this->autoFollowCheck_);
                this->autoFollowCheck_->setChecked(enabled);
            });
    connect(this->hemisphereWidget_, &HemisphereWidget::beamDirectionPicked, this, &MainWindow::onBeamDirectionPicked);
    connect(this->altSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double)
            { this->hemisphereWidget_->setBeamDirection(this->altSpin_->value(), this->azSpin_->value()); });
    connect(this->azSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double)
            { this->hemisphereWidget_->setBeamDirection(this->altSpin_->value(), this->azSpin_->value()); });

    QMenu *file_menu = this->menuBar()->addMenu(QStringLiteral("文件"));
    QAction *open_playback_action = file_menu->addAction(QStringLiteral("打开回放..."));
    open_playback_action->setShortcut(QKeySequence::Open);
    connect(open_playback_action, &QAction::triggered, this, &MainWindow::onOpenPlayback);
    file_menu->addSeparator();
    QAction *quit_action = file_menu->addAction(QStringLiteral("退出"));
    quit_action->setShortcut(QKeySequence::Quit);
    connect(quit_action, &QAction::triggered, this, [this]()
            { this->close(); });

    QAction *settings_action = this->menuBar()->addAction(QStringLiteral("设置"));
    settings_action->setToolTip(QStringLiteral("软件设置"));
    settings_action->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
    connect(settings_action, &QAction::triggered, this, &MainWindow::onOpenSettings);

    this->menuBar()->setStyleSheet(QStringLiteral(
        "QMenuBar { background: #eef1f4; border-bottom: 1px solid #b8c0c8; padding: 2px; }"
        "QMenuBar::item { background: #fbfcfd; border: 1px solid #9aa6b2;"
        " border-radius: 3px; padding: 3px 10px; margin: 1px 2px; }"
        "QMenuBar::item:selected { background: #dce8f5; border-color: #4a7bb5; }"
        "QMenuBar::item:pressed { background: #c8d8ea; }"));
}

void vuprs::client::MainWindow::setupNetwork()
{
    this->networkThread_ = new QThread(this);
    this->client_ = new ControlClient(this->beamQueue_, this->scanQueue_, this->inferenceQueue_,
                                      this->config_.commandTimeoutMs);
    this->client_->setRecorder(this->recorder_);
    this->client_->moveToThread(this->networkThread_);

    connect(this->networkThread_, &QThread::started, this->client_, &ControlClient::initialize);
    connect(this->networkThread_, &QThread::finished, this->client_, &QObject::deleteLater);
    connect(this->client_, &ControlClient::connectionChanged, this, &MainWindow::onConnectionChanged);
    connect(this->client_, &ControlClient::commandFinished, this, &MainWindow::onCommandFinished);
    connect(this->client_, &ControlClient::protocolError, this, &MainWindow::onProtocolError);
    this->networkThread_->start();
}

void vuprs::client::MainWindow::shutdownNetwork()
{
    if (this->client_ != nullptr && this->networkThread_ != nullptr && this->networkThread_->isRunning())
    {
        QMetaObject::invokeMethod(this->client_, "doShutdown", Qt::BlockingQueuedConnection);
    }
    if (this->networkThread_ != nullptr)
    {
        this->networkThread_->quit();
        this->networkThread_->wait(3000);
        delete this->networkThread_;
        this->networkThread_ = nullptr;
        this->client_ = nullptr;
    }
}

void vuprs::client::MainWindow::closeEvent(QCloseEvent *event)
{
    if (this->playbackTimer_ != nullptr)
        this->playbackTimer_->stop();
    if (this->recordingUi_ || (this->recorder_ && this->recorder_->isActive()))
        this->stopRecording(QStringLiteral("程序关闭"), false);

    this->config_.host = this->hostEdit_->text().trimmed();
    this->config_.port = static_cast<quint16>(this->portSpin_->value());
    this->config_.recordingDirectory = this->recordingDirectoryPath();
    QString config_error;
    if (!saveAppConfig(this->config_, &config_error))
        this->logMessage(QStringLiteral("保存配置失败: ") + config_error);

    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), this->saveGeometry());
    this->shutdownNetwork();
    QMainWindow::closeEvent(event);
}

void vuprs::client::MainWindow::logMessage(const QString &message)
{
    if (this->logView_ == nullptr)
        return;
    const QString line = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")) +
                         QStringLiteral("  ") + message;
    qInfo().noquote() << line;
    this->logView_->appendPlainText(line);
}

void vuprs::client::MainWindow::sendCommand(const QString &cmd, const QJsonObject &params)
{
    if (this->client_ == nullptr)
        return;
    const QByteArray compact = QJsonDocument(params).toJson(QJsonDocument::Compact);
    this->logMessage(QStringLiteral("→ %1 %2").arg(cmd, QString::fromUtf8(compact)));
    this->client_->sendCommand(cmd, params);
}

void vuprs::client::MainWindow::connectToServer(const QString &host, quint16 port)
{
    this->hostEdit_->setText(host);
    this->portSpin_->setValue(port);
    if (!this->connected_)
        this->onConnectClicked();
}

void vuprs::client::MainWindow::openPlayback(const QString &path)
{
    if (this->loadPlaybackFile(path))
        this->onPlaybackToggle();
}

void vuprs::client::MainWindow::setTabIndex(int index)
{
    if (index >= 0 && index < this->tabs_->count())
        this->tabs_->setCurrentIndex(index);
}

void vuprs::client::MainWindow::onConnectClicked()
{
    if (this->connected_)
    {
        this->client_->disconnectFromServer();
        return;
    }
    const QString host = this->hostEdit_->text().trimmed();
    if (host.isEmpty())
    {
        this->logMessage(QStringLiteral("服务器地址不能为空"));
        return;
    }
    this->logMessage(QStringLiteral("连接 %1:%2 ...").arg(host).arg(this->portSpin_->value()));
    this->client_->connectToServer(host, static_cast<quint16>(this->portSpin_->value()),
                                   this->config_.connectTimeoutMs);
}

void vuprs::client::MainWindow::onMockClicked()
{
    this->hostEdit_->setText(QStringLiteral("127.0.0.1"));
    this->portSpin_->setValue(18080);
    this->logMessage(QStringLiteral("已填入本地 mock 地址 127.0.0.1:18080"));
    this->onConnectClicked();
}

void vuprs::client::MainWindow::onBoardConnectClicked()
{
    this->hostEdit_->setText(defaultServerHost());
    this->portSpin_->setValue(defaultServerPort());
    this->logMessage(QStringLiteral("已填入 DHCP 直连板卡地址 %1:%2")
                         .arg(defaultServerHost())
                         .arg(defaultServerPort()));
    this->onConnectClicked();
}

void vuprs::client::MainWindow::onConnectionChanged(bool connected, const QString &detail)
{
    this->connected_ = connected;
    this->setConnectedUi(connected);
    this->updateConnectionStatus(detail);
    this->logMessage(QStringLiteral("连接状态: ") + detail);
    if (connected)
    {
        this->clearData();
        this->serverInfoLabel_->setText(QStringLiteral("协议版本 1"));
        this->sendCommand(QStringLiteral("ack"));
        this->sendCommand(QStringLiteral("read_algorithm_parameters"));
        this->sendCommand(QStringLiteral("get_status"));
    }
    else
    {
        this->deviceRunning_ = false;
        this->inferenceReady_ = false;
        this->scanEnabled_ = false;
        this->faultValid_ = false;
        this->faultIdentity_ = -1;
        this->latestInference_ = InferenceFrame();
        this->hasInference_ = false;
        this->lastInferenceSequence_ = -1;
        this->inferenceTensorLength_ = 0;
        if (this->inferenceQueue_ != nullptr)
            this->inferenceQueue_->clear();
        if (this->inferenceBarWidget_ != nullptr)
        {
            this->inferenceBarWidget_->clear();
            this->inferenceBarWidget_->setExpectedLength(0);
        }
        if (this->recordingUi_)
            this->stopRecording(QStringLiteral("连接断开"), false);
        this->updateStatusLabels();
    }
}

void vuprs::client::MainWindow::onCommandFinished(quint32 requestId,
                                                   const QString &cmd,
                                                   bool ok,
                                                   int error,
                                                   const QString &info,
                                                   const QJsonObject &params)
{
    const QString detail = QStringLiteral("%1  cmd=%2  id=%3  error=%4  %5")
                               .arg(ok ? QStringLiteral("OK") : QStringLiteral("FAILED"))
                               .arg(cmd)
                               .arg(requestId)
                               .arg(error)
                               .arg(info);
    this->logMessage(QStringLiteral("← ") + detail);

    if (!ok)
    {
        this->statusBar()->showMessage(QStringLiteral("%1 失败: %2").arg(cmd, info), 4000);
        if (cmd == QStringLiteral("change_algorithm_parameters"))
        {
            this->logMessage(QStringLiteral("参数应用失败, 重新读取服务端参数"));
            this->sendCommand(QStringLiteral("read_algorithm_parameters"));
        }
        else if (cmd == QStringLiteral("change_beamformer"))
        {
            this->sendCommand(QStringLiteral("get_status"));
        }
        return;
    }

    if (cmd == QStringLiteral("read_algorithm_parameters"))
    {
        this->applyAlgorithmParameters(params);
    }
    else if (cmd == QStringLiteral("validate_config"))
    {
        const auto read_number = [&params](const char *key) -> double
        {
            const QJsonValue value = params.value(QString::fromLatin1(key));
            if (value.isDouble())
                return value.toDouble();
            if (value.isString())
            {
                bool ok = false;
                const double parsed = value.toString().toDouble(&ok);
                return ok ? parsed : 0.0;
            }
            return 0.0;
        };
        const double df = read_number("frequency_resolution_hz");
        const double bins = read_number("band_bins");
        const double inference_samples = read_number("inference_frame_samples");
        const QString warnings = params.value(QStringLiteral("warnings")).toString();
        this->logMessage(QStringLiteral("参数检查: df=%1 Hz, 通带约 %2 个频点, 推理帧 %3 点")
                             .arg(df, 0, 'f', 3)
                             .arg(bins, 0, 'f', 0)
                             .arg(inference_samples, 0, 'f', 0));
        if (warnings.isEmpty())
        {
            this->statusBar()->showMessage(QStringLiteral("参数检查通过"), 5000);
        }
        else
        {
            this->logMessage(QStringLiteral("参数检查警告: ") + warnings);
            this->statusBar()->showMessage(QStringLiteral("参数检查通过, 但有警告"), 5000);
        }
    }
    else if (cmd == QStringLiteral("get_status"))
    {
        const QString running = params.value(QStringLiteral("running")).toString();
        const QString inference = params.value(QStringLiteral("inference_ready")).toString();
        const QString scan = params.value(QStringLiteral("scan_enabled")).toString();
        this->deviceRunning_ = running == QStringLiteral("1");
        this->inferenceReady_ = inference == QStringLiteral("1");
        this->scanEnabled_ = scan == QStringLiteral("1");
        this->applyInferenceMetadata(params);
        this->beamformerName_ = params.value(QStringLiteral("beamformer")).toString();
        bool scan_points_ok = false;
        const int scan_points = params.value(QStringLiteral("scan_points")).toString().toInt(&scan_points_ok);
        if (scan_points_ok && scan_points > 0)
            this->scanPointsSpin_->setValue(scan_points);
        bool scan_alt_ok = false;
        const double scan_alt = params.value(QStringLiteral("scan_alt_min")).toString().toDouble(&scan_alt_ok);
        if (scan_alt_ok)
            this->setSpinValueExpanding(this->scanAltMinSpin_, scan_alt, QStringLiteral("alt_min"));
        if (!this->beamformerName_.isEmpty())
        {
            const int index = this->beamformerCombo_->findText(this->beamformerName_);
            if (index >= 0)
                this->beamformerCombo_->setCurrentIndex(index);
        }
        this->updateStatusLabels();
    }
    else if (cmd == QStringLiteral("ack"))
    {
        const QString protocol = params.value(QStringLiteral("protocol_version")).toString();
        const QString version = params.value(QStringLiteral("server_version")).toString();
        this->serverInfoLabel_->setText(QStringLiteral("服务器: %1  协议: %2").arg(version, protocol));
        this->applyInferenceMetadata(params);
        const QJsonObject limits = params.value(QStringLiteral("limits")).toObject();
        if (!limits.isEmpty())
        {
            this->serverLimits_ = parseServerLimits(limits);
            this->applyServerLimits();
            this->logMessage(QStringLiteral("服务端能力: fs %1..%2 Hz, FFT %3 点, FIR %4, DMA %5 点, 最大扫描点 %6")
                                 .arg(this->serverLimits_.minSamplingFrequencyHz, 0, 'f', 1)
                                 .arg(this->serverLimits_.maxSamplingFrequencyHz, 0, 'f', 1)
                                 .arg(this->serverLimits_.fftPoints)
                                 .arg(this->serverLimits_.firLength)
                                 .arg(this->serverLimits_.dmaBufferPoints)
                                 .arg(this->serverLimits_.maxScanPoints));
        }
        else
        {
            this->serverLimits_ = ServerLimits();
            this->applyServerLimits();
            this->logMessage(QStringLiteral("服务端未提供能力限制, 使用默认参数范围"));
        }
    }

    if (cmd == QStringLiteral("start") || cmd == QStringLiteral("stop") ||
        cmd == QStringLiteral("reset") || cmd == QStringLiteral("change_beamformer") ||
        cmd == QStringLiteral("redirect") || cmd == QStringLiteral("enable_scan") ||
        cmd == QStringLiteral("disable_scan"))
    {
        this->sendCommand(QStringLiteral("get_status"));
    }
    else if (cmd == QStringLiteral("change_algorithm_parameters"))
    {
        this->sendCommand(QStringLiteral("read_algorithm_parameters"));
        this->sendCommand(QStringLiteral("get_status"));
    }
}

void vuprs::client::MainWindow::onProtocolError(const QString &detail)
{
    this->logMessage(QStringLiteral("协议/网络错误: ") + detail);
    this->statusBar()->showMessage(detail, 5000);
}

void vuprs::client::MainWindow::onDataTimer()
{
    QVector<BeamFrame> frames;
    const int count = this->beamQueue_->drain(&frames, 128);
    if (count > 0)
    {
        const double old_rate = this->waveformBuffer_.sampleRateHz();
        for (const BeamFrame &frame : frames)
        {
            if (frame.sampleRateHz() > 0.0)
                this->waveformBuffer_.append(frame);
            if (this->lastBeamSequence_ >= 0 && frame.sequence > static_cast<quint32>(this->lastBeamSequence_ + 1))
                this->sequenceDropped_ += static_cast<quint64>(frame.sequence - this->lastBeamSequence_ - 1);
            this->lastBeamSequence_ = frame.sequence;
            ++this->framesSinceStatus_;
        }
        if (!this->playbackActive_)
        {
            if (!std::isfinite(old_rate) || old_rate != this->waveformBuffer_.sampleRateHz())
            {
                this->waveformWidget_->setViewWindowSeconds(this->windowSecondsSpin_->value());
                this->waveformWidget_->resetView();
            }
            this->waveformWidget_->update();
        }
    }

    QVector<ScanFrame> scans;
    const int scan_count = this->scanQueue_->drain(&scans, 16);
    if (scan_count > 0)
    {
        for (const ScanFrame &scan : scans)
        {
            if (this->lastScanSequence_ >= 0 && scan.sequence > static_cast<quint32>(this->lastScanSequence_ + 1))
                this->sequenceDropped_ += static_cast<quint64>(scan.sequence - this->lastScanSequence_ - 1);
            this->lastScanSequence_ = scan.sequence;
        }
        this->latestScan_ = scans.back();
        this->hasScan_ = true;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - this->lastHeatmapUpdateMs_ >= 200)
        {
            this->hemisphereWidget_->setScanFrame(this->latestScan_);
            this->lastHeatmapUpdateMs_ = now;
        }
    }

    QVector<InferenceFrame> inferences;
    const int inference_count = this->inferenceQueue_->drain(&inferences, 4);
    if (inference_count > 0)
    {
        for (const InferenceFrame &inference : inferences)
        {
            if (this->lastInferenceSequence_ >= 0 &&
                inference.sequence > static_cast<quint64>(this->lastInferenceSequence_ + 1))
                this->sequenceDropped_ += inference.sequence -
                                         static_cast<quint64>(this->lastInferenceSequence_ + 1);
            this->lastInferenceSequence_ = static_cast<qint64>(inference.sequence);
            this->latestInference_ = inference;
        }
        this->hasInference_ = !this->latestInference_.values.isEmpty();
        if (this->hasInference_)
        {
            this->faultValid_ = true;
            this->faultIdentity_ = inferenceArgMax(this->latestInference_.values);
            this->inferenceReady_ = true;
            if (this->inferenceTensorLength_ > 0 &&
                this->latestInference_.values.size() != this->inferenceTensorLength_ &&
                this->lastInferenceWarnLength_ != this->latestInference_.values.size())
            {
                this->lastInferenceWarnLength_ = this->latestInference_.values.size();
                this->logMessage(QStringLiteral("推理 tensor 长度 %1 与 ack 声明 %2 不一致")
                                     .arg(this->latestInference_.values.size())
                                     .arg(this->inferenceTensorLength_));
            }
            this->inferenceBarWidget_->setInferenceFrame(this->latestInference_);
        }
        else
        {
            this->faultValid_ = false;
            this->faultIdentity_ = -1;
            this->inferenceBarWidget_->clear();
        }
    }
    this->updateStatusLabels();
}

void vuprs::client::MainWindow::onStatusTimer()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 elapsed = std::max<qint64>(1, now - this->statusWindowStartMs_);
    const double rate = this->framesSinceStatus_ * 1000.0 / elapsed;
    this->framesSinceStatus_ = 0;
    this->statusWindowStartMs_ = now;

    const quint64 dropped = this->beamQueue_->dropped() + this->scanQueue_->dropped() +
                            this->inferenceQueue_->dropped() + this->sequenceDropped_;
    const quint64 rx = this->client_ != nullptr ? this->client_->receivedBytes() : 0;
    this->dataStatusLabel_->setText(
        QStringLiteral("接收: %1 fps | 队列 %2 | 丢帧 %3 | 字节 %4")
            .arg(rate, 0, 'f', 1)
            .arg(this->beamQueue_->size() + this->scanQueue_->size() + this->inferenceQueue_->size())
            .arg(dropped)
            .arg(rx));
    this->maybeFinishRecording();
    this->updateRecordingUi();
    this->updateStatusLabels();
}

void vuprs::client::MainWindow::onReadParameters()
{
    this->sendCommand(QStringLiteral("read_algorithm_parameters"));
}

void vuprs::client::MainWindow::onValidateParameters()
{
    this->sendCommand(QStringLiteral("validate_config"), this->algorithmParametersFromUi());
}

void vuprs::client::MainWindow::onApplyParameters()
{
    const double fs = this->fsSpin_->value();
    const double lower = this->lowerFrequencySpin_->value();
    const double upper = this->upperFrequencySpin_->value();
    if (!(fs >= this->serverLimits_.minSamplingFrequencyHz) ||
        !(fs <= this->serverLimits_.maxSamplingFrequencyHz))
    {
        this->logMessage(QStringLiteral("参数校验失败: fs 超出服务端范围 %1..%2 Hz")
                             .arg(this->serverLimits_.minSamplingFrequencyHz)
                             .arg(this->serverLimits_.maxSamplingFrequencyHz));
        return;
    }
    if (!(lower >= this->serverLimits_.minFrequencyHz) ||
        !(upper >= this->serverLimits_.minFrequencyHz) ||
        lower >= upper || upper >= fs / 2.0)
    {
        this->logMessage(QStringLiteral("参数校验失败: 需要 %1 <= lower < upper < fs/2")
                             .arg(this->serverLimits_.minFrequencyHz));
        return;
    }
    if (this->waveVelocitySpin_->value() <= 0.0 ||
        this->snapshotWindowSpin_->value() < 1.0 ||
        this->covarianceIndexSpin_->value() < 0.0 ||
        this->covarianceIndexSpin_->value() >= 1.0)
    {
        this->logMessage(QStringLiteral("参数校验失败: 声速/快照窗口/协方差系数"));
        return;
    }
    this->sendCommand(QStringLiteral("change_algorithm_parameters"), this->algorithmParametersFromUi());
}

void vuprs::client::MainWindow::onApplyBeamformer()
{
    QJsonObject params;
    params["beamformer"] = this->beamformerCombo_->currentText();
    this->sendCommand(QStringLiteral("change_beamformer"), params);
}

void vuprs::client::MainWindow::onStart()
{
    this->sendCommand(QStringLiteral("start"));
}

void vuprs::client::MainWindow::onStop()
{
    this->sendCommand(QStringLiteral("stop"));
}

void vuprs::client::MainWindow::onReset()
{
    this->sendCommand(QStringLiteral("reset"));
}

void vuprs::client::MainWindow::onApplyRedirect()
{
    QJsonObject params;
    params["alt"] = this->numberString(this->altSpin_->value());
    params["az"] = this->numberString(this->azSpin_->value());
    this->hemisphereWidget_->setBeamDirection(this->altSpin_->value(), this->azSpin_->value());
    this->sendCommand(QStringLiteral("redirect"), params);
}

void vuprs::client::MainWindow::onEnableScan()
{
    const int points = this->scanPointsSpin_->value();
    const double alt_min = this->scanAltMinSpin_->value();
    if (points <= 0 || points > 10000 || alt_min <= 10.0 || alt_min > 90.0)
    {
        this->logMessage(QStringLiteral("扫描参数校验失败: points 需在 1..10000, alt_min 必须大于 10°"));
        return;
    }
    QJsonObject params;
    params["points"] = QString::number(points);
    params["alt_min"] = this->numberString(alt_min);
    this->sendCommand(QStringLiteral("enable_scan"), params);
}

void vuprs::client::MainWindow::onDisableScan()
{
    this->sendCommand(QStringLiteral("disable_scan"));
}

void vuprs::client::MainWindow::onRecordClicked()
{
    if (this->recordingUi_)
    {
        this->stopRecording(QStringLiteral("用户停止"), true);
        return;
    }
    if (!this->connected_)
    {
        this->logMessage(QStringLiteral("未连接服务器，无法录制"));
        return;
    }

    const QString directory = this->recordingDirectoryPath();
    if (!QDir().mkpath(directory))
    {
        const QString message = QStringLiteral("无法创建录制目录: ") + directory;
        this->logMessage(message);
        QMessageBox::warning(this, QStringLiteral("录制"), message);
        return;
    }
    this->recordDirectoryEdit_->setText(directory);
    if (this->config_.recordingDirectory != directory)
    {
        this->config_.recordingDirectory = directory;
        QString config_error;
        if (!saveAppConfig(this->config_, &config_error))
            this->logMessage(QStringLiteral("保存录制目录失败: ") + config_error);
    }

    const int index = nextRecordingIndex(directory, this->taskId_, this->nextRecordingIndex_);
    const QString path = recordingFilePath(directory, this->taskId_, index);
    const QJsonObject metadata = this->recordingMetadata(index);
    if (!this->recorder_->start(path, metadata, this->config_.recordingQueueFrames))
    {
        const QString message = QStringLiteral("录制启动失败: ") + this->recorder_->lastError();
        this->logMessage(message);
        QMessageBox::warning(this, QStringLiteral("录制"), message);
        return;
    }

    this->nextRecordingIndex_ = index + 1;
    this->currentRecordingPath_ = path;
    this->lastRecordingPath_.clear();
    this->recordingUi_ = true;
    this->recordingStartedMs_ = QDateTime::currentMSecsSinceEpoch();
    this->logMessage(QStringLiteral("开始录制 BF 波形: %1（task%2, id%3）")
                         .arg(path, this->taskId_)
                         .arg(index));
    this->updateRecordingUi();
}

void vuprs::client::MainWindow::onBrowseRecordingDirectory()
{
    const QString directory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择录制目录"), this->recordingDirectoryPath());
    if (directory.isEmpty())
        return;

    const QString cleaned = QDir::cleanPath(directory);
    this->recordDirectoryEdit_->setText(cleaned);
    this->config_.recordingDirectory = cleaned;
    QString error;
    if (!saveAppConfig(this->config_, &error))
    {
        this->logMessage(QStringLiteral("保存录制目录失败: ") + error);
        QMessageBox::warning(this, QStringLiteral("录制目录"), error);
    }
    else
    {
        this->logMessage(QStringLiteral("录制目录已保存: ") + cleaned);
    }
    this->updateRecordingUi();
}

void vuprs::client::MainWindow::onOpenPlayback()
{
    QString start_dir;
    if (!this->playbackPath_.isEmpty())
        start_dir = QFileInfo(this->playbackPath_).absolutePath();
    else
        start_dir = this->recordingDirectoryPath();
    if (start_dir.isEmpty() || !QDir(start_dir).exists())
        start_dir = QDir::homePath();

    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("打开回放"), start_dir,
        QStringLiteral("VUPRS BF 录制 (*.vbf *.part);;所有文件 (*)"));
    if (!path.isEmpty())
        this->loadPlaybackFile(path);
}

bool vuprs::client::MainWindow::loadPlaybackFile(const QString &path)
{
    VbfReader reader;
    QString error;
    if (!reader.open(path, &error))
    {
        this->logMessage(QStringLiteral("回放失败: ") + error);
        QMessageBox::warning(this, QStringLiteral("打开回放"), error);
        return false;
    }

    const qint64 file_size = reader.fileSize();
    if (file_size > 512LL * 1024 * 1024)
    {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("打开回放"),
            QStringLiteral("文件约 %1 MB，回放需要相近的内存。是否继续？")
                .arg(file_size / (1024.0 * 1024.0), 0, 'f', 1));
        if (answer != QMessageBox::Yes)
            return false;
    }

    BeamFrame first;
    VbfReader::Result read_result = reader.next(&first, &error);
    if (read_result == VbfReader::Result::Error)
    {
        this->logMessage(QStringLiteral("回放失败: ") + error);
        QMessageBox::warning(this, QStringLiteral("打开回放"), error);
        return false;
    }
    if (read_result == VbfReader::Result::End)
    {
        const QString message = QStringLiteral("文件中没有 BF 数据");
        this->logMessage(QStringLiteral("回放失败: ") + message);
        QMessageBox::warning(this, QStringLiteral("打开回放"), message);
        return false;
    }

    double sample_rate_hz = first.sampleRateHz();
    if (sample_rate_hz <= 0.0)
    {
        const double metadata_rate = reader.metadata().value(QStringLiteral("sample_rate_millihz")).toDouble() / 1000.0;
        if (metadata_rate > 0.0)
            sample_rate_hz = metadata_rate;
    }
    if (sample_rate_hz <= 0.0)
    {
        const QString message = QStringLiteral("无法确定采样率");
        this->logMessage(QStringLiteral("回放失败: ") + message);
        QMessageBox::warning(this, QStringLiteral("打开回放"), message);
        return false;
    }

    const qint64 capacity = file_size / static_cast<qint64>(sizeof(qint32)) + 1024;
    if (capacity > std::numeric_limits<int>::max())
    {
        const QString message = QStringLiteral("文件过大，无法载入回放");
        this->logMessage(QStringLiteral("回放失败: ") + message);
        QMessageBox::warning(this, QStringLiteral("打开回放"), message);
        return false;
    }

    WaveformBuffer loaded;
    if (!loaded.configureSamples(sample_rate_hz, capacity))
    {
        const QString message = QStringLiteral("创建回放波形缓存失败");
        this->logMessage(QStringLiteral("回放失败: ") + message);
        QMessageBox::warning(this, QStringLiteral("打开回放"), message);
        return false;
    }

    QProgressDialog progress(QStringLiteral("正在载入回放..."), QStringLiteral("取消"), 0, 1000, this);
    progress.setWindowTitle(QStringLiteral("打开回放"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    quint64 frames = 0;
    quint64 skipped = 0;
    const auto append_frame = [&](const BeamFrame &frame)
    {
        if (frame.samples.isEmpty())
            return;
        if (frame.sampleRateMilliHz != 0 &&
            std::abs(frame.sampleRateHz() - sample_rate_hz) > 1e-6)
        {
            ++skipped;
            return;
        }
        if (loaded.append(frame))
            ++frames;
    };

    append_frame(first);
    BeamFrame frame;
    int since_progress = 0;
    while ((read_result = reader.next(&frame, &error)) == VbfReader::Result::Frame)
    {
        append_frame(frame);
        if (++since_progress >= 200)
        {
            const qint64 total_bytes = std::max<qint64>(1, reader.fileSize());
            progress.setValue(static_cast<int>(
                std::min<qint64>(1000, reader.position() * 1000 / total_bytes)));
            QCoreApplication::processEvents();
            if (progress.wasCanceled())
            {
                this->logMessage(QStringLiteral("已取消载入回放"));
                return false;
            }
            since_progress = 0;
        }
    }
    progress.setValue(1000);
    if (read_result == VbfReader::Result::Error)
    {
        this->logMessage(QStringLiteral("回放失败: ") + error);
        QMessageBox::warning(this, QStringLiteral("打开回放"), error);
        return false;
    }
    if (loaded.totalSamples() <= 0)
    {
        const QString message = QStringLiteral("文件中没有可显示的 BF 数据");
        this->logMessage(QStringLiteral("回放失败: ") + message);
        QMessageBox::warning(this, QStringLiteral("打开回放"), message);
        return false;
    }

    const double duration_s = static_cast<double>(loaded.totalSamples()) / sample_rate_hz;
    this->logMessage(QStringLiteral("载入回放: %1 | %2 帧 | %3 样本 | %4 s | fs=%5 Hz")
                         .arg(path)
                         .arg(frames)
                         .arg(loaded.totalSamples())
                         .arg(duration_s, 0, 'f', 3)
                         .arg(sample_rate_hz, 0, 'f', 3));
    if (reader.statistics().truncated)
        this->logMessage(QStringLiteral("回放文件末尾不完整，已载入完整的帧"));
    if (!reader.statistics().hasFooter)
        this->logMessage(QStringLiteral("回放文件缺少文件尾，可能是未正常停止的录制"));
    if (skipped > 0)
        this->logMessage(QStringLiteral("回放跳过 %1 个采样率不一致的帧").arg(skipped));

    this->pausePlayback();
    this->playbackBuffer_ = std::move(loaded);
    this->playbackPath_ = path;
    this->playbackTotalSamples_ = this->playbackBuffer_.totalSamples();
    this->playbackSampleRateHz_ = sample_rate_hz;
    this->playheadSample_ = 0;
    this->playbackActive_ = true;
    this->enterPlayback();
    return true;
}

void vuprs::client::MainWindow::enterPlayback()
{
    this->playbackRangeActive_ = false;
    this->playbackRangeStart_ = 0;
    this->playbackRangeEnd_ = 0;
    this->playbackBar_->setVisible(true);
    this->waveformWidget_->setBuffer(&this->playbackBuffer_);
    this->waveformWidget_->setSelectionEnabled(true);
    {
        const QSignalBlocker blocker(this->autoFollowCheck_);
        this->autoFollowCheck_->setChecked(false);
    }
    this->autoFollowCheck_->setEnabled(false);
    this->waveformWidget_->setAutoFollow(false);
    {
        const QSignalBlocker blocker(this->freezeCheck_);
        this->freezeCheck_->setChecked(false);
    }
    this->waveformWidget_->setFrozen(false);

    const double duration_s = this->playbackSampleRateHz_ > 0.0
                                  ? static_cast<double>(this->playbackTotalSamples_) / this->playbackSampleRateHz_
                                  : 0.0;
    const double window_s = std::min(this->windowSecondsSpin_->value(), std::max(0.1, duration_s));
    this->waveformWidget_->setViewWindowSeconds(window_s);
    this->waveformWidget_->setViewRange(0, this->waveformWidget_->viewSpanSamples());
    this->waveformWidget_->setPlayheadSample(0, true);

    {
        const QSignalBlocker blocker(this->playbackSlider_);
        this->playbackSlider_->setRange(
            0, static_cast<int>(std::min<qint64>(this->playbackDurationMs(), std::numeric_limits<int>::max())));
        this->playbackSlider_->setValue(0);
    }
    this->playbackFileLabel_->setText(QFileInfo(this->playbackPath_).fileName());
    this->playbackFileLabel_->setToolTip(this->playbackPath_);
    this->playbackToggleButton_->setText(QStringLiteral("播放"));
    this->tabs_->setCurrentIndex(0);
    this->updatePlaybackUi();
}

void vuprs::client::MainWindow::exitPlayback()
{
    if (!this->playbackActive_)
        return;
    this->pausePlayback();
    this->playbackActive_ = false;
    this->playbackRangeActive_ = false;
    this->playbackRangeStart_ = 0;
    this->playbackRangeEnd_ = 0;
    this->waveformWidget_->setSelectionEnabled(false);
    this->waveformWidget_->setPlayheadSample(0, false);
    this->waveformWidget_->setBuffer(&this->waveformBuffer_);
    this->autoFollowCheck_->setEnabled(true);
    {
        const QSignalBlocker blocker(this->autoFollowCheck_);
        this->autoFollowCheck_->setChecked(true);
    }
    this->waveformWidget_->setAutoFollow(true);
    {
        const QSignalBlocker blocker(this->freezeCheck_);
        this->freezeCheck_->setChecked(false);
    }
    this->waveformWidget_->setFrozen(false);
    this->waveformWidget_->setViewWindowSeconds(this->windowSecondsSpin_->value());
    this->waveformWidget_->resetView();
    this->playbackBar_->setVisible(false);
    this->playbackFileLabel_->clear();
    this->playbackSlider_->setRange(0, 0);
    this->playbackTotalSamples_ = 0;
    this->playbackSampleRateHz_ = 0.0;
    this->playheadSample_ = 0;
    this->playbackPath_.clear();
    this->playbackBuffer_ = WaveformBuffer();
    this->logMessage(QStringLiteral("已退出 .vbf 回放"));
}

void vuprs::client::MainWindow::onPlaybackToggle()
{
    if (!this->playbackActive_)
        return;
    if (this->playbackPlaying_)
    {
        this->pausePlayback();
        return;
    }
    if (this->playbackTotalSamples_ <= 0 || this->playbackSampleRateHz_ <= 0.0)
        return;
    if (this->playbackRangeActive_)
    {
        if (this->playbackRangeEnd_ <= this->playbackRangeStart_)
        {
            this->clearPlaybackRange();
        }
        else
        {
            if (this->playheadSample_ < this->playbackRangeStart_ ||
                this->playheadSample_ >= this->playbackRangeEnd_)
            {
                this->playheadSample_ = this->playbackRangeStart_;
            }
            this->waveformWidget_->setViewRange(
                this->playbackRangeStart_,
                this->playbackRangeEnd_ - this->playbackRangeStart_);
        }
    }
    if (!this->playbackRangeActive_ &&
        this->playheadSample_ >= this->playbackTotalSamples_)
    {
        this->playheadSample_ = 0;
    }
    this->playbackPlaying_ = true;
    this->playbackAnchorSample_ = this->playheadSample_;
    this->playbackAnchorMs_ = QDateTime::currentMSecsSinceEpoch();
    this->waveformWidget_->setPlayheadSample(this->playheadSample_, true);
    this->playbackTimer_->start();
    if (!this->playbackRangeActive_)
        this->updatePlaybackView();
    this->updatePlaybackUi();
}

void vuprs::client::MainWindow::pausePlayback()
{
    if (this->playbackTimer_ != nullptr)
        this->playbackTimer_->stop();
    if (!this->playbackPlaying_)
        return;
    this->playbackPlaying_ = false;
    this->updatePlaybackUi();
}

void vuprs::client::MainWindow::onPlaybackClose()
{
    this->exitPlayback();
}

void vuprs::client::MainWindow::onPlaybackTimer()
{
    if (!this->playbackActive_ || !this->playbackPlaying_ ||
        this->playbackSampleRateHz_ <= 0.0)
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const double elapsed_s = static_cast<double>(now - this->playbackAnchorMs_) / 1000.0;
    const qint64 sample = this->playbackAnchorSample_ +
                          static_cast<qint64>(std::llround(elapsed_s * this->playbackSampleRateHz_));
    const qint64 limit = this->playbackRangeActive_ ? this->playbackRangeEnd_
                                                     : this->playbackTotalSamples_;

    if (sample >= limit)
    {
        this->playheadSample_ = limit;
        this->waveformWidget_->setPlayheadSample(this->playheadSample_, true);
        this->pausePlayback();
        if (!this->playbackRangeActive_)
            this->updatePlaybackView();
        this->updatePlaybackUi();
        return;
    }

    this->playheadSample_ = sample;
    this->waveformWidget_->setPlayheadSample(sample, true);
    this->updatePlaybackView();
    this->updatePlaybackUi();
}

void vuprs::client::MainWindow::onPlaybackSliderMoved(int ms)
{
    if (!this->playbackActive_ || this->playbackSampleRateHz_ <= 0.0)
        return;
    this->seekPlayback(static_cast<qint64>(
        std::llround(static_cast<double>(ms) / 1000.0 * this->playbackSampleRateHz_)));
}

void vuprs::client::MainWindow::seekPlayback(qint64 sample)
{
    if (!this->playbackActive_)
        return;
    this->playheadSample_ = std::max<qint64>(
        0, std::min<qint64>(sample, this->playbackTotalSamples_));
    if (this->playbackRangeActive_ &&
        (this->playheadSample_ < this->playbackRangeStart_ ||
         this->playheadSample_ > this->playbackRangeEnd_))
    {
        this->clearPlaybackRange();
    }
    if (this->playbackPlaying_)
    {
        this->playbackAnchorSample_ = this->playheadSample_;
        this->playbackAnchorMs_ = QDateTime::currentMSecsSinceEpoch();
    }
    this->waveformWidget_->setPlayheadSample(this->playheadSample_, true);
    this->updatePlaybackView();
    this->updatePlaybackUi();
}

void vuprs::client::MainWindow::onWaveformRangeSelected(qint64 startSample, qint64 endSample)
{
    if (!this->playbackActive_)
        return;
    this->selectPlaybackRange(startSample, endSample);
}

void vuprs::client::MainWindow::onSelectPlaybackRange()
{
    if (!this->playbackActive_ || this->playbackSampleRateHz_ <= 0.0 ||
        this->playbackTotalSamples_ <= 0)
        return;

    this->pausePlayback();
    const double duration_s = static_cast<double>(this->playbackTotalSamples_) /
                              this->playbackSampleRateHz_;
    double start_s = static_cast<double>(this->waveformWidget_->viewStartSamples()) /
                     this->playbackSampleRateHz_;
    double end_s = start_s + static_cast<double>(this->waveformWidget_->viewSpanSamples()) /
                                 this->playbackSampleRateHz_;
    start_s = std::max(0.0, std::min(start_s, duration_s));
    end_s = std::max(0.0, std::min(end_s, duration_s));
    if (end_s <= start_s)
    {
        start_s = 0.0;
        end_s = duration_s;
    }

    TimeRangeDialog dialog(duration_s, start_s, end_s, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const qint64 start = static_cast<qint64>(std::llround(
        dialog.startSeconds() * this->playbackSampleRateHz_));
    const qint64 end = static_cast<qint64>(std::llround(
        dialog.endSeconds() * this->playbackSampleRateHz_));
    this->selectPlaybackRange(start, end);
}

void vuprs::client::MainWindow::selectPlaybackRange(qint64 startSample, qint64 endSample)
{
    if (!this->playbackActive_ || this->playbackSampleRateHz_ <= 0.0 ||
        this->playbackTotalSamples_ <= 0)
        return;

    startSample = std::max<qint64>(0, std::min<qint64>(startSample, this->playbackTotalSamples_));
    endSample = std::max<qint64>(0, std::min<qint64>(endSample, this->playbackTotalSamples_));
    if (endSample <= startSample)
        return;
    if (endSample - startSample < 16)
    {
        this->statusBar()->showMessage(QStringLiteral("选择的时间段过短"), 3000);
        return;
    }

    this->pausePlayback();
    this->playbackRangeActive_ = true;
    this->playbackRangeStart_ = startSample;
    this->playbackRangeEnd_ = endSample;
    this->playheadSample_ = startSample;
    this->waveformWidget_->setViewRange(startSample, endSample - startSample);
    this->waveformWidget_->setPlayheadSample(startSample, true);
    this->updatePlaybackUi();

    const double fs = this->playbackSampleRateHz_;
    this->logMessage(QStringLiteral("回放时间段: %1 – %2 s（%3 – %4 样本）")
                         .arg(static_cast<double>(startSample) / fs, 0, 'f', 3)
                         .arg(static_cast<double>(endSample) / fs, 0, 'f', 3)
                         .arg(startSample)
                         .arg(endSample));
    this->statusBar()->showMessage(
        QStringLiteral("已选择回放时间段 %1 – %2 s")
            .arg(static_cast<double>(startSample) / fs, 0, 'f', 3)
            .arg(static_cast<double>(endSample) / fs, 0, 'f', 3),
        5000);
}

void vuprs::client::MainWindow::clearPlaybackRange()
{
    this->playbackRangeActive_ = false;
    this->playbackRangeStart_ = 0;
    this->playbackRangeEnd_ = 0;
}

void vuprs::client::MainWindow::updatePlaybackUi()
{
    if (!this->playbackActive_)
        return;
    const qint64 duration_ms = this->playbackDurationMs();
    const qint64 position_ms = this->playbackSampleRateHz_ > 0.0
                                   ? static_cast<qint64>(std::llround(
                                         static_cast<double>(this->playheadSample_) * 1000.0 /
                                         this->playbackSampleRateHz_))
                                   : 0;
    if (!this->playbackSlider_->isSliderDown())
    {
        const QSignalBlocker blocker(this->playbackSlider_);
        this->playbackSlider_->setValue(static_cast<int>(
            std::min<qint64>(position_ms, this->playbackSlider_->maximum())));
    }
    this->playbackTimeLabel_->setText(this->formatPlaybackTime(position_ms) +
                                      QStringLiteral(" / ") +
                                      this->formatPlaybackTime(duration_ms));
    this->playbackToggleButton_->setText(this->playbackPlaying_ ? QStringLiteral("暂停")
                                                                : QStringLiteral("播放"));
}

void vuprs::client::MainWindow::updatePlaybackView()
{
    if (!this->playbackActive_ || this->playbackTotalSamples_ <= 0)
        return;
    if (this->playbackRangeActive_ || this->freezeCheck_->isChecked())
        return;

    const qint64 span = this->waveformWidget_->viewSpanSamples();
    qint64 start = this->playheadSample_ - span / 2;
    const qint64 max_start = std::max<qint64>(0, this->playbackTotalSamples_ - span);
    start = std::max<qint64>(0, std::min<qint64>(start, max_start));
    this->waveformWidget_->setViewRange(start, span);
}

qint64 vuprs::client::MainWindow::playbackDurationMs() const
{
    if (this->playbackSampleRateHz_ <= 0.0)
        return 0;
    return static_cast<qint64>(std::llround(
        static_cast<double>(this->playbackTotalSamples_) * 1000.0 / this->playbackSampleRateHz_));
}

QString vuprs::client::MainWindow::formatPlaybackTime(qint64 ms) const
{
    ms = std::max<qint64>(0, ms);
    const qint64 total_seconds = ms / 1000;
    const qint64 hours = total_seconds / 3600;
    const qint64 minutes = (total_seconds / 60) % 60;
    const qint64 seconds = total_seconds % 60;
    const qint64 millis = ms % 1000;
    if (hours > 0)
    {
        return QStringLiteral("%1:%2:%3.%4")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'))
            .arg(millis, 3, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2.%3")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(millis, 3, 10, QLatin1Char('0'));
}

void vuprs::client::MainWindow::onExportPlaybackCsv()
{
    if (!this->playbackActive_ || this->playbackPath_.isEmpty())
    {
        this->logMessage(QStringLiteral("没有打开的回放文件"));
        return;
    }

    const QString vbf_path = this->playbackPath_;
    const QFileInfo vbf_info(vbf_path);
    const QString suggested = vbf_info.absolutePath() + QLatin1Char('/') +
                              vbf_info.completeBaseName() + QStringLiteral(".csv");
    const QString csv_path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出 CSV"), suggested,
        QStringLiteral("CSV 文件 (*.csv);;所有文件 (*)"));
    if (csv_path.isEmpty())
        return;
    if (QFileInfo(csv_path).absoluteFilePath() == vbf_info.absoluteFilePath())
    {
        QMessageBox::warning(this, QStringLiteral("导出 CSV"),
                             QStringLiteral("CSV 输出路径不能与 .vbf 相同"));
        return;
    }

    QProgressDialog progress(QStringLiteral("正在导出 CSV..."), QStringLiteral("取消"), 0, 1000, this);
    progress.setWindowTitle(QStringLiteral("导出 CSV"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    QString error;
    VbfCsvExportResult result;
    const bool ok = exportVbfToCsv(
        vbf_path, csv_path, &error,
        [&progress](qint64 processed, qint64 total)
        {
            if (total > 0)
                progress.setValue(static_cast<int>(
                    std::min<qint64>(1000, processed * 1000 / total)));
            QCoreApplication::processEvents();
            return !progress.wasCanceled();
        },
        &result);
    const bool canceled = progress.wasCanceled();
    progress.setValue(1000);

    if (!ok)
    {
        if (canceled)
        {
            this->logMessage(QStringLiteral("已取消 CSV 导出"));
        }
        else
        {
            this->logMessage(QStringLiteral("CSV 导出失败: ") + error);
            QMessageBox::warning(this, QStringLiteral("导出 CSV"), error);
        }
        return;
    }

    this->logMessage(QStringLiteral("CSV 导出完成: %1 | 帧=%2 行=%3")
                         .arg(csv_path)
                         .arg(result.frames)
                         .arg(result.rows));
    if (result.truncated)
        this->logMessage(QStringLiteral("源 .vbf 末尾不完整，已导出完整的帧"));
    if (!result.hasFooter)
        this->logMessage(QStringLiteral("源 .vbf 缺少文件尾，可能是未正常停止的录制"));
    this->statusBar()->showMessage(
        QStringLiteral("CSV 导出完成: %1（%2 行）").arg(QFileInfo(csv_path).fileName()).arg(result.rows),
        5000);
}

void vuprs::client::MainWindow::onOpenSettings()
{
    AppConfig initial = this->config_;
    initial.recordingDirectory = this->recordingDirectoryPath();
    SettingsDialog dialog(initial, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const AppConfig updated = dialog.config();
    QString error;
    if (!saveAppConfig(updated, &error))
    {
        this->logMessage(QStringLiteral("保存设置失败: ") + error);
        QMessageBox::warning(this, QStringLiteral("软件设置"), error);
        return;
    }
    this->applyConfig(updated);
    this->logMessage(QStringLiteral("设置已保存: ") + appConfigFilePath());
}

void vuprs::client::MainWindow::applyConfig(const AppConfig &config)
{
    const AppConfig previous = this->config_;
    this->config_ = config;

    this->hostEdit_->setText(this->config_.host);
    this->portSpin_->setValue(this->config_.port);
    this->recordDirectoryEdit_->setText(this->config_.recordingDirectory);
    this->config_.recordingDirectory = this->recordingDirectoryPath();
    this->recordDirectoryEdit_->setText(this->config_.recordingDirectory);

    this->waveformWidget_->setDefaultWindowSeconds(this->config_.defaultWindowSeconds);
    {
        const QSignalBlocker blocker(this->windowSecondsSpin_);
        this->windowSecondsSpin_->setValue(this->config_.defaultWindowSeconds);
    }
    this->waveformBuffer_.configure(this->currentSampleRateHz(), this->config_.historySeconds);
    if (!this->playbackActive_)
    {
        this->waveformWidget_->setViewWindowSeconds(this->config_.defaultWindowSeconds);
        this->waveformWidget_->resetView();
    }

    if (previous.beamQueueFrames != this->config_.beamQueueFrames ||
        previous.commandTimeoutMs != this->config_.commandTimeoutMs)
    {
        this->logMessage(QStringLiteral("接收队列帧数/命令超时将在下次启动后生效"));
    }
    this->updateRecordingUi();
    this->updateParameterMetrics();
}

void vuprs::client::MainWindow::onBeamDirectionPicked(double altDeg, double azDeg)
{
    this->altSpin_->setValue(altDeg);
    this->azSpin_->setValue(azDeg);
    this->hemisphereWidget_->setBeamDirection(altDeg, azDeg);
    this->logMessage(QStringLiteral("从热力图选择指向 alt=%1°, az=%2°（点击“应用指向”下发）")
                         .arg(altDeg, 0, 'f', 2)
                         .arg(azDeg, 0, 'f', 2));
}

void vuprs::client::MainWindow::onAutoFollowChanged(bool enabled)
{
    this->waveformWidget_->setAutoFollow(enabled);
}

void vuprs::client::MainWindow::onFreezeChanged(bool frozen)
{
    this->waveformWidget_->setFrozen(frozen);
}

void vuprs::client::MainWindow::onResetView()
{
    if (this->playbackActive_)
    {
        this->pausePlayback();
        this->clearPlaybackRange();
        this->playheadSample_ = 0;
        this->waveformWidget_->setPlayheadSample(0, true);
        this->waveformWidget_->setViewRange(
            0, std::max<qint64>(16, this->playbackTotalSamples_));
        this->updatePlaybackUi();
        this->statusBar()->showMessage(QStringLiteral("已恢复完整回放视图"), 3000);
        return;
    }
    const QSignalBlocker blocker(this->autoFollowCheck_);
    this->autoFollowCheck_->setChecked(true);
    this->waveformWidget_->resetView();
}

void vuprs::client::MainWindow::onWindowSecondsChanged(double seconds)
{
    this->waveformWidget_->setViewWindowSeconds(seconds);
    if (this->playbackActive_)
    {
        if (this->playbackRangeActive_)
            this->clearPlaybackRange();
        this->updatePlaybackView();
    }
}

void vuprs::client::MainWindow::setConnectedUi(bool connected)
{
    this->connectButton_->setText(connected ? QStringLiteral("断开") : QStringLiteral("连接"));
    this->hostEdit_->setEnabled(!connected);
    this->portSpin_->setEnabled(!connected);
    this->mockButton_->setEnabled(!connected);
    this->boardButton_->setEnabled(!connected);
    const bool controls = connected;
    this->readParametersButton_->setEnabled(controls);
    this->validateParametersButton_->setEnabled(controls);
    this->applyParametersButton_->setEnabled(controls);
    this->applyBeamformerButton_->setEnabled(controls);
    this->startButton_->setEnabled(controls);
    this->stopButton_->setEnabled(controls);
    this->resetButton_->setEnabled(controls);
    this->applyRedirectButton_->setEnabled(controls);
    this->enableScanButton_->setEnabled(controls);
    this->disableScanButton_->setEnabled(controls);
    this->recordButton_->setEnabled(controls && !this->recordingUi_);
    this->recordDirectoryEdit_->setEnabled(!this->recordingUi_);
    this->browseRecordButton_->setEnabled(!this->recordingUi_);
}

void vuprs::client::MainWindow::updateConnectionStatus(const QString &detail)
{
    this->connectionLabel_->setText(detail);
    QPalette palette = this->connectionLabel_->palette();
    palette.setColor(QPalette::WindowText,
                     this->connected_ ? QColor(60, 190, 120) : QColor(220, 150, 60));
    this->connectionLabel_->setPalette(palette);
}

void vuprs::client::MainWindow::clearData()
{
    this->waveformBuffer_.clear();
    this->waveformWidget_->update();
    this->hemisphereWidget_->clear();
    this->latestScan_ = ScanFrame();
    this->hasScan_ = false;
    this->lastHeatmapUpdateMs_ = 0;
    this->lastBeamSequence_ = -1;
    this->lastScanSequence_ = -1;
    this->lastInferenceSequence_ = -1;
    this->sequenceDropped_ = 0;
    this->faultIdentity_ = -1;
    this->faultValid_ = false;
    this->latestInference_ = InferenceFrame();
    this->hasInference_ = false;
    this->inferenceTensorLength_ = 0;
    this->lastInferenceWarnLength_ = -1;
    this->inferenceQueue_->clear();
    if (this->inferenceBarWidget_ != nullptr)
    {
        this->inferenceBarWidget_->clear();
        this->inferenceBarWidget_->setExpectedLength(0);
    }
    this->framesSinceStatus_ = 0;
    this->statusWindowStartMs_ = QDateTime::currentMSecsSinceEpoch();
    this->waveformBuffer_.configure(this->fsSpin_->value(), this->config_.historySeconds);
    if (!this->playbackActive_)
        this->waveformWidget_->resetView();
    this->updateStatusLabels();
}

void vuprs::client::MainWindow::updateStatusLabels()
{
    this->runningLabel_->setText(QStringLiteral("运行: %1").arg(this->deviceRunning_ ? QStringLiteral("是") : QStringLiteral("否")));
    this->inferenceLabel_->setText(QStringLiteral("推理: %1").arg(this->inferenceReady_ ? QStringLiteral("可用") : QStringLiteral("未就绪")));
    if (this->connected_)
    {
        if (this->faultValid_ && this->faultIdentity_ >= 0)
            this->faultLabel_->setText(QStringLiteral("故障ID: %1").arg(this->faultIdentity_));
        else
            this->faultLabel_->setText(QStringLiteral("故障ID: 无有效推理"));
    }
    else
    {
        this->faultLabel_->setText(QStringLiteral("故障ID: -"));
    }

    if (!this->connected_)
    {
        this->scanStatusLabel_->setText(QStringLiteral("扫描: -"));
    }
    else if (!this->scanEnabled_)
    {
        this->scanStatusLabel_->setText(QStringLiteral("扫描: 未开启"));
    }
    else if (this->hasScan_)
    {
        this->scanStatusLabel_->setText(
            QStringLiteral("扫描: points=%1 data=%2\nmin=%3 dB max=%4 dB")
                .arg(this->latestScan_.pointsHalf)
                .arg(this->latestScan_.dataPoints)
                .arg(this->latestScan_.minPowerDb, 0, 'f', 2)
                .arg(this->latestScan_.maxPowerDb, 0, 'f', 2));
    }
    else
    {
        this->scanStatusLabel_->setText(QStringLiteral("扫描: 等待数据"));
    }
    this->statusBar()->showMessage(
        QStringLiteral("%1 | %2 | 队列 %3 | 丢帧 %4")
            .arg(this->connected_ ? QStringLiteral("已连接") : QStringLiteral("未连接"))
            .arg(this->deviceRunning_ ? QStringLiteral("运行") : QStringLiteral("停止"))
            .arg(this->beamQueue_->size() + this->scanQueue_->size())
            .arg(this->beamQueue_->dropped() + this->scanQueue_->dropped() + this->sequenceDropped_));
}

void vuprs::client::MainWindow::updateRecordingUi()
{
    this->recordButton_->setText(this->recordingUi_ ? QStringLiteral("停止录制") : QStringLiteral("开始录制"));
    this->recordButton_->setEnabled(this->connected_);
    this->recordDirectoryEdit_->setEnabled(!this->recordingUi_);
    this->browseRecordButton_->setEnabled(!this->recordingUi_);
    if (!this->recordingUi_)
    {
        if (!this->recorder_->lastError().isEmpty())
            this->recordStatusLabel_->setText(QStringLiteral("录制错误: ") + this->recorder_->lastError());
        else if (!this->lastRecordingPath_.isEmpty())
            this->recordStatusLabel_->setText(QStringLiteral("已保存:\n") + this->lastRecordingPath_);
        else
            this->recordStatusLabel_->setText(
                QStringLiteral("任务ID: task%1\n未录制").arg(this->taskId_));
        return;
    }
    const qint64 elapsed = std::max<qint64>(0, QDateTime::currentMSecsSinceEpoch() - this->recordingStartedMs_);
    const quint64 frames = this->recorder_->framesWritten();
    const quint64 bytes = this->recorder_->payloadBytes();
    const quint64 dropped = this->recorder_->droppedFrames();
    this->recordStatusLabel_->setText(
        QStringLiteral("录制中 %1:%2  帧=%3  %4 MB  丢弃=%5\n%6")
            .arg(elapsed / 60000)
            .arg((elapsed / 1000) % 60, 2, 10, QLatin1Char('0'))
            .arg(frames)
            .arg(bytes / (1024.0 * 1024.0), 0, 'f', 2)
            .arg(dropped)
            .arg(QFileInfo(this->currentRecordingPath_).fileName()));
}

void vuprs::client::MainWindow::stopRecording(const QString &reason, bool notify)
{
    if (!this->recordingUi_ && !this->recorder_->isActive())
        return;
    const QString path = this->currentRecordingPath_;
    this->recorder_->stop();
    const QString error = this->recorder_->lastError();
    this->logMessage(QStringLiteral("停止录制（%1）: 帧=%2, 数据=%3 MB, 丢弃=%4")
                         .arg(reason)
                         .arg(this->recorder_->framesWritten())
                         .arg(this->recorder_->payloadBytes() / (1024.0 * 1024.0), 0, 'f', 2)
                         .arg(this->recorder_->droppedFrames()));
    if (error.isEmpty())
    {
        this->lastRecordingPath_ = path;
        if (!path.isEmpty())
            this->logMessage(QStringLiteral("录制文件已保存: ") + path);
    }
    else
    {
        this->lastRecordingPath_.clear();
        this->logMessage(QStringLiteral("录制错误: ") + error);
    }
    this->currentRecordingPath_.clear();
    this->recordingUi_ = false;
    this->updateRecordingUi();

    if (!notify)
        return;
    if (!error.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("录制失败"), error);
    }
    else if (!path.isEmpty())
    {
        QMessageBox::information(
            this, QStringLiteral("录制完成"),
            QStringLiteral("录制文件已保存到：\n%1").arg(QDir::toNativeSeparators(path)));
    }
}

void vuprs::client::MainWindow::maybeFinishRecording()
{
    if (this->recordingUi_ && !this->recorder_->isActive())
    {
        const QString error = this->recorder_->lastError();
        if (error.isEmpty())
            this->lastRecordingPath_ = this->currentRecordingPath_;
        else
            this->lastRecordingPath_.clear();
        this->currentRecordingPath_.clear();
        this->recordingUi_ = false;
        this->logMessage(QStringLiteral("录制线程已结束") + (error.isEmpty() ? QString() : QStringLiteral(": ") + error));
        this->updateRecordingUi();
    }
}

double vuprs::client::MainWindow::currentSampleRateHz() const
{
    if (this->waveformBuffer_.sampleRateHz() > 0.0)
        return this->waveformBuffer_.sampleRateHz();
    return this->fsSpin_->value();
}

QJsonObject vuprs::client::MainWindow::algorithmParametersFromUi() const
{
    QJsonObject params;
    params["fs"] = this->numberString(this->fsSpin_->value());
    params["wave_velocity"] = this->numberString(this->waveVelocitySpin_->value());
    params["lower_frequency"] = this->numberString(this->lowerFrequencySpin_->value());
    params["upper_frequency"] = this->numberString(this->upperFrequencySpin_->value());
    params["snapshot_window_size"] = this->numberString(this->snapshotWindowSpin_->value());
    params["covariance_average_index"] = this->numberString(this->covarianceIndexSpin_->value());
    return params;
}

QJsonObject vuprs::client::MainWindow::recordingMetadata(int index) const
{
    QJsonObject metadata;
    metadata["protocol_version"] = 1;
    metadata["client_version"] = QStringLiteral("0.1.0");
    metadata["sample_rate_millihz"] = static_cast<qint64>(std::llround(this->currentSampleRateHz() * 1000.0));
    metadata["sample_format"] = QStringLiteral("int32_q16.16");
    metadata["server"] = QStringLiteral("%1:%2").arg(this->hostEdit_->text().trimmed()).arg(this->portSpin_->value());
    metadata["start_utc"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    metadata["task_id"] = this->taskId_;
    metadata["recording_index"] = index;
    metadata["beamformer"] = this->beamformerCombo_->currentText();
    metadata["target_alt_deg"] = this->altSpin_->value();
    metadata["target_az_deg"] = this->azSpin_->value();
    metadata["algorithm"] = this->algorithmParametersFromUi();
    return metadata;
}

QString vuprs::client::MainWindow::recordingDirectoryPath() const
{
    QString directory = this->recordDirectoryEdit_ != nullptr
                            ? this->recordDirectoryEdit_->text().trimmed()
                            : this->config_.recordingDirectory;
    if (directory.isEmpty())
        directory = this->config_.recordingDirectory;
    if (directory.isEmpty())
        directory = defaultRecordingDirectory();
    if (QDir::isRelativePath(directory))
    {
        QString base = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        if (base.isEmpty())
            base = QCoreApplication::applicationDirPath();
        directory = QDir(base).filePath(directory);
    }
    return QDir::cleanPath(QDir(directory).absolutePath());
}


QString vuprs::client::MainWindow::numberString(double value) const
{
    return QString::number(value, 'f', 6);
}

void vuprs::client::MainWindow::applyAlgorithmParameters(const QJsonObject &params)
{
    const auto read_double = [&params](const QString &key, double *target)
    {
        bool ok = false;
        const double value = params.value(key).toString().toDouble(&ok);
        if (ok)
            *target = value;
    };
    double fs = this->fsSpin_->value();
    double velocity = this->waveVelocitySpin_->value();
    double lower = this->lowerFrequencySpin_->value();
    double upper = this->upperFrequencySpin_->value();
    double snapshot = this->snapshotWindowSpin_->value();
    double covariance = this->covarianceIndexSpin_->value();
    read_double(QStringLiteral("fs"), &fs);
    read_double(QStringLiteral("wave_velocity"), &velocity);
    read_double(QStringLiteral("lower_frequency"), &lower);
    read_double(QStringLiteral("upper_frequency"), &upper);
    read_double(QStringLiteral("snapshot_window_size"), &snapshot);
    read_double(QStringLiteral("covariance_average_index"), &covariance);

    const double old_rate = this->waveformBuffer_.sampleRateHz();
    this->setSpinValueExpanding(this->fsSpin_, fs, QStringLiteral("fs"));
    this->setSpinValueExpanding(this->waveVelocitySpin_, velocity, QStringLiteral("声速"));
    this->setSpinValueExpanding(this->lowerFrequencySpin_, lower, QStringLiteral("通带下限"));
    this->setSpinValueExpanding(this->upperFrequencySpin_, upper, QStringLiteral("通带上限"));
    this->setSpinValueExpanding(this->snapshotWindowSpin_, snapshot, QStringLiteral("快照窗口"));
    this->setSpinValueExpanding(this->covarianceIndexSpin_, covariance, QStringLiteral("协方差平均"));
    if (fs > 0.0)
    {
        this->waveformBuffer_.configure(fs, this->config_.historySeconds);
        if (old_rate <= 0.0 || std::abs(old_rate - fs) > 1e-6)
        {
            this->waveformWidget_->setViewWindowSeconds(this->windowSecondsSpin_->value());
            this->waveformWidget_->resetView();
            this->logMessage(QStringLiteral("波形缓存已按采样率 %1 Hz 重新初始化").arg(fs, 0, 'f', 3));
        }
    }
    this->updateParameterMetrics();
}

void vuprs::client::MainWindow::applyInferenceMetadata(const QJsonObject &params)
{
    const QJsonValue length_value = params.value(QStringLiteral("inference_tensor_length"));
    int length = 0;
    bool length_ok = false;
    if (length_value.isString())
        length = length_value.toString().toInt(&length_ok);
    else if (length_value.isDouble())
    {
        length = length_value.toInt();
        length_ok = true;
    }
    if (length_ok)
    {
        length = std::max(0, length);
        if (length != this->inferenceTensorLength_)
        {
            this->inferenceTensorLength_ = length;
            this->latestInference_ = InferenceFrame();
            this->hasInference_ = false;
            this->lastInferenceSequence_ = -1;
            this->lastInferenceWarnLength_ = -1;
            if (this->inferenceBarWidget_ != nullptr)
            {
                this->inferenceBarWidget_->clear();
                this->inferenceBarWidget_->setExpectedLength(length);
            }
        }
    }

    const QString format = params.value(QStringLiteral("inference_format")).toString().trimmed().toLower();
    if (!format.isEmpty() && format != QStringLiteral("q31"))
        this->logMessage(QStringLiteral("不支持的推理格式: ") + format);
}

void vuprs::client::MainWindow::setSpinValueExpanding(QDoubleSpinBox *spin,
                                                      double value,
                                                      const QString &name)
{
    if (spin == nullptr || !std::isfinite(value))
        return;
    const double old_min = spin->minimum();
    const double old_max = spin->maximum();
    if (value < old_min || value > old_max)
    {
        spin->setRange(std::min(old_min, value), std::max(old_max, value));
        this->logMessage(QStringLiteral("服务端参数 %1=%2 超出界面范围, 已扩展显示范围")
                             .arg(name)
                             .arg(value));
    }
    spin->setValue(value);
}

void vuprs::client::MainWindow::applyServerLimits()
{
    const ServerLimits &limits = this->serverLimits_;
    const double fs_min = std::max(0.001, limits.minSamplingFrequencyHz);
    const double fs_max = std::max(fs_min, limits.maxSamplingFrequencyHz);
    const double frequency_min = std::max(0.001, limits.minFrequencyHz);
    const double frequency_max = std::max(frequency_min, limits.maxFrequencyHz);

    const double current_fs = this->fsSpin_->value();
    this->fsSpin_->setRange(std::min(fs_min, current_fs), std::max(fs_max, current_fs));
    if (current_fs < fs_min || current_fs > fs_max)
    {
        this->logMessage(QStringLiteral("当前 fs=%1 Hz 超出服务端范围 %2..%3 Hz")
                             .arg(current_fs)
                             .arg(fs_min)
                             .arg(fs_max));
    }

    const double current_lower = this->lowerFrequencySpin_->value();
    const double current_upper = this->upperFrequencySpin_->value();
    this->lowerFrequencySpin_->setRange(std::min(frequency_min, current_lower),
                                        std::max(frequency_max, current_lower));
    this->upperFrequencySpin_->setRange(std::min(frequency_min, current_upper),
                                        std::max(frequency_max, current_upper));

    this->waveVelocitySpin_->setRange(
        1.0, std::max(limits.maxWaveVelocity, this->waveVelocitySpin_->value()));
    this->scanPointsSpin_->setRange(
        1, std::max(limits.maxScanPoints, this->scanPointsSpin_->value()));

    const QString limits_text =
        QStringLiteral("服务端限制: fs %1..%2 Hz, 频率 >= %3 Hz, FFT %4 点, FIR %5, "
                       "DMA %6 点, 最大扫描点 %7, 声速 <= %8 m/s")
            .arg(fs_min, 0, 'f', 1)
            .arg(fs_max, 0, 'f', 1)
            .arg(frequency_min, 0, 'f', 1)
            .arg(limits.fftPoints)
            .arg(limits.firLength)
            .arg(limits.dmaBufferPoints)
            .arg(limits.maxScanPoints)
            .arg(limits.maxWaveVelocity, 0, 'f', 0);
    this->serverInfoLabel_->setToolTip(limits_text);
    this->fsSpin_->setToolTip(QStringLiteral("%1 实际范围由服务端决定; 通带上限必须低于 fs/2.")
                                  .arg(limits_text));
    this->updateParameterMetrics();
}

void vuprs::client::MainWindow::updateParameterMetrics()
{
    if (this->constraintsLabel_ == nullptr)
        return;

    this->parameterMetrics_ = computeParameterMetrics(
        this->serverLimits_,
        this->fsSpin_->value(),
        this->lowerFrequencySpin_->value(),
        this->upperFrequencySpin_->value(),
        this->snapshotWindowSpin_->value(),
        this->config_.beamQueueFrames,
        this->config_.recordingQueueFrames);

    QStringList lines;
    const ParameterMetrics &metrics = this->parameterMetrics_;
    if (metrics.frequencyResolutionHz > 0.0)
    {
        QString band = QStringLiteral("df = %1 Hz, 通带约 %2 个频点")
                           .arg(metrics.frequencyResolutionHz, 0, 'f', 2)
                           .arg(metrics.bandBinCount);
        if (metrics.bandBinCount < 8)
            band += QStringLiteral(" (过少)");
        else if (metrics.bandBinCount < 16)
            band += QStringLiteral(" (偏少)");
        lines << band;
    }
    if (metrics.blockPeriodSeconds > 0.0)
    {
        lines << QStringLiteral("快照约 %1 s, 数据块 %2 ms")
                     .arg(metrics.snapshotTimeSeconds, 0, 'f', 3)
                     .arg(metrics.blockPeriodSeconds * 1000.0, 0, 'f', 1);
        lines << QStringLiteral("接收队列约 %1 s, 录制队列约 %2 s")
                     .arg(metrics.receiveQueueSeconds, 0, 'f', 1)
                     .arg(metrics.recordingQueueSeconds, 0, 'f', 1);
    }
    if (this->serverLimits_.fromServer)
    {
        lines << QStringLiteral("服务端: fs %1..%2, FFT %3, FIR %4, DMA %5")
                     .arg(this->serverLimits_.minSamplingFrequencyHz, 0, 'f', 0)
                     .arg(this->serverLimits_.maxSamplingFrequencyHz, 0, 'f', 0)
                     .arg(this->serverLimits_.fftPoints)
                     .arg(this->serverLimits_.firLength)
                     .arg(this->serverLimits_.dmaBufferPoints);
    }
    else
    {
        lines << QStringLiteral("服务端能力未获取, 使用默认 FFT %1, FIR %2, DMA %3")
                     .arg(this->serverLimits_.fftPoints)
                     .arg(this->serverLimits_.firLength)
                     .arg(this->serverLimits_.dmaBufferPoints);
    }
    if (this->serverLimits_.inferenceEnabled)
    {
        lines << QStringLiteral("推理帧 %1 点, DMA 缓冲 %2 点")
                     .arg(metrics.inferenceFrameSamples)
                     .arg(this->serverLimits_.dmaBufferPoints);
    }
    if (metrics.inferenceBufferTooShort)
        lines << QStringLiteral("警告: DMA 缓冲小于一个推理帧, 推理不会启动");

    this->constraintsLabel_->setText(lines.join(QLatin1Char('\n')));
    if (metrics.inferenceBufferTooShort || (metrics.frequencyResolutionHz > 0.0 && metrics.bandBinCount < 8))
        this->constraintsLabel_->setStyleSheet(QStringLiteral("color: #b00020;"));
    else if (metrics.frequencyResolutionHz > 0.0 && metrics.bandBinCount < 16)
        this->constraintsLabel_->setStyleSheet(QStringLiteral("color: #a15c00;"));
    else
        this->constraintsLabel_->setStyleSheet(QStringLiteral("color: #4a5560;"));
}
