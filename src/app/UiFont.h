#pragma once

#include <QFont>
#include <QString>

// Single source of truth for the UI typeface, so painted widgets and
// stylesheet-driven widgets stay on the same font.
inline QFont UiFont(int pixel_size, QFont::Weight weight = QFont::Normal) {
    QFont font;
    font.setFamilies({QStringLiteral("Microsoft YaHei UI"),
                      QStringLiteral("Microsoft YaHei")});
    font.setPixelSize(pixel_size);
    font.setWeight(weight);
    return font;
}
