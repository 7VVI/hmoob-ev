#pragma once

#include <QAbstractButton>
#include <QString>

#include "app/IconRenderer.h"

// One of the four capture-mode tiles in the 录制内容 card.
class ModeTile : public QAbstractButton {
    Q_OBJECT

public:
    ModeTile(Icon icon, const QString& text, QWidget* parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    Icon icon_;
    QString text_;
    bool hover_ = false;
};
