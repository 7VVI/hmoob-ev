#include "app/SettingsDialog.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <string>
#include <vector>

#include "app/Controls.h"
#include "app/UiFont.h"
#include "core/capture/CameraSource.h"

namespace {

const char* const kSettingsStyle = R"(
QDialog { background: #FFFFFF; }
QFrame#settingsCard { background: #FFFFFF; border: 1px solid #EDF0F5; border-radius: 12px; }
QLabel#dialogTitle { font-size: 17px; font-weight: 600; color: #1D2129; }
QLabel#sectionTitle { font-size: 13px; font-weight: 600; color: #86909C; }
QLabel#fieldLabel { font-size: 14px; color: #1D2129; }
QLabel#fieldHint { font-size: 12px; color: #86909C; }
QLabel#fieldError { font-size: 12px; color: #E5484D; }
QComboBox { background: #FFFFFF; border: 1px solid #E5E6EB; border-radius: 10px; padding: 0 10px 0 14px; font-size: 14px; color: #1D2129; }
QComboBox:hover { border-color: #C9CDD4; }
QComboBox:disabled { background: #F7F8FA; color: #A9AEB8; border-color: #EDF0F5; }
QComboBox::drop-down { border: none; background: transparent; width: 34px; }
QComboBox::down-arrow { image: none; width: 0px; height: 0px; }
QComboBox QAbstractItemView { background: #FFFFFF; border: 1px solid #E5E6EB; border-radius: 8px; padding: 4px; outline: 0; selection-background-color: #EAF2FE; selection-color: #2F6BFF; }
QComboBox QAbstractItemView::item { min-height: 30px; padding: 0 10px; }
QPushButton { min-height: 36px; padding: 0 22px; border-radius: 8px; font-size: 14px; }
QPushButton#secondary { background: #FFFFFF; border: 1px solid #E5E6EB; color: #4E5969; }
QPushButton#secondary:hover { background: #F7F8FA; }
QPushButton#primary { background: #2F6BFF; border: 1px solid #2F6BFF; color: #FFFFFF; }
QPushButton#primary:hover { background: #245BF0; }
QPushButton#primary:pressed { background: #1E52E8; }
QPushButton#primary:disabled { background: #B7C7F5; border-color: #B7C7F5; color: #FFFFFF; }
)";

QLabel* MakeFieldLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("fieldLabel"));
    return label;
}

QLabel* MakeFieldHint(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("fieldHint"));
    return label;
}

QLabel* MakeSectionTitle(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("sectionTitle"));
    return label;
}

}  // namespace

