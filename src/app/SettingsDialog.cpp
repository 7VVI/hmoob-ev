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
QLabel#fieldLabel { font-size: 14px; color: #1D2129; }
QLabel#fieldHint { font-size: 12px; color: #86909C; }
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

}  // namespace

SettingsDialog::SettingsDialog(int64_t bitrate, bool hdr, int camera_index,
                               QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("设置"));
    setModal(true);
    setFont(UiFont(14));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QString::fromUtf8(kSettingsStyle));
    setFixedSize(420, 380);

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

    layout->addWidget(MakeFieldLabel(QStringLiteral("码率"), card));
    layout->addSpacing(8);
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
    layout->addSpacing(5);
    layout->addWidget(
        MakeFieldHint(QStringLiteral("视频编码的平均码率"), card));
    layout->addSpacing(16);

    hdr_check_ = new ToggleCheckBox(QStringLiteral("HDR（色调映射为 SDR）"),
                                    card);
    hdr_check_->setChecked(hdr);
    layout->addWidget(hdr_check_);
    layout->addSpacing(4);
    auto* hdr_hint = MakeFieldHint(QStringLiteral("仅全屏录制生效"), card);
    hdr_hint->setContentsMargins(26, 0, 0, 0);
    layout->addWidget(hdr_hint);
    layout->addSpacing(16);

    layout->addWidget(MakeFieldLabel(QStringLiteral("摄像头设备"), card));
    layout->addSpacing(8);
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
    layout->addSpacing(5);
    layout->addWidget(
        MakeFieldHint(QStringLiteral("摄像头录制时使用"), card));
    layout->addStretch(1);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    buttons->addStretch(1);
    auto* cancel = new QPushButton(QStringLiteral("取消"), card);
    cancel->setObjectName(QStringLiteral("secondary"));
    cancel->setCursor(Qt::PointingHandCursor);
    auto* save = new QPushButton(QStringLiteral("保存"), card);
    save->setObjectName(QStringLiteral("primary"));
    save->setCursor(Qt::PointingHandCursor);
    save->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(save);
    layout->addLayout(buttons);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, &QDialog::accept);
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
