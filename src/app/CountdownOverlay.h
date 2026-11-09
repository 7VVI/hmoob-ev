#pragma once

#include <QWidget>

class QPushButton;
class QTimer;

// Dark translucent countdown shown over the preview card after the capture
// source is resolved and before Recorder::Start(). It owns the one-second
// timer: Start(n) immediately shows n, then counts n-1 ... 1 and emits
// finished() one second after 1 was shown. 取消 / Esc cancels.
class CountdownOverlay : public QWidget {
    Q_OBJECT

public:
    explicit CountdownOverlay(QWidget* parent = nullptr);

    void Start(int seconds);
    // Cancels and notifies the owner through cancelled().
    void Cancel();
    // Stops and hides without emitting a signal (used while starting the
    // recorder after finished()).
    void Stop();

    [[nodiscard]] bool IsActive() const { return active_; }
    [[nodiscard]] int RemainingSeconds() const { return remaining_; }

signals:
    void tick(int remaining);
    void finished();
    void cancelled();

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    [[nodiscard]] QRectF CardRect() const;
    void ApplyStyle();

    QTimer* timer_ = nullptr;
    QPushButton* cancel_button_ = nullptr;
    int remaining_ = 0;
    bool active_ = false;
};