SettingsDialog::SettingsDialog(int64_t bitrate, bool hdr, int camera_index,
                               const QString& container_extension,
                               hmmob::AudioCodec audio_codec,
                               int audio_sample_rate, int audio_bitrate,
                               const QString& video_codec, QWidget* parent)
    : QDialog(parent), hevc_(video_codec == QStringLiteral("hevc")) {
    setWindowTitle(QStringLiteral("设置"));
    setModal(true);
    setFont(UiFont(14));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QString::fromUtf8(kSettingsStyle));
    setFixedSize(440, 700);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 16, 16, 16);
    outer->setSpacing(0);

    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("settingsCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    outer->addWidget(card);

    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(0);

    auto* title = new QLabel(QStringLiteral("设置"), card);
    title->setObjectName(QStringLiteral("dialogTitle"));
    layout->addWidget(title);
    layout->addSpacing(16);

    layout->addWidget(MakeSectionTitle(QStringLiteral("视频设置"), card));
    layout->addSpacing(8);

    layout->addWidget(MakeFieldLabel(QStringLiteral("保存格式"), card));
    layout->addSpacing(5);
    container_combo_ = new StyledComboBox(card);
    container_combo_->setFixedHeight(36);
    container_combo_->addItem(QStringLiteral("MP4"), QStringLiteral("mp4"));
    container_combo_->addItem(QStringLiteral("MOV"), QStringLiteral("mov"));
    container_combo_->addItem(QStringLiteral("MKV"), QStringLiteral("mkv"));
    container_combo_->addItem(QStringLiteral("AVI"), QStringLiteral("avi"));
    container_combo_->addItem(QStringLiteral("FLV"), QStringLiteral("flv"));
    container_combo_->addItem(QStringLiteral("WMV"), QStringLiteral("wmv"));
    {
        const int index = container_combo_->findData(container_extension);
        container_combo_->setCurrentIndex(index >= 0 ? index : 0);
    }
    layout->addWidget(container_combo_);
    layout->addSpacing(4);
    compatibility_hint_ = new QLabel(card);
    compatibility_hint_->setObjectName(QStringLiteral("fieldError"));
    compatibility_hint_->setFixedHeight(16);
    layout->addWidget(compatibility_hint_);
    layout->addSpacing(12);

    layout->addWidget(MakeFieldLabel(QStringLiteral("码率"), card));
    layout->addSpacing(5);
    bitrate_combo_ = new StyledComboBox(card);
    bitrate_combo_->setFixedHeight(36);
    bitrate_combo_->addItem(QStringLiteral("20 Mbps"),
                            QVariant::fromValue<qlonglong>(20000000));
    bitrate_combo_->addItem(QStringLiteral("50 Mbps"),
                            QVariant::fromValue<qlonglong>(50000000));
    bitrate_combo_->addItem(QStringLiteral("100 Mbps"),
                            QVariant::fromValue<qlonglong>(100000000));
    {
        const int index = bitrate_combo_->findData(
            QVariant::fromValue<qlonglong>(static_cast<qlonglong>(bitrate)));
        bitrate_combo_->setCurrentIndex(index >= 0 ? index : 1);
    }
    layout->addWidget(bitrate_combo_);
    layout->addSpacing(12);

    hdr_check_ = new ToggleCheckBox(QStringLiteral("HDR（色调映射为 SDR）"),
                                    card);
    hdr_check_->setChecked(hdr);
    layout->addWidget(hdr_check_);
    layout->addSpacing(4);
    auto* hdr_hint = MakeFieldHint(QStringLiteral("仅全屏录制生效"), card);
    hdr_hint->setContentsMargins(26, 0, 0, 0);
    layout->addWidget(hdr_hint);
    layout->addSpacing(16);

    layout->addWidget(MakeSectionTitle(QStringLiteral("音频设置"), card));
    layout->addSpacing(8);

    layout->addWidget(MakeFieldLabel(QStringLiteral("音频格式"), card));
    layout->addSpacing(5);
    audio_codec_combo_ = new StyledComboBox(card);
    audio_codec_combo_->setFixedHeight(36);
    audio_codec_combo_->addItem(QStringLiteral("AAC"),
                                static_cast<int>(hmmob::AudioCodec::Aac));
    audio_codec_combo_->addItem(
        QStringLiteral("PCM"),
        static_cast<int>(hmmob::AudioCodec::PcmS16Le));
    {
        const int index = audio_codec_combo_->findData(
            static_cast<int>(audio_codec));
        audio_codec_combo_->setCurrentIndex(index >= 0 ? index : 0);
    }
    layout->addWidget(audio_codec_combo_);
    layout->addSpacing(12);

    layout->addWidget(MakeFieldLabel(QStringLiteral("采样率"), card));
    layout->addSpacing(5);
    sample_rate_combo_ = new StyledComboBox(card);
    sample_rate_combo_->setFixedHeight(36);
    sample_rate_combo_->addItem(QStringLiteral("44.1 kHz"), 44100);
    sample_rate_combo_->addItem(QStringLiteral("48 kHz"), 48000);
    {
        const int index = sample_rate_combo_->findData(audio_sample_rate);
        sample_rate_combo_->setCurrentIndex(index >= 0 ? index : 1);
    }
    layout->addWidget(sample_rate_combo_);
    layout->addSpacing(12);

    layout->addWidget(MakeFieldLabel(QStringLiteral("音频码率"), card));
    layout->addSpacing(5);
    audio_bitrate_combo_ = new StyledComboBox(card);
    audio_bitrate_combo_->setFixedHeight(36);
    audio_bitrate_combo_->addItem(QStringLiteral("96 kbps"), 96000);
    audio_bitrate_combo_->addItem(QStringLiteral("128 kbps"), 128000);
    audio_bitrate_combo_->addItem(QStringLiteral("192 kbps"), 192000);
    audio_bitrate_combo_->addItem(QStringLiteral("320 kbps"), 320000);
    {
        const int index = audio_bitrate_combo_->findData(audio_bitrate);
        audio_bitrate_combo_->setCurrentIndex(index >= 0 ? index : 2);
    }
    layout->addWidget(audio_bitrate_combo_);
    layout->addSpacing(12);

    layout->addWidget(MakeFieldLabel(QStringLiteral("摄像头设备"), card));
    layout->addSpacing(5);
    camera_combo_ = new StyledComboBox(card);
    camera_combo_->setFixedHeight(36);
    const std::vector<hmmob::CameraSource::CameraDeviceInfo> devices =
        hmmob::CameraSource::EnumerateDevices();
    if (devices.empty()) {
        camera_combo_->addItem(QStringLiteral("未检测到摄像头"));
        camera_combo_->setEnabled(false);
    } else {
        for (size_t i = 0; i < devices.size(); ++i) {
            camera_combo_->addItem(QString::fromStdString(devices[i].name),
                                   static_cast<int>(i));
        }
        const int index = camera_combo_->findData(camera_index);
        camera_combo_->setCurrentIndex(index >= 0 ? index : 0);
    }
    layout->addWidget(camera_combo_);
    layout->addStretch(1);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    buttons->addStretch(1);
    auto* cancel = new QPushButton(QStringLiteral("取消"), card);
    cancel->setObjectName(QStringLiteral("secondary"));
    cancel->setCursor(Qt::PointingHandCursor);
    save_button_ = new QPushButton(QStringLiteral("保存"), card);
    save_button_->setObjectName(QStringLiteral("primary"));
    save_button_->setCursor(Qt::PointingHandCursor);
    save_button_->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(save_button_);
    layout->addLayout(buttons);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save_button_, &QPushButton::clicked, this, &QDialog::accept);
    connect(container_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { UpdateCompatibility(); });
    connect(audio_codec_combo_,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { UpdateCompatibility(); });
    UpdateCompatibility();
}

