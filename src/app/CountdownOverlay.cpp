#include "app/CountdownOverlay.h"

#include <QEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QTimer>

#include "app/Theme.h"
#include "app/UiFont.h"

namespace {

constexpr qreal kCardWidth = 320.0;
constexpr qreal kCardHeight = 280.0;
constexpr qreal kCardRadius = 20.0;
constexpr qreal kButtonWidth = 104.0;
constexpr qreal kButtonHeight = 38.0;
constexpr qreal kButtonMargin = 26.0;

}  // namespace

CountdownOverlay::CountdownOverlay(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);

    cancel_button_ = new QPushButton(QStringLiteral("取消"), this);
    cancel_button_->setCursor(Qt::PointingHandCursor);
    cancel_button_->setFocusPolicy(Qt::NoFocus);
    cancel_button_->setFixedSize(static_cast<int>(kButtonWidth),
                                 static_cast<int>(kButtonHeight));
    connect(cancel_button_, &QPushButton::clicked, this,
            &CountdownOverlay::Cancel);

    timer_ = new QTimer(this);
    timer_->setInterval(1000);
    timer_->setTimerType(Qt::PreciseTimer);
    connect(timer_, &QTimer::timeout, this, [this] {
        if (!active_) {
            return;
        }
        --remaining_;
        if (remaining_ <= 0) {
            timer_->stop();
            active_ = false;
            hide();
            emit finished();
            return;
        }
        update();
        emit tick(remaining_);
    });

    connect(&Theme::Instance(), &Theme::changed, this,
            [this] { ApplyStyle(); });

    if (parent) {
        parent->installEventFilter(this);
    }
    ApplyStyle();
    hide();
}

void CountdownOverlay::ApplyStyle() {
    cancel_button_->setStyleSheet(Theme::Instance().Expand(QStringLiteral(
        "QPushButton { background: {primary}; border: none; border-radius: 9px;"
        " color: #FFFFFF; font-size: 14px; }"
        "QPushButton:hover { background: {primaryHover}; }"
        "QPushButton:pressed { background: {primaryPressed}; }")));
    update();
}

QRectF CountdownOverlay::CardRect() const {
    return QRectF((width() - kCardWidth) / 2.0,
                  (height() - kCardHeight) / 2.0, kCardWidth, kCardHeight);
}

void CountdownOverlay::Start(int seconds) {
    if (seconds <= 0) {
        return;
    }
    remaining_ = seconds;
    active_ = true;
    if (parentWidget()) {
        setGeometry(parentWidget()->rect());
    }
    show();
    raise();
    setFocus(Qt::OtherFocusReason);
    update();
    emit tick(remaining_);
    timer_->start();
}

void CountdownOverlay::Cancel() {
    if (!active_) {
        return;
    }
    timer_->stop();
    active_ = false;
    remaining_ = 0;
    hide();
    emit cancelled();
}

void CountdownOverlay::Stop() {
    timer_->stop();
    active_ = false;
    remaining_ = 0;
    hide();
}

void CountdownOverlay::paintEvent(QPaintEvent* event) {
    (void)event;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Subtle scrim so the live preview stays visible underneath. Rounded to
    // match the preview card's own 14 px corner radius.
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(6, 8, 12, 120));
    painter.drawRoundedRect(QRectF(rect()), 14.0, 14.0);

    const QRectF card = CardRect();
    painter.setPen(QPen(QColor(255, 255, 255, 30), 1.0));
    painter.setBrush(QColor(19, 21, 27, 234));
    painter.drawRoundedRect(card, kCardRadius, kCardRadius);

    painter.setFont(UiFont(88, QFont::Bold));
    painter.setPen(QColor(Qt::white));
    painter.drawText(QRectF(card.left(), card.top() + 30.0, card.width(), 124.0),
                     Qt::AlignCenter, QString::number(remaining_));

    painter.setFont(UiFont(14));
    painter.setPen(QColor(255, 255, 255, 190));
    painter.drawText(
        QRectF(card.left(), card.top() + 164.0, card.width(), 24.0),
        Qt::AlignCenter, QStringLiteral("即将开始录制"));
}

void CountdownOverlay::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        event->accept();
        Cancel();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool CountdownOverlay::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        setGeometry(parentWidget()->rect());
    }
    return QWidget::eventFilter(watched, event);
}

void CountdownOverlay::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    const QRectF card = CardRect();
    cancel_button_->move(
        static_cast<int>(card.center().x() - kButtonWidth / 2.0),
        static_cast<int>(card.bottom() - kButtonMargin - kButtonHeight));
    cancel_button_->raise();
}
