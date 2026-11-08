#pragma once

#include <QAbstractButton>
#include <QString>

#include "app/IconRenderer.h"

// Top navigation tab: 24 px line icon + label, with a light-blue pill and a
// short blue underline when active.
class NavTabButton : public QAbstractButton {
    Q_OBJECT

public:
    NavTabButton(Icon icon, const QString& text, QWidget* parent = nullptr);

    void SetActive(bool active);
    [[nodiscard]] bool IsActive() const { return active_; }

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    Icon icon_;
    QString text_;
    bool active_ = false;
    bool hover_ = false;
};
