#pragma once

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>

// QComboBox with a vector chevron instead of the platform-provided arrow.
class StyledComboBox : public QComboBox {
    Q_OBJECT

public:
    explicit StyledComboBox(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
    // A closed combo consumes no wheel input, so scrolling a page never
    // changes a value by accident. With the popup open the wheel still
    // scrolls the list as usual.
    void wheelEvent(QWheelEvent* event) override;
};

// Checkbox with a hand-drawn rounded box and check mark.
class ToggleCheckBox : public QCheckBox {
    Q_OBJECT

public:
    explicit ToggleCheckBox(const QString& text, QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
};

// The large primary action: blue "开始录制" with a record dot, red
// "停止录制" with a stop square.
class RecordButton : public QAbstractButton {
    Q_OBJECT

public:
    explicit RecordButton(QWidget* parent = nullptr);

    void SetRecording(bool recording);
    void SetBusy(bool busy);
    [[nodiscard]] bool IsRecording() const { return recording_; }

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    bool recording_ = false;
    bool busy_ = false;
    bool hover_ = false;
};