void SettingsDialog::UpdateCompatibility() {
    const QString extension = container_combo_->currentData().toString();
    const auto audio_codec = static_cast<hmmob::AudioCodec>(
        audio_codec_combo_->currentData().toInt());

    QString hint;
    if (hevc_ && (extension == QStringLiteral("flv") ||
                  extension == QStringLiteral("avi") ||
                  extension == QStringLiteral("wmv"))) {
        hint = QStringLiteral(
            "HEVC 不支持该保存格式，请改用 H.264 或更换格式");
    } else if (extension == QStringLiteral("flv") &&
               audio_codec == hmmob::AudioCodec::PcmS16Le) {
        hint = QStringLiteral(
            "FLV 保存格式不支持 PCM 音频，请改用 AAC 或更换格式");
    }

    compatibility_hint_->setText(hint);
    save_button_->setEnabled(hint.isEmpty());
    audio_bitrate_combo_->setEnabled(audio_codec == hmmob::AudioCodec::Aac);
}

int64_t SettingsDialog::bitrate() const {
    return bitrate_combo_->currentData().toLongLong();
}

bool SettingsDialog::hdr() const { return hdr_check_->isChecked(); }

int SettingsDialog::cameraIndex() const {
    if (!camera_combo_->isEnabled()) {
        return 0;
    }
    return camera_combo_->currentData().toInt();
}

QString SettingsDialog::containerExtension() const {
    return container_combo_->currentData().toString();
}

hmmob::AudioCodec SettingsDialog::audioCodec() const {
    return static_cast<hmmob::AudioCodec>(
        audio_codec_combo_->currentData().toInt());
}

int SettingsDialog::audioSampleRate() const {
    return sample_rate_combo_->currentData().toInt();
}

int SettingsDialog::audioBitrate() const {
    return audio_bitrate_combo_->currentData().toInt();
}
