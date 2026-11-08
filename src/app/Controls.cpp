#include "app/Controls.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>

#include "app/IconRenderer.h"
#include "app/Theme.h"
#include "app/UiFont.h"

StyledComboBox::StyledComboBox(QWidget* parent) : QComboBox(parent) {
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    setMinimumContentsLength(7);
    connect(&Theme::Instance(), &Theme::changed, this,
            [this] { update(); });
}

void StyledComboBox::paintEvent(QPaintEvent* event) {
    QComboBox::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF arrow(width() - 30.0, (height() - 16.0) / 2.0, 16, 16);
    IconRenderer::Paint(painter, Icon::ChevronDown, arrow,
                        Theme::Instance().colors().iconMuted, 1.8);
}

ToggleCheckBox::ToggleCheckBox(const QString& text, QWidget* parent)
    : QCheckBox(text, parent) {
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_Hover, true);
    connect(&Theme::Instance(), &Theme::changed, this,
            [this] { update(); });
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

    const ThemeColors& colors = Theme::Instance().colors();
    const QRectF box(0.0, (height() - 18.0) / 2.0, 18, 18);
    if (isChecked()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.primary);
        painter.drawRoundedRect(box, 4.0, 4.0);
        IconRenderer::Paint(painter, Icon::Check, box.adjusted(2.5, 2.5, -2.5, -2.5),
                            QColor(Qt::white), 2.6);
    } else {
        painter.setPen(QPen(colors.inputBorder, 1.2));
        painter.setBrush(colors.inputBg);
        painter.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), 4.0, 4.0);
    }

    painter.setFont(UiFont(14));
    painter.setPen(isEnabled() ? colors.text : colors.disabledText);
    painter.drawText(QRectF(26, 0, width() - 26, height()),
                     Qt::AlignLeft | Qt::AlignVCenter, text());
}

RecordButton::RecordButton(QWidget* parent) : QAbstractButton(parent) {
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_Hover, true);
    setFixedSize(sizeHint());
    connect(&Theme::Instance(), &Theme::changed, this,
            [this] { update(); });
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

    const ThemeColors& colors = Theme::Instance().colors();
    QColor background = colors.primary;
    if (busy_ || !isEnabled()) {
        background = colors.primaryDisabled;
    } else if (recording_) {
        background = hover_ ? colors.dangerHover : colors.danger;
    } else if (isDown()) {
        background = colors.primaryPressed;
    } else if (hover_) {
        background = colors.primaryHover;
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
