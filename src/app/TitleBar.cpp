#include "app/TitleBar.h"

#include <QAbstractButton>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>

#include "app/IconRenderer.h"

namespace {

class WindowButton : public QAbstractButton {
public:
    WindowButton(Icon icon, const QColor& hover_color, bool close_style,
                 QWidget* parent)
        : QAbstractButton(parent),
          icon_(icon),
          hover_color_(hover_color),
          close_style_(close_style) {
        setCursor(Qt::ArrowCursor);
        setFocusPolicy(Qt::NoFocus);
        setFixedSize(46, 56);
        setAttribute(Qt::WA_Hover, true);
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
        if (hover_) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(hover_color_);
            painter.drawRect(rect());
        }
        const QColor color =
            (close_style_ && hover_) ? QColor(Qt::white)
                                     : QColor(0x3A, 0x42, 0x54);
        IconRenderer::Paint(painter, icon_,
                            QRectF((width() - 16.0) / 2.0,
                                   (height() - 16.0) / 2.0, 16, 16),
                            color, 1.5);
    }

private:
    Icon icon_;
    QColor hover_color_;
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

    auto* mark = new QLabel(this);
    mark->setPixmap(IconRenderer::Pixmap(Icon::Play, 30, QColor(0x2F, 0x6B, 0xFF)));
    mark->setFixedSize(30, 30);
    layout->addWidget(mark, 0, Qt::AlignVCenter);
    layout->addSpacing(14);

    title_label_ = new QLabel(QStringLiteral("Hmmob 录屏"), this);
    QFont title_font = title_label_->font();
    title_font.setPixelSize(18);
    title_font.setWeight(QFont::DemiBold);
    title_label_->setFont(title_font);
    title_label_->setStyleSheet(QStringLiteral("color:#1D2129;"));
    layout->addWidget(title_label_, 0, Qt::AlignVCenter);

    layout->addStretch(1);

    auto* minimize = new WindowButton(Icon::Minimize, QColor(0xEC, 0xEF, 0xF5),
                                      false, this);
    minimize->setToolTip(QStringLiteral("最小化"));
    auto* maximize = new WindowButton(Icon::Maximize, QColor(0xEC, 0xEF, 0xF5),
                                      false, this);
    maximize->setToolTip(QStringLiteral("最大化"));
    auto* close = new WindowButton(Icon::Close, QColor(0xE8, 0x11, 0x23), true,
                                   this);
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
    painter.fillRect(rect(), QColor(0xF5, 0xF7, 0xFC));
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
        window()->move(event->globalPosition().toPoint() - drag_offset_);
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
