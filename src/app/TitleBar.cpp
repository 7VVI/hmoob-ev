#include "app/TitleBar.h"

#include <QAbstractButton>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QScreen>

#include "app/IconRenderer.h"
#include "app/Theme.h"

namespace {

class WindowButton : public QAbstractButton {
public:
    WindowButton(Icon icon, bool close_style, QWidget* parent)
        : QAbstractButton(parent), icon_(icon), close_style_(close_style) {
        setCursor(Qt::ArrowCursor);
        setFocusPolicy(Qt::NoFocus);
        setFixedSize(46, 56);
        setAttribute(Qt::WA_Hover, true);
        connect(&Theme::Instance(), &Theme::changed, this,
                [this] { update(); });
    }

    void SetIcon(Icon icon) {
        icon_ = icon;
        update();
    }

protected:
    void enterEvent(QEnterEvent* event) override {
        hover_ = true;
        update();
        QAbstractButton::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        hover_ = false;
        update();
        QAbstractButton::leaveEvent(event);
    }

    void paintEvent(QPaintEvent* event) override {
        (void)event;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const ThemeColors& colors = Theme::Instance().colors();
        if (hover_) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(close_style_ ? colors.dangerStrong : colors.hoverBg);
            painter.drawRect(rect());
        }
        const QColor color =
            (close_style_ && hover_) ? QColor(Qt::white) : colors.icon;
        IconRenderer::Paint(painter, icon_,
                            QRectF((width() - 16.0) / 2.0,
                                   (height() - 16.0) / 2.0, 16, 16),
                            color, 1.5);
    }

private:
    Icon icon_;
    bool close_style_ = false;
    bool hover_ = false;
};

}  // namespace

TitleBar::TitleBar(QWidget* parent) : QWidget(parent) {
    setFixedHeight(56);
    setMouseTracking(false);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(30, 0, 0, 0);
    layout->setSpacing(0);

    mark_ = new QLabel(this);
    mark_->setFixedSize(30, 30);
    layout->addWidget(mark_, 0, Qt::AlignVCenter);
    layout->addSpacing(14);

    title_label_ = new QLabel(QStringLiteral("Hmmob 录屏"), this);
    QFont title_font = title_label_->font();
    title_font.setPixelSize(18);
    title_font.setWeight(QFont::DemiBold);
    title_label_->setFont(title_font);
    layout->addWidget(title_label_, 0, Qt::AlignVCenter);

    layout->addStretch(1);

    auto* minimize = new WindowButton(Icon::Minimize, false, this);
    minimize->setToolTip(QStringLiteral("最小化"));
    auto* maximize = new WindowButton(Icon::Maximize, false, this);
    maximize->setToolTip(QStringLiteral("最大化"));
    auto* close = new WindowButton(Icon::Close, true, this);
    close->setToolTip(QStringLiteral("关闭"));
    minimize_button_ = minimize;
    maximize_button_ = maximize;
    close_button_ = close;
    layout->addWidget(minimize);
    layout->addWidget(maximize);
    layout->addWidget(close);
    layout->addSpacing(4);

    connect(minimize, &QAbstractButton::clicked, this,
            &TitleBar::MinimizeRequested);
    connect(maximize, &QAbstractButton::clicked, this,
            &TitleBar::MaximizeRequested);
    connect(close, &QAbstractButton::clicked, this, &TitleBar::CloseRequested);
    connect(&Theme::Instance(), &Theme::changed, this, &TitleBar::ApplyTheme);
    ApplyTheme();
}

void TitleBar::ApplyTheme() {
    const ThemeColors& colors = Theme::Instance().colors();
    if (mark_) {
        mark_->setPixmap(IconRenderer::Pixmap(Icon::Play, 30, colors.primary));
    }
    if (title_label_) {
        title_label_->setStyleSheet(
            QStringLiteral("color:%1;").arg(colors.text.name()));
    }
    update();
}

void TitleBar::SetMaximized(bool maximized) {
    maximized_ = maximized;
    static_cast<WindowButton*>(maximize_button_)
        ->SetIcon(maximized ? Icon::Restore : Icon::Maximize);
    maximize_button_->setToolTip(maximized ? QStringLiteral("还原")
                                           : QStringLiteral("最大化"));
}

void TitleBar::paintEvent(QPaintEvent* event) {
    (void)event;
    QPainter painter(this);
    painter.fillRect(rect(), Theme::Instance().colors().titleBar);
}

void TitleBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && !maximized_) {
        dragging_ = true;
        drag_offset_ =
            event->globalPosition().toPoint() - window()->frameGeometry().topLeft();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void TitleBar::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_ && (event->buttons() & Qt::LeftButton)) {
        const QPoint target =
            event->globalPosition().toPoint() - drag_offset_;
        // Keep enough of the caption on the nearest screen that the window can
        // always be grabbed and moved back.
        constexpr int kVisible = 80;
        QScreen* screen = QGuiApplication::screenAt(
            event->globalPosition().toPoint());
        if (!screen) {
            screen = QGuiApplication::screenAt(window()->frameGeometry().center());
        }
        if (!screen) {
            screen = QGuiApplication::primaryScreen();
        }
        if (screen) {
            const QRect available = screen->availableGeometry();
            const int max_x = available.right() - kVisible;
            const int min_x =
                available.left() - window()->width() + kVisible;
            const int max_y = available.bottom() - 40;
            window()->move(qBound(min_x, target.x(), max_x),
                           qBound(available.top(), target.y(), max_y));
        } else {
            window()->move(target);
        }
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void TitleBar::mouseReleaseEvent(QMouseEvent* event) {
    if (dragging_) {
        dragging_ = false;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit MaximizeRequested();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}
