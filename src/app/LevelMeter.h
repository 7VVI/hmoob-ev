#pragma once

#include <QWidget>

class QTimer;

// Eight rounded bars whose lit count drifts a little over time, so the audio
// rows look alive even though the recorder does not expose per-source levels.
class LevelMeter : public QWidget {
    Q_OBJECT

public:
    explicit LevelMeter(int seed = 0, QWidget* parent = nullptr);

    void SetActive(bool active);
    [[nodiscard]] bool IsActive() const { return active_; }

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void Tick();

    QTimer* timer_ = nullptr;
    int seed_ = 0;
    int lit_ = 5;
    int target_lit_ = 5;
    bool active_ = true;
};
