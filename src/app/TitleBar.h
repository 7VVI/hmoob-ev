#pragma once

#include <QWidget>

class QAbstractButton;
class QLabel;
class QMouseEvent;

// Custom caption bar for the frameless window: app mark, title, drag and
// double-click-to-maximize, plus minimize / maximize / close buttons.
class TitleBar : public QWidget {
    Q_OBJECT

public:
    explicit TitleBar(QWidget* parent = nullptr);

    void SetMaximized(bool maximized);
    // Caption text; the main window keeps the default "Hmmob 录屏".
    void SetTitle(const QString& title);
    void ApplyTheme();

signals:
    void MinimizeRequested();
    void MaximizeRequested();
    void CloseRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    QLabel* mark_ = nullptr;
    QLabel* title_label_ = nullptr;
    QAbstractButton* minimize_button_ = nullptr;
    QAbstractButton* maximize_button_ = nullptr;
    QAbstractButton* close_button_ = nullptr;
    bool maximized_ = false;
    bool dragging_ = false;
    QPoint drag_offset_;
};
