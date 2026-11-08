#pragma once

#include <QIcon>
#include <QPixmap>

// Application mark: a rounded blue gradient square with a white ring and play
// triangle. Everything is painted with QPainter so the UI needs no image
// assets or image-format plugins at run time.
namespace Logo {

// Renders the mark at size logical pixels, honouring the primary screen's
// device pixel ratio.
QPixmap Pixmap(int size);

// Multi-size icon (16..256) for window, taskbar and tray use.
QIcon Icon();

}  // namespace Logo
