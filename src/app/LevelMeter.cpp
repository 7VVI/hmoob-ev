#include "app/LevelMeter.h"

#include <QPainter>
#include <QPaintEvent>
#include <QRandomGenerator>
#include <QTimer>

#include "app/Theme.h"

namespace {
constexpr int kBars = 8;
constexpr int kBarWidth = 6;
constexpr int kBarGap = 5;
constexpr int kBarHeight = 22;
}  // namespace

LevelMeter::LevelMeter(int seed, QWidget* parent)
    : QWidget(parent), lit_(4 + seed % 3), target_lit_(4 + seed % 3) {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setFixedSize(sizeHint());
    timer_ = new QTimer(this);
    timer_->setInterval(150);
    connect(timer_, &QTimer::timeout, this, &LevelMeter::Tick);
    timer_->start();
    connect(&Theme::Instance(), &Theme::changed, this,
            [this] { update(); });
}

QSize LevelMeter::sizeHint() const {
    return QSize(kBars * kBarWidth + (kBars - 1) * kBarGap, kBarHeight);
}

void LevelMeter::SetActive(bool active) {
    active_ = active;
    if (active_ && !paused_) {
        timer_->start();
    } else if (!active_) {
        timer_->stop();
    }
    update();
}

void LevelMeter::SetPaused(bool paused) {
    paused_ = paused;
    if (paused_) {
        timer_->stop();
    } else if (active_) {
        timer_->start();
    }
}

void LevelMeter::Tick() {
    if (!active_ || paused_) {
        return;
    }
    // Wander between four and seven lit bars. The step is deliberately small
    // so the meter feels like a level display instead of a progress bar.
    auto* rng = QRandomGenerator::global();
    if (lit_ == target_lit_ || rng->bounded(3) == 0) {
        const int step = rng->bounded(3) - 1;
        target_lit_ = qBound(4, target_lit_ + step, 7);
        if (target_lit_ == lit_) {
            target_lit_ = qBound(4, lit_ + (lit_ >= 6 ? -1 : 1), 7);
        }
    }
    if (lit_ < target_lit_) {
        ++lit_;
    } else if (lit_ > target_lit_) {
        --lit_;
    }
    update();
}

void LevelMeter::paintEvent(QPaintEvent* event) {
    (void)event;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);

    const ThemeColors& colors = Theme::Instance().colors();
    const QColor lit = colors.primary;
    const QColor unlit = colors.tileBorder;
    const int y = (height() - kBarHeight) / 2;
    for (int i = 0; i < kBars; ++i) {
        const bool on = active_ && i < lit_;
        painter.setBrush(on ? lit : unlit);
        painter.drawRoundedRect(
            QRectF(i * (kBarWidth + kBarGap), y, kBarWidth, kBarHeight),
            kBarWidth / 2.0, kBarWidth / 2.0);
    }
}
