#include "app/Controls.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>

#include "app/IconRenderer.h"
#include "app/UiFont.h"

StyledComboBox::StyledComboBox(QWidget* parent) : QComboBox(parent) {
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    setMinimumContentsLength(7);
}

void StyledComboBox::paintEvent(QPaintEvent* event) {
    QComboBox::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF arrow(width() - 30.0, (height() - 16.0) / 2.0, 16, 16);
    IconRenderer::Paint(painter, Icon::ChevronDown, arrow,
                        QColor(0x86, 0x90, 0x9C), 1.8);
}

ToggleCheckBox::ToggleCheckBox(const QString& text, QWidget* parent)
    : QCheckBox(text, parent) {
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_Hover, true);
}

QSize ToggleCheckBox::sizeHint() const {
    const QFontMetrics metrics(UiFont(14));
    return QSize(18 + 8 + metrics.horizontalAdvance(text()), 26);
}

QSize ToggleCheckBox::minimumSizeHint() const {
    return sizeHint();
}

void ToggleCheckBox::paintEvent(QPaintEvent* event) {
    (void)event;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF box(0.0, (height() - 18.0) / 2.0, 18, 18);
    if (isChecked()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0x2F, 0x6B, 0xFF));
        painter.drawRoundedRect(box, 4.0, 4.0);
        IconRenderer::Paint(painter, Icon::Check, box.adjusted(2.5, 2.5, -2.5, -2.5),
                            QColor(Qt::white), 2.6);
    } else {
        painter.setPen(QPen(QColor(0xC9, 0xCD, 0xD4), 1.2));
        painter.setBrush(QColor(Qt::white));
        painter.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), 4.0, 4.0);
    }

    painter.setFont(UiFont(14));
    painter.setPen(QColor(0x1D, 0x21, 0x29));
    painter.drawText(QRectF(26, 0, width() - 26, height()),
                     Qt::AlignLeft | Qt::AlignVCenter, text());
}

RecordButton::RecordButton(QWidget* parent) : QAbstractButton(parent) {
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_Hover, true);
    setFixedSize(sizeHint());
}

void RecordButton::SetRecording(bool recording) {
    if (recording_ == recording) {
        return;
    }
    recording_ = recording;
    update();
}

void RecordButton::SetBusy(bool busy) {
    if (busy_ == busy) {
        return;
    }
    busy_ = busy;
    update();
}

QSize RecordButton::sizeHint() const {
    return QSize(230, 56);
}

void RecordButton::enterEvent(QEnterEvent* event) {
    hover_ = true;
    update();
    QAbstractButton::enterEvent(event);
}

void RecordButton::leaveEvent(QEvent* event) {
    hover_ = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void RecordButton::paintEvent(QPaintEvent* event) {
    (void)event;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QColor background(0x2F, 0x6B, 0xFF);
    if (busy_ || !isEnabled()) {
        background = QColor(0xA9, 0xC0, 0xFF);
    } else if (recording_) {
        background = hover_ ? QColor(0xD9, 0x3A, 0x40) : QColor(0xE5, 0x48, 0x4D);
    } else if (isDown()) {
        background = QColor(0x1E, 0x52, 0xE8);
    } else if (hover_) {
        background = QColor(0x24, 0x5B, 0xF0);
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawRoundedRect(QRectF(rect()), 12.0, 12.0);

    const QString label =
        recording_ ? QStringLiteral("停止录制") : QStringLiteral("开始录制");
    const QFont font = UiFont(18, QFont::DemiBold);
    painter.setFont(font);
    const QFontMetrics metrics(font);
    const int dot = 20;
    const int gap = 12;
    const int text_width = metrics.horizontalAdvance(label);
    const int group_width = dot + gap + text_width;
    const qreal x = (width() - group_width) / 2.0;
    const QRectF dot_rect(x, (height() - dot) / 2.0, dot, dot);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(Qt::white));
    if (recording_) {
        IconRenderer::Paint(painter, Icon::StopSquare, dot_rect,
                            QColor(Qt::white));
    } else {
        IconRenderer::Paint(painter, Icon::RecordDot, dot_rect,
                            QColor(Qt::white));
    }
    painter.setPen(QColor(Qt::white));
    painter.drawText(QRectF(x + dot + gap, 0, text_width + 4, height()),
                     Qt::AlignLeft | Qt::AlignVCenter, label);
}
