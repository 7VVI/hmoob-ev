#pragma once

#include <QColor>
#include <QIcon>
#include <QRectF>

class QPainter;
class QPixmap;

// Every glyph in the window is drawn with QPainter; the application ships no
// image assets and does not link Qt SVG. Icons are authored on a 24x24 grid
// and scaled to the requested rect.
enum class Icon {
    Play,
    Monitor,
    Region,
    Window,
    Camera,
    Webcam,
    Gear,
    Folder,
    Menu,
    Mic,
    Speaker,
    SpeakerMuted,
    Hd,
    Film,
    Info,
    Clock,
    File,
    Minimize,
    Maximize,
    Restore,
    Close,
    Check,
    ChevronDown,
    ChevronUp,
    RecordDot,
    StopSquare,
    Keyboard,
    Sun,
    Moon,
    Search,
    Grid,
    ListView,
    Refresh,
    Edit,
    Trash,
    Calendar,
};

namespace IconRenderer {

void Paint(QPainter& painter, Icon icon, const QRectF& rect,
           const QColor& color, qreal stroke = 1.7);

QPixmap Pixmap(Icon icon, int size, const QColor& color, qreal stroke = 1.7);

QIcon QIconFor(Icon icon, int size, const QColor& color, qreal stroke = 1.7);

}  // namespace IconRenderer
