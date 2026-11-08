#include "app/LevelMeter.h"

#include <QPainter>
#include <QPaintEvent>
#include <QRandomGenerator>
#include <QTimer>

namespace {
constexpr int kBars = 8;
constexpr int kBarWidth = 6;
constexpr int kBarGap = 5;
constexpr int kBarHeight = 22;
}  // namespace

LevelMeter::LevelMeter(int seed, QWidget* parent)
    : QWidget(parent), seed_(seed), lit_(4 + seed % 3),
      target_lit_(4 + seed % 3) {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setFixedSize(sizeHint());
    timer_ = new QTimer(this);
    timer_->setInterval(150);
    connect(timer_, &QTimer::timeout, this, &LevelMeter::Tick);
    timer_->start();
}

QSize LevelMeter::sizeHint() const {
    return QSize(kBars * kBarWidth + (kBars - 1) * kBarGap, kBarHeight);
}

void LevelMeter::SetActive(bool active) {
    active_ = active;
    update();
}

void LevelMeter::Tick() {
    if (!active_) {
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

    const QColor lit(0x2F, 0x6B, 0xFF);
    const QColor unlit(0xE5, 0xE6, 0xEB);
    const int y = (height() - kBarHeight) / 2;
    for (int i = 0; i < kBars; ++i) {
        const bool on = active_ && i < lit_;
        painter.setBrush(on ? lit : unlit);
        painter.drawRoundedRect(
            QRectF(i * (kBarWidth + kBarGap), y, kBarWidth, kBarHeight),
            kBarWidth / 2.0, kBarWidth / 2.0);
    }
}
