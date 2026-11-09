#include "app/FileManagerWindow.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStringConverter>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>
#include <algorithm>
#include <cmath>
#include <cstdio>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <dwmapi.h>
#include <windows.h>
#include <windowsx.h>

#include "app/AppSettings.h"
#include "app/Controls.h"
#include "app/IconRenderer.h"
#include "app/TitleBar.h"
#include "app/UiFont.h"

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWCP_DONOTROUND
#define DWMWCP_DONOTROUND 1
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

namespace {

constexpr int kWindowWidth = 1180;
constexpr int kWindowHeight = 760;
constexpr int kWindowMinWidth = 900;
constexpr int kWindowMinHeight = 600;
constexpr int kDetailsWidth = 340;
constexpr int kGridSpacing = 16;
constexpr int kGridScrollbarReserve = 10;
constexpr int kMinCardWidth = 168;
constexpr int kResizeBorder = 7;
constexpr int kThumbWidth = 480;
constexpr int kThumbHeight = 270;

const char* const kFileManagerStyleTemplate = R"(
QWidget#fmRoot { background: {window}; }
QWidget#fmHeader { background: {window}; }
QWidget#fmContent { background: transparent; }
QLabel#libraryTitle { font-size: 20px; font-weight: 600; color: {text}; }
QLabel#libraryCount { font-size: 14px; color: {textMuted}; }
QLabel#emptyTitle { font-size: 17px; font-weight: 600; color: {textSecondary}; }
QLabel#emptyHint { font-size: 13px; color: {textMuted}; }
QLabel#emptyPath { font-size: 12px; color: {textMuted}; }
QLabel#detailsName { font-size: 16px; font-weight: 600; color: {text}; }
QLabel#detailLabel { font-size: 13px; color: {textMuted}; }
QLabel#detailValue { font-size: 13px; color: {text}; }
QLabel#detailsPlaceholder { font-size: 13px; color: {textMuted}; }
QFrame#detailsCard { background: {card}; border: 1px solid {cardBorder}; border-radius: 12px; }
QStackedWidget#detailsStack { background: transparent; }
QWidget#detailsPage { background: transparent; }
QLineEdit#searchEdit { background: {inputBg}; border: 1px solid {inputBorder}; border-radius: 10px; padding: 0 10px; font-size: 14px; color: {text}; selection-background-color: {primary}; selection-color: #FFFFFF; }
QLineEdit#searchEdit:hover { border-color: {iconMuted}; }
QLineEdit#searchEdit:focus { border: 1px solid {primary}; }
QLineEdit#searchEdit:disabled { background: {disabledBg}; color: {disabledText}; }
QComboBox#sortCombo { background: {inputBg}; border: 1px solid {inputBorder}; border-radius: 10px; padding: 0 8px 0 12px; font-size: 13px; color: {text}; }
QComboBox#sortCombo:hover { border-color: {iconMuted}; }
QComboBox#sortCombo::drop-down { border: none; background: transparent; width: 30px; }
QComboBox#sortCombo::down-arrow { image: none; width: 0px; height: 0px; }
QComboBox QAbstractItemView { background: {menuBg}; border: 1px solid {menuBorder}; border-radius: 8px; padding: 4px; outline: 0; color: {text}; selection-background-color: {selectionBg}; selection-color: {selectionText}; }
QComboBox QAbstractItemView::item { min-height: 30px; padding: 0 10px; }
QComboBox QAbstractItemView::item:hover { background: {hoverBg}; }
QWidget#viewToggle { background: {inputBg}; border: 1px solid {inputBorder}; border-radius: 10px; }
QPushButton#viewSegment { background: transparent; border: none; border-radius: 8px; }
QPushButton#viewSegment:hover { background: {hoverBg}; }
QPushButton#viewSegment:checked { background: {primarySoft}; border: 1px solid {primary}; }
QPushButton#refreshButton { background: transparent; border: none; border-radius: 10px; }
QPushButton#refreshButton:hover { background: {hoverBg}; }
QPushButton#refreshButton:pressed { background: {pressedBg}; }
QPushButton#iconButton { background: transparent; border: none; border-radius: 8px; }
QPushButton#iconButton:hover { background: {hoverBg}; }
QPushButton#actionPrimary { background: {primary}; border: 1px solid {primary}; border-radius: 8px; color: #FFFFFF; font-size: 12px; padding: 0 8px; }
QPushButton#actionPrimary:hover { background: {primaryHover}; }
QPushButton#actionPrimary:pressed { background: {primaryPressed}; }
QPushButton#actionPrimary:disabled { background: {primaryDisabled}; border-color: {primaryDisabled}; color: {primaryDisabledText}; }
QPushButton#actionOutline { background: {card}; border: 1px solid {inputBorder}; border-radius: 8px; color: {textSecondary}; font-size: 12px; padding: 0 8px; }
QPushButton#actionOutline:hover { background: {hoverBg}; }
QPushButton#actionOutline:pressed { background: {pressedBg}; }
QPushButton#actionOutline:disabled { background: {disabledBg}; border-color: {disabledBorder}; color: {disabledText}; }
QPushButton#actionDelete { background: {card}; border: 1px solid {inputBorder}; border-radius: 8px; color: {textSecondary}; font-size: 12px; padding: 0 8px; }
QPushButton#actionDelete:hover { background: {card}; border-color: {danger}; color: {danger}; }
QPushButton#actionDelete:pressed { background: {pressedBg}; }
QPushButton#actionDelete:disabled { background: {disabledBg}; border-color: {disabledBorder}; color: {disabledText}; }
QScrollArea#contentScroll { background: transparent; border: none; }
QScrollArea#contentScroll > QWidget > QWidget { background: transparent; }
QWidget#scrollContent { background: transparent; }
QScrollBar:vertical { background: transparent; width: 8px; margin: 0px; }
QScrollBar::handle:vertical { background: {scrollHandle}; border-radius: 4px; min-height: 28px; }
QScrollBar::handle:vertical:hover { background: {scrollHandleHover}; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; background: transparent; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QToolTip { background: {menuBg}; color: {text}; border: 1px solid {menuBorder}; padding: 4px 8px; }
QMenu { background: {menuBg}; border: 1px solid {menuBorder}; border-radius: 8px; padding: 4px; color: {text}; }
QMessageBox { background: {card}; }
QMessageBox QLabel { color: {text}; }
QInputDialog { background: {card}; }
QInputDialog QLabel { color: {text}; }
QInputDialog QLineEdit { background: {inputBg}; border: 1px solid {inputBorder}; border-radius: 8px; padding: 5px 8px; font-size: 14px; color: {text}; selection-background-color: {primary}; selection-color: #FFFFFF; }
QInputDialog QPushButton { min-height: 32px; padding: 0 16px; border-radius: 8px; background: {card}; border: 1px solid {inputBorder}; color: {text}; }
QInputDialog QPushButton:hover { background: {hoverBg}; }
)";

QString FormatDuration(double seconds) {
    if (!(seconds > 0.0)) {
        return QStringLiteral("--:--");
    }
    const qint64 total = qRound64(seconds);
    const qint64 hours = total / 3600;
    const qint64 minutes = (total / 60) % 60;
    const qint64 secs = total % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'));
}

QString FormatSize(qint64 bytes) {
    if (bytes <= 0) {
        return QStringLiteral("0 KB");
    }
    if (bytes < 1024 * 1024) {
        const double kb = static_cast<double>(bytes) / 1024.0;
        return QStringLiteral("%1 KB").arg(kb, 0, 'f', kb < 10.0 ? 1 : 0);
    }
    const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (mb >= 1024.0) {
        return QStringLiteral("%1 GB").arg(mb / 1024.0, 0, 'f', 2);
    }
    return QStringLiteral("%1 MB").arg(mb, 0, 'f', 1);
}

QString FormatModified(const QDateTime& time) {
    return time.isValid()
               ? time.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
               : QStringLiteral("—");
}

QString FormatResolution(const VideoInfo& info) {
    if (info.width <= 0 || info.height <= 0) {
        return QStringLiteral("—");
    }
    return QStringLiteral("%1 × %2").arg(info.width).arg(info.height);
}

bool IsVideoSuffix(const QString& suffix) {
    static const QStringList kSuffixes = {
        QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("mkv"),
        QStringLiteral("avi"), QStringLiteral("flv"), QStringLiteral("wmv")};
    return kSuffixes.contains(suffix.toLower());
}

void PaintPlayCircle(QPainter& painter, const QRectF& thumbnail) {
    const qreal diameter =
        qBound<qreal>(28.0, qMin(thumbnail.width(), thumbnail.height()) * 0.40,
                      52.0);
    const QPointF center = thumbnail.center();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(15, 23, 42, 155));
    painter.drawEllipse(center, diameter / 2.0, diameter / 2.0);

    const qreal size = diameter * 0.30;
    QPainterPath triangle;
    triangle.moveTo(center.x() - size * 0.42, center.y() - size);
    triangle.lineTo(center.x() + size * 0.92, center.y());
    triangle.lineTo(center.x() - size * 0.42, center.y() + size);
    triangle.closeSubpath();
    painter.setBrush(QColor(255, 255, 255, 240));
    painter.drawPath(triangle);
}

void PaintDurationBadge(QPainter& painter, const QRectF& thumbnail,
                        double duration_seconds) {
    if (!(duration_seconds > 0.0)) {
        return;
    }
    const QFont font = UiFont(11, QFont::DemiBold);
    const QFontMetrics metrics(font);
    const QString text = FormatDuration(duration_seconds);
    const int width = metrics.horizontalAdvance(text) + 12;
    const QRectF badge(thumbnail.right() - width - 8.0,
                       thumbnail.bottom() - 20.0 - 8.0, width, 20.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(15, 23, 42, 176));
    painter.drawRoundedRect(badge, 6.0, 6.0);
    painter.setFont(font);
    painter.setPen(QColor(Qt::white));
    painter.drawText(badge, Qt::AlignCenter, text);
}

// Rounded "cover" thumbnail: scales the frame uniformly and centre-crops it.
// A null image is drawn as a film placeholder.
void PaintThumbnail(QPainter& painter, const QRectF& rect,
                    const QImage& image, bool failed, double duration_seconds,
                    qreal radius) {
    const ThemeColors& colors = Theme::Instance().colors();
    painter.save();
    QPainterPath clip;
    clip.addRoundedRect(rect, radius, radius);
    painter.setClipPath(clip);
    painter.fillRect(rect, colors.previewBg);

    if (!image.isNull()) {
        const QSizeF source = image.size();
        const qreal scale =
            qMax(rect.width() / source.width(), rect.height() / source.height());
        const QSizeF scaled = source * scale;
        const QRectF target(rect.center().x() - scaled.width() / 2.0,
                            rect.center().y() - scaled.height() / 2.0,
                            scaled.width(), scaled.height());
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(target, image);
    } else {
        const qreal icon_size =
            qBound<qreal>(20.0, qMin(rect.width(), rect.height()) * 0.34, 64.0);
        const QRectF icon_rect(rect.center().x() - icon_size / 2.0,
                               rect.center().y() - icon_size / 2.0, icon_size,
                               icon_size);
        painter.setOpacity(failed ? 1.0 : 0.55);
        IconRenderer::Paint(painter, Icon::Film, icon_rect, colors.iconMuted,
                            1.25);
        painter.setOpacity(1.0);
    }
    painter.restore();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    PaintPlayCircle(painter, rect);
    PaintDurationBadge(painter, rect, duration_seconds);
    painter.restore();
}

QString ElidedMiddle(const QFont& font, const QString& text, int width) {
    return QFontMetrics(font).elidedText(text, Qt::ElideMiddle,
                                         qMax(0, width));
}

QString ElidedRight(const QFont& font, const QString& text, int width) {
    return QFontMetrics(font).elidedText(text, Qt::ElideRight,
                                         qMax(0, width));
}

bool ContainsInvalidFileNameCharacter(const QString& name) {
    static const QString kInvalid = QStringLiteral("<>:\"/\\|?*");
    for (const QChar character : name) {
        if (kInvalid.contains(character) || character.unicode() < 0x20) {
            return true;
        }
    }
    return name.endsWith(QLatin1Char('.')) ||
           name.endsWith(QLatin1Char(' '));
}

// Thumbnail decoding opens the file on a worker thread. Windows refuses the
// rename/delete while that handle exists, so retry briefly; a player keeping
// the file open still fails after the timeout.
bool RenameWithRetry(const QString& from, const QString& to) {
    if (QFile::rename(from, to)) {
        return true;
    }
    for (int attempt = 0; attempt < 50; ++attempt) {
        QThread::msleep(10);
        if (QFile::rename(from, to)) {
            return true;
        }
    }
    return false;
}

bool MoveToTrashWithRetry(const QString& path) {
    if (QFile::moveToTrash(path)) {
        return true;
    }
    for (int attempt = 0; attempt < 25; ++attempt) {
        QThread::msleep(20);
        if (QFile::moveToTrash(path)) {
            return true;
        }
    }
    return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// Small painting widgets. They use std::function callbacks instead of signals
// so the file manager stays one translation unit.

class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(Qt::TextElideMode mode, QWidget* parent = nullptr)
        : QLabel(parent), mode_(mode) {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }

    void SetFullText(const QString& text) {
        full_ = text;
        UpdateElidedText();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QLabel::resizeEvent(event);
        UpdateElidedText();
    }

private:
    void UpdateElidedText() {
        QLabel::setText(
            QFontMetrics(font()).elidedText(full_, mode_, qMax(0, width())));
        setToolTip(full_);
    }

    Qt::TextElideMode mode_;
    QString full_;
};

class ThumbnailView : public QWidget {
public:
    explicit ThumbnailView(QWidget* parent = nullptr) : QWidget(parent) {
        setCursor(Qt::PointingHandCursor);
        setMinimumHeight(120);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setToolTip(QStringLiteral("播放"));
        connect(&Theme::Instance(), &Theme::changed, this,
                [this] { update(); });
    }

    void SetState(const QImage& image, bool failed, double duration_seconds) {
        image_ = image;
        failed_ = failed;
        duration_ = duration_seconds;
        update();
    }

    std::function<void()> on_click;

protected:
    void paintEvent(QPaintEvent* event) override {
        (void)event;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        PaintThumbnail(painter, QRectF(rect()), image_, failed_, duration_,
                       10.0);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && rect().contains(
                                                     event->position().toPoint()) &&
            on_click) {
            on_click();
        }
        QWidget::mouseReleaseEvent(event);
    }

private:
    QImage image_;
    bool failed_ = false;
    double duration_ = 0.0;
};

class VideoCard : public QWidget {
public:
    explicit VideoCard(const FileManagerEntry& entry, QWidget* parent)
        : QWidget(parent), entry_(entry) {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        setToolTip(QDir::toNativeSeparators(entry.path));
    }

    [[nodiscard]] QString path() const { return entry_.path; }

    void SetEntry(const FileManagerEntry& entry) {
        entry_ = entry;
        setToolTip(QDir::toNativeSeparators(entry.path));
        update();
    }

    void SetSelected(bool selected) {
        if (selected_ != selected) {
            selected_ = selected;
            update();
        }
    }

    void SetCardWidth(int width) {
        thumb_height_ = qRound((width - 8) * 9.0 / 16.0);
        setFixedSize(width, 4 + thumb_height_ + 9 + 20 + 2 + 17 + 4);
        update();
    }

    std::function<void(const QString&)> clicked;
    std::function<void(const QString&)> activated;

protected:
    void enterEvent(QEnterEvent* event) override {
        hover_ = true;
        update();
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        hover_ = false;
        update();
        QWidget::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            if (clicked) {
                clicked(entry_.path);
            }
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            if (activated) {
                activated(entry_.path);
            }
            event->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    void paintEvent(QPaintEvent* event) override {
        (void)event;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const ThemeColors& colors = Theme::Instance().colors();

        if (selected_) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colors.primarySoft);
            painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                                    12.0, 12.0);
        } else if (hover_) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colors.hoverBg);
            painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                                    12.0, 12.0);
        }

        const QRectF thumbnail(4.0, 4.0, width() - 8.0, thumb_height_);
        PaintThumbnail(painter, thumbnail, entry_.thumbnail, entry_.failed,
                       entry_.info.duration_seconds, 10.0);

        const QFont name_font = UiFont(13, QFont::DemiBold);
        painter.setFont(name_font);
        painter.setPen(colors.text);
        const QRectF name_rect(6.0, thumbnail.bottom() + 9.0, width() - 12.0,
                               20.0);
        painter.drawText(name_rect, Qt::AlignLeft | Qt::AlignVCenter,
                         ElidedMiddle(name_font, entry_.name,
                                      static_cast<int>(name_rect.width())));

        const QFont meta_font = UiFont(12);
        painter.setFont(meta_font);
        painter.setPen(colors.textMuted);
        const QString meta = QStringLiteral("%1 · %2")
                                 .arg(FormatModified(entry_.modified),
                                      FormatSize(entry_.size));
        const QRectF meta_rect(6.0, name_rect.bottom() + 2.0, width() - 12.0,
                               17.0);
        painter.drawText(meta_rect, Qt::AlignLeft | Qt::AlignVCenter,
                         ElidedRight(meta_font, meta,
                                     static_cast<int>(meta_rect.width())));

        if (selected_) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(colors.primary, 2.0));
            painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 12.0,
                                    12.0);
        }
    }

private:
    FileManagerEntry entry_;
    int thumb_height_ = 90;
    bool hover_ = false;
    bool selected_ = false;
};

class VideoRow : public QWidget {
public:
    explicit VideoRow(const FileManagerEntry& entry, QWidget* parent)
        : QWidget(parent), entry_(entry) {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        setFixedHeight(84);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setToolTip(QDir::toNativeSeparators(entry.path));
    }

    [[nodiscard]] QString path() const { return entry_.path; }

    void SetEntry(const FileManagerEntry& entry) {
        entry_ = entry;
        setToolTip(QDir::toNativeSeparators(entry.path));
        update();
    }

    void SetSelected(bool selected) {
        if (selected_ != selected) {
            selected_ = selected;
            update();
        }
    }

    std::function<void(const QString&)> clicked;
    std::function<void(const QString&)> activated;

protected:
    void enterEvent(QEnterEvent* event) override {
        hover_ = true;
        update();
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        hover_ = false;
        update();
        QWidget::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            if (clicked) {
                clicked(entry_.path);
            }
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            if (activated) {
                activated(entry_.path);
            }
            event->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    void paintEvent(QPaintEvent* event) override {
        (void)event;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const ThemeColors& colors = Theme::Instance().colors();

        if (selected_ || hover_) {
            painter.setPen(selected_ ? QPen(colors.primary, 1.5)
                                     : QPen(Qt::NoPen));
            painter.setBrush(selected_ ? colors.primarySoft : colors.hoverBg);
            painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                                    10.0, 10.0);
        }

        const QRectF thumbnail(12.0, 8.0, 120.0, 68.0);
        PaintThumbnail(painter, thumbnail, entry_.thumbnail, entry_.failed,
                       entry_.info.duration_seconds, 8.0);

        const QFont name_font = UiFont(14, QFont::DemiBold);
        const QFont meta_font = UiFont(12);
        const int name_width = width() - 146 - 154;
        painter.setFont(name_font);
        painter.setPen(colors.text);
        painter.drawText(QRectF(146.0, 14.0, name_width, 22.0),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         ElidedMiddle(name_font, entry_.name, name_width));

        painter.setFont(meta_font);
        painter.setPen(colors.textMuted);
        const QString meta = QStringLiteral("%1 · %2")
                                 .arg(FormatModified(entry_.modified),
                                      FormatSize(entry_.size));
        painter.drawText(QRectF(146.0, 38.0, name_width, 18.0),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         ElidedRight(meta_font, meta, name_width));

        const QFont duration_font = UiFont(13);
        painter.setFont(duration_font);
        painter.setPen(colors.textSecondary);
        painter.drawText(QRectF(width() - 142.0, 0.0, 118.0, height()),
                         Qt::AlignRight | Qt::AlignVCenter,
                         FormatDuration(entry_.info.duration_seconds));
    }

private:
    FileManagerEntry entry_;
    bool hover_ = false;
    bool selected_ = false;
};

// ---------------------------------------------------------------------------

ThumbnailLoader::ThumbnailLoader(QObject* parent) : QObject(parent) {}

// Harness-only interceptor for QDesktopServices::openUrl("file:..."), so the
// play path can be verified without launching the system player.
class FileManagerUrlProbe : public QObject {
    Q_OBJECT

public:
    QUrl last_url;
    int count = 0;

public slots:
    void Handle(const QUrl& url) {
        last_url = url;
        ++count;
    }
};

namespace {

// Runs on the thread pool; hands the result back to the UI thread through the
// loader object, which the task keeps alive with a shared_ptr.
class ThumbnailTask : public QRunnable {
public:
    ThumbnailTask(std::shared_ptr<ThumbnailLoader> loader, QString path,
                  qint64 mtime_ms)
        : loader_(std::move(loader)),
          path_(std::move(path)),
          mtime_ms_(mtime_ms) {}

    void run() override {
        VideoInfo info;
        bool ok = ReadVideoInfo(path_, info);
        QImage image;
        if (ok) {
            image = DecodeThumbnail(path_, kThumbWidth, kThumbHeight);
            ok = !image.isNull();
        }
        std::shared_ptr<ThumbnailLoader> loader = loader_;
        const QString path = path_;
        const qint64 mtime_ms = mtime_ms_;
        QMetaObject::invokeMethod(
            loader.get(),
            [loader, path, mtime_ms, image, info, ok] {
                emit loader->Loaded(path, mtime_ms, image, info, ok);
            },
            Qt::QueuedConnection);
    }

private:
    std::shared_ptr<ThumbnailLoader> loader_;
    QString path_;
    qint64 mtime_ms_ = 0;
};

}  // namespace

FileManagerWindow::FileManagerWindow(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("fmRoot"));
    setAttribute(Qt::WA_StyledBackground, true);
    setWindowTitle(QStringLiteral("文件管理"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint |
                   Qt::WindowMinimizeButtonHint |
                   Qt::WindowMaximizeButtonHint | Qt::WindowSystemMenuHint);
    setWindowModality(Qt::NonModal);
    setMinimumSize(kWindowMinWidth, kWindowMinHeight);
    resize(kWindowWidth, kWindowHeight);
    setStyleSheet(Theme::Instance().Expand(
        QString::fromUtf8(kFileManagerStyleTemplate)));

    pool_.setMaxThreadCount(qBound(2, QThread::idealThreadCount(), 4));
    loader_ = std::make_shared<ThumbnailLoader>();
    connect(loader_.get(), &ThumbnailLoader::Loaded, this,
            &FileManagerWindow::OnThumbnailLoaded);

    const AppSettings settings = LoadAppSettings();
    directory_ = settings.output_directory.isEmpty()
                     ? DefaultOutputDirectory()
                     : QDir::cleanPath(
                           QDir::fromNativeSeparators(settings.output_directory));

    BuildUi();
    connect(&Theme::Instance(), &Theme::changed, this,
            &FileManagerWindow::ApplyTheme);
}

FileManagerWindow::~FileManagerWindow() {
    pool_.clear();
    pool_.waitForDone(5000);
}

void FileManagerWindow::BuildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    title_bar_ = new TitleBar(this);
    title_bar_->SetTitle(QStringLiteral("文件管理"));
    root->addWidget(title_bar_);
    connect(title_bar_, &TitleBar::MinimizeRequested, this,
            &QWidget::showMinimized);
    connect(title_bar_, &TitleBar::MaximizeRequested, this,
            &FileManagerWindow::ToggleMaximize);
    connect(title_bar_, &TitleBar::CloseRequested, this, &QWidget::close);

    root->addWidget(BuildHeader());
    root->addWidget(BuildContent(), 1);
    UpdateDetails();
}

QWidget* FileManagerWindow::BuildHeader() {
    auto* header = new QWidget(this);
    header->setObjectName(QStringLiteral("fmHeader"));
    header->setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QVBoxLayout(header);
    layout->setContentsMargins(26, 16, 26, 14);
    layout->setSpacing(12);

    auto* caption = new QHBoxLayout;
    caption->setContentsMargins(0, 0, 0, 0);
    caption->setSpacing(10);
    auto* title = new QLabel(QStringLiteral("全部视频"), header);
    title->setObjectName(QStringLiteral("libraryTitle"));
    count_label_ = new QLabel(QStringLiteral("0"), header);
    count_label_->setObjectName(QStringLiteral("libraryCount"));
    caption->addWidget(title, 0, Qt::AlignBottom);
    caption->addWidget(count_label_, 0, Qt::AlignBottom);
    caption->addStretch(1);
    layout->addLayout(caption);

    auto* controls = new QHBoxLayout;
    controls->setContentsMargins(0, 0, 0, 0);
    controls->setSpacing(12);

    search_edit_ = new QLineEdit(header);
    search_edit_->setObjectName(QStringLiteral("searchEdit"));
    search_edit_->setPlaceholderText(QStringLiteral("搜索视频文件…"));
    // The platform style's clear button may be missing in this Qt build; a
    // themed trailing action is always visible and matches the UI.
    search_edit_->setClearButtonEnabled(false);
    search_edit_->setFocusPolicy(Qt::ClickFocus);
    search_edit_->setFixedHeight(40);
    search_edit_->setMinimumWidth(220);
    search_action_ = search_edit_->addAction(
        IconRenderer::QIconFor(Icon::Search, 18,
                               Theme::Instance().ToneColor(IconTone::Muted)),
        QLineEdit::LeadingPosition);
    clear_action_ = search_edit_->addAction(
        IconRenderer::QIconFor(Icon::Close, 16,
                               Theme::Instance().ToneColor(IconTone::Muted)),
        QLineEdit::TrailingPosition);
    clear_action_->setToolTip(QStringLiteral("清除"));
    clear_action_->setVisible(false);
    connect(clear_action_, &QAction::triggered, this, [this] {
        search_edit_->clear();
        search_edit_->setFocus();
    });
    controls->addWidget(search_edit_, 1);

    sort_combo_ = new StyledComboBox(header);
    sort_combo_->setObjectName(QStringLiteral("sortCombo"));
    sort_combo_->setFixedSize(148, 40);
    sort_combo_->addItem(QStringLiteral("按修改时间"), 0);
    sort_combo_->addItem(QStringLiteral("按名称"), 1);
    sort_combo_->addItem(QStringLiteral("按大小"), 2);
    controls->addWidget(sort_combo_);

    auto* toggle = new QWidget(header);
    toggle->setObjectName(QStringLiteral("viewToggle"));
    toggle->setAttribute(Qt::WA_StyledBackground, true);
    toggle->setFixedSize(84, 40);
    auto* toggle_layout = new QHBoxLayout(toggle);
    toggle_layout->setContentsMargins(3, 3, 3, 3);
    toggle_layout->setSpacing(2);
    grid_button_ = new QPushButton(toggle);
    grid_button_->setObjectName(QStringLiteral("viewSegment"));
    grid_button_->setCheckable(true);
    grid_button_->setCursor(Qt::PointingHandCursor);
    grid_button_->setFocusPolicy(Qt::NoFocus);
    grid_button_->setFixedSize(38, 34);
    grid_button_->setIconSize(QSize(18, 18));
    grid_button_->setToolTip(QStringLiteral("网格视图"));
    list_button_ = new QPushButton(toggle);
    list_button_->setObjectName(QStringLiteral("viewSegment"));
    list_button_->setCheckable(true);
    list_button_->setCursor(Qt::PointingHandCursor);
    list_button_->setFocusPolicy(Qt::NoFocus);
    list_button_->setFixedSize(38, 34);
    list_button_->setIconSize(QSize(18, 18));
    list_button_->setToolTip(QStringLiteral("列表视图"));
    toggle_layout->addWidget(grid_button_);
    toggle_layout->addWidget(list_button_);
    auto* group = new QButtonGroup(toggle);
    group->setExclusive(true);
    group->addButton(grid_button_);
    group->addButton(list_button_);
    grid_button_->setChecked(true);
    controls->addWidget(toggle);

    refresh_button_ = new QPushButton(header);
    refresh_button_->setObjectName(QStringLiteral("refreshButton"));
    refresh_button_->setCursor(Qt::PointingHandCursor);
    refresh_button_->setFocusPolicy(Qt::NoFocus);
    refresh_button_->setFixedSize(40, 40);
    refresh_button_->setIconSize(QSize(20, 20));
    refresh_button_->setToolTip(QStringLiteral("刷新"));
    controls->addWidget(refresh_button_);

    layout->addLayout(controls);

    connect(search_edit_, &QLineEdit::textChanged, this,
            [this](const QString& text) {
                if (clear_action_) {
                    clear_action_->setVisible(!text.isEmpty());
                }
                SetSearchText(text);
            });
    connect(sort_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) { SetSortIndex(index); });
    connect(grid_button_, &QPushButton::clicked, this,
            [this] { SetListMode(false); });
    connect(list_button_, &QPushButton::clicked, this,
            [this] { SetListMode(true); });
    connect(refresh_button_, &QPushButton::clicked, this,
            &FileManagerWindow::Refresh);

    UpdateViewToggleIcons();
    if (search_action_) {
        search_action_->setIcon(IconRenderer::QIconFor(
            Icon::Search, 18, Theme::Instance().ToneColor(IconTone::Muted)));
    }
    refresh_button_->setIcon(IconRenderer::QIconFor(
        Icon::Refresh, 20, Theme::Instance().ToneColor(IconTone::Secondary)));
    return header;
}

QWidget* FileManagerWindow::BuildContent() {
    auto* content = new QWidget(this);
    content->setObjectName(QStringLiteral("fmContent"));
    auto* layout = new QHBoxLayout(content);
    layout->setContentsMargins(26, 0, 26, 24);
    layout->setSpacing(20);

    content_stack_ = new QStackedWidget(content);

    const auto make_scroll = [&](const QString& object_name) {
        auto* scroll = new QScrollArea(content_stack_);
        scroll->setObjectName(object_name);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroll->viewport()->setAutoFillBackground(false);
        return scroll;
    };

    grid_scroll_ = make_scroll(QStringLiteral("contentScroll"));
    grid_content_ = new QWidget(grid_scroll_);
    grid_content_->setObjectName(QStringLiteral("scrollContent"));
    grid_layout_ = new QGridLayout(grid_content_);
    grid_layout_->setContentsMargins(4, 2, 4, 2);
    grid_layout_->setHorizontalSpacing(kGridSpacing);
    grid_layout_->setVerticalSpacing(kGridSpacing);
    grid_layout_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    grid_scroll_->setWidget(grid_content_);
    content_stack_->addWidget(grid_scroll_);

    list_scroll_ = make_scroll(QStringLiteral("contentScroll"));
    list_content_ = new QWidget(list_scroll_);
    list_content_->setObjectName(QStringLiteral("scrollContent"));
    list_layout_ = new QVBoxLayout(list_content_);
    list_layout_->setContentsMargins(4, 2, 4, 2);
    list_layout_->setSpacing(6);
    list_layout_->setAlignment(Qt::AlignTop);
    list_scroll_->setWidget(list_content_);
    content_stack_->addWidget(list_scroll_);

    empty_page_ = BuildEmptyPage();
    content_stack_->addWidget(empty_page_);

    layout->addWidget(content_stack_, 1);
    details_panel_ = BuildDetailsPanel();
    layout->addWidget(details_panel_, 0);
    return content;
}

QWidget* FileManagerWindow::BuildEmptyPage() {
    auto* page = new QWidget(content_stack_);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(10);

    layout->addStretch(1);
    auto* icon = new QLabel(page);
    icon->setPixmap(IconRenderer::Pixmap(
        Icon::Film, 64, Theme::Instance().ToneColor(IconTone::Muted), 1.25));
    icon->setFixedSize(64, 64);
    icon->setAlignment(Qt::AlignCenter);
    icon_labels_.push_back({icon, Icon::Film, 64, IconTone::Muted});
    layout->addWidget(icon, 0, Qt::AlignHCenter);

    empty_title_ = new QLabel(QStringLiteral("暂无录制文件"), page);
    empty_title_->setObjectName(QStringLiteral("emptyTitle"));
    empty_title_->setAlignment(Qt::AlignCenter);
    layout->addWidget(empty_title_);

    empty_hint_ = new QLabel(
        QStringLiteral("完成一次录制后，视频会显示在这里。"), page);
    empty_hint_->setObjectName(QStringLiteral("emptyHint"));
    empty_hint_->setAlignment(Qt::AlignCenter);
    empty_hint_->setWordWrap(true);
    layout->addWidget(empty_hint_);

    empty_path_ = new ElidedLabel(Qt::ElideMiddle, page);
    empty_path_->setObjectName(QStringLiteral("emptyPath"));
    empty_path_->setAlignment(Qt::AlignCenter);
    empty_path_->setMinimumWidth(240);
    empty_path_->setMinimumHeight(18);
    layout->addWidget(empty_path_);
    layout->addStretch(1);
    return page;
}

QFrame* FileManagerWindow::BuildDetailsPanel() {
    auto* panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("detailsCard"));
    panel->setAttribute(Qt::WA_StyledBackground, true);
    panel->setFixedWidth(kDetailsWidth);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    details_stack_ = new QStackedWidget(panel);
    details_stack_->setObjectName(QStringLiteral("detailsStack"));

    auto* file_page = new QWidget(details_stack_);
    file_page->setObjectName(QStringLiteral("detailsPage"));
    auto* file_layout = new QVBoxLayout(file_page);
    file_layout->setContentsMargins(0, 0, 0, 0);
    file_layout->setSpacing(10);

    details_thumbnail_ = new ThumbnailView(file_page);
    details_thumbnail_->setFixedHeight(178);
    details_thumbnail_->on_click = [this] { PlaySelected(); };
    file_layout->addWidget(details_thumbnail_);

    auto* name_row = new QWidget(file_page);
    auto* name_layout = new QHBoxLayout(name_row);
    name_layout->setContentsMargins(2, 0, 0, 0);
    name_layout->setSpacing(4);
    details_name_ = new ElidedLabel(Qt::ElideMiddle, name_row);
    details_name_->setObjectName(QStringLiteral("detailsName"));
    details_name_->setMinimumWidth(80);
    name_layout->addWidget(details_name_, 1);
    details_edit_ = new QPushButton(name_row);
    details_edit_->setObjectName(QStringLiteral("iconButton"));
    details_edit_->setCursor(Qt::PointingHandCursor);
    details_edit_->setFocusPolicy(Qt::NoFocus);
    details_edit_->setFixedSize(28, 28);
    details_edit_->setIconSize(QSize(16, 16));
    details_edit_->setToolTip(QStringLiteral("重命名"));
    name_layout->addWidget(details_edit_, 0, Qt::AlignVCenter);
    file_layout->addWidget(name_row);

    const auto add_info_row = [&](Icon icon, const QString& label_text,
                                  QLabel** value_out) {
        auto* row = new QWidget(file_page);
        row->setMinimumHeight(26);
        auto* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(2, 0, 2, 0);
        row_layout->setSpacing(8);
        auto* icon_label = new QLabel(row);
        icon_label->setPixmap(IconRenderer::Pixmap(
            icon, 16, Theme::Instance().ToneColor(IconTone::Muted), 1.5));
        icon_label->setFixedSize(16, 16);
        icon_labels_.push_back({icon_label, icon, 16, IconTone::Muted});
        row_layout->addWidget(icon_label, 0, Qt::AlignVCenter);
        auto* label = new QLabel(label_text, row);
        label->setObjectName(QStringLiteral("detailLabel"));
        row_layout->addWidget(label, 0, Qt::AlignVCenter);
        row_layout->addStretch(1);
        auto* value = new QLabel(QStringLiteral("—"), row);
        value->setObjectName(QStringLiteral("detailValue"));
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        row_layout->addWidget(value, 0, Qt::AlignVCenter);
        *value_out = value;
        file_layout->addWidget(row);
    };

    add_info_row(Icon::File, QStringLiteral("大小"), &detail_size_);
    add_info_row(Icon::Clock, QStringLiteral("时长"), &detail_duration_);
    add_info_row(Icon::Monitor, QStringLiteral("分辨率"), &detail_resolution_);
    add_info_row(Icon::Calendar, QStringLiteral("修改时间"), &detail_modified_);
    file_layout->addStretch(1);
    details_stack_->addWidget(file_page);

    auto* empty = new QWidget(details_stack_);
    auto* empty_layout = new QVBoxLayout(empty);
    empty_layout->setContentsMargins(12, 12, 12, 12);
    empty_layout->setSpacing(8);
    empty_layout->addStretch(1);
    auto* empty_icon = new QLabel(empty);
    empty_icon->setPixmap(IconRenderer::Pixmap(
        Icon::Film, 40, Theme::Instance().ToneColor(IconTone::Muted), 1.4));
    empty_icon->setFixedSize(40, 40);
    empty_icon->setAlignment(Qt::AlignCenter);
    icon_labels_.push_back({empty_icon, Icon::Film, 40, IconTone::Muted});
    empty_layout->addWidget(empty_icon, 0, Qt::AlignHCenter);
    auto* placeholder = new QLabel(QStringLiteral("选择一个文件查看详情"), empty);
    placeholder->setObjectName(QStringLiteral("detailsPlaceholder"));
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setWordWrap(true);
    empty_layout->addWidget(placeholder);
    empty_layout->addStretch(1);
    details_stack_->addWidget(empty);

    layout->addWidget(details_stack_, 1);

    auto* buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(6);
    play_button_ = new QPushButton(QStringLiteral("播放"), panel);
    play_button_->setObjectName(QStringLiteral("actionPrimary"));
    folder_button_ = new QPushButton(QStringLiteral("打开文件夹"), panel);
    folder_button_->setObjectName(QStringLiteral("actionOutline"));
    rename_button_ = new QPushButton(QStringLiteral("重命名"), panel);
    rename_button_->setObjectName(QStringLiteral("actionOutline"));
    delete_button_ = new QPushButton(QStringLiteral("删除"), panel);
    delete_button_->setObjectName(QStringLiteral("actionDelete"));
    for (QPushButton* button :
         {play_button_, folder_button_, rename_button_, delete_button_}) {
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        button->setFixedHeight(36);
        button->setIconSize(QSize(14, 14));
        buttons->addWidget(button);
    }
    play_button_->setIcon(IconRenderer::QIconFor(Icon::Play, 14,
                                                 QColor(Qt::white)));
    layout->addLayout(buttons);

    connect(play_button_, &QPushButton::clicked, this,
            &FileManagerWindow::PlaySelected);
    connect(folder_button_, &QPushButton::clicked, this,
            &FileManagerWindow::OpenFolderSelected);
    connect(rename_button_, &QPushButton::clicked, this,
            &FileManagerWindow::RenameSelected);
    connect(delete_button_, &QPushButton::clicked, this,
            &FileManagerWindow::DeleteSelected);
    connect(details_edit_, &QPushButton::clicked, this,
            &FileManagerWindow::RenameSelected);

    UpdateDetailsIcons();
    return panel;
}

void FileManagerWindow::ApplyTheme() {
    setStyleSheet(Theme::Instance().Expand(
        QString::fromUtf8(kFileManagerStyleTemplate)));
    UpdateViewToggleIcons();
    UpdateActionIcons();
    UpdateDetailsIcons();
    if (search_action_) {
        search_action_->setIcon(IconRenderer::QIconFor(
            Icon::Search, 18, Theme::Instance().ToneColor(IconTone::Muted)));
    }
    if (clear_action_) {
        clear_action_->setIcon(IconRenderer::QIconFor(
            Icon::Close, 16, Theme::Instance().ToneColor(IconTone::Muted)));
    }
    if (refresh_button_) {
        refresh_button_->setIcon(IconRenderer::QIconFor(
            Icon::Refresh, 20,
            Theme::Instance().ToneColor(IconTone::Secondary)));
    }
    for (VideoCard* card : cards_) {
        card->update();
    }
    for (VideoRow* row : rows_) {
        row->update();
    }
    if (details_thumbnail_) {
        details_thumbnail_->update();
    }
    ApplyWindowEffects();
    update();
}

void FileManagerWindow::UpdateViewToggleIcons() {
    const ThemeColors& colors = Theme::Instance().colors();
    const auto icon_for = [&colors](Icon icon) {
        QIcon result;
        result.addPixmap(
            IconRenderer::Pixmap(icon, 18, colors.textSecondary, 1.7),
            QIcon::Normal, QIcon::Off);
        result.addPixmap(IconRenderer::Pixmap(icon, 18, colors.primary, 1.9),
                         QIcon::Normal, QIcon::On);
        result.addPixmap(
            IconRenderer::Pixmap(icon, 18, colors.iconDisabled, 1.7),
            QIcon::Disabled, QIcon::Off);
        return result;
    };
    if (grid_button_) {
        grid_button_->setIcon(icon_for(Icon::Grid));
    }
    if (list_button_) {
        list_button_->setIcon(icon_for(Icon::ListView));
    }
}

void FileManagerWindow::UpdateActionIcons() {
    const ThemeColors& colors = Theme::Instance().colors();
    if (play_button_) {
        play_button_->setIcon(
            IconRenderer::QIconFor(Icon::Play, 14, QColor(Qt::white)));
    }
    if (folder_button_) {
        folder_button_->setIcon(
            IconRenderer::QIconFor(Icon::Folder, 14, colors.textSecondary));
    }
    if (rename_button_) {
        rename_button_->setIcon(
            IconRenderer::QIconFor(Icon::Edit, 14, colors.textSecondary));
    }
    if (delete_button_) {
        delete_button_->setIcon(
            IconRenderer::QIconFor(Icon::Trash, 14, colors.textSecondary));
    }
    if (details_edit_) {
        details_edit_->setIcon(
            IconRenderer::QIconFor(Icon::Edit, 16, colors.textSecondary));
    }
}

void FileManagerWindow::UpdateDetailsIcons() {
    const Theme& theme = Theme::Instance();
    for (const ThemedIconLabel& ref : icon_labels_) {
        if (ref.label) {
            ref.label->setPixmap(
                IconRenderer::Pixmap(ref.icon, ref.size,
                                     theme.ToneColor(ref.tone), 1.4));
        }
    }
}

void FileManagerWindow::ApplyWindowEffects() {
#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd) {
        return;
    }
    if (!frame_prepared_) {
        frame_prepared_ = true;
        LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
        style &= ~static_cast<LONG_PTR>(WS_CAPTION);
        style |= WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU;
        SetWindowLongPtr(hwnd, GWL_STYLE, style);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                         SWP_FRAMECHANGED);
    }
    const int preference = maximized_ ? DWMWCP_DONOTROUND : DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference,
                          sizeof(preference));
    const BOOL dark =
        Theme::Instance().effectiveMode() == ThemeMode::Dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark,
                          sizeof(dark));
#endif
}

void FileManagerWindow::ToggleMaximize() {
    if (isMaximized()) {
        showNormal();
    } else {
        showMaximized();
    }
    SyncMaximizedState();
}

void FileManagerWindow::SyncMaximizedState() {
    const bool maximized = isMaximized();
    if (maximized == maximized_) {
        return;
    }
    maximized_ = maximized;
    if (title_bar_) {
        title_bar_->SetMaximized(maximized_);
    }
    ApplyWindowEffects();
}

void FileManagerWindow::SetDirectory(const QString& directory) {
    QString cleaned = directory.trimmed();
    if (cleaned.isEmpty()) {
        const AppSettings settings = LoadAppSettings();
        cleaned = settings.output_directory.isEmpty()
                      ? DefaultOutputDirectory()
                      : settings.output_directory;
    }
    cleaned = QDir::cleanPath(QDir::fromNativeSeparators(cleaned));
    if (cleaned == directory_) {
        return;
    }
    directory_ = cleaned;
    selected_path_.clear();
    Scan();
}

void FileManagerWindow::Refresh() { Scan(); }

void FileManagerWindow::ShowNear(QWidget* anchor) {
    if (anchor) {
        QScreen* screen = anchor->screen();
        if (!screen) {
            screen = QGuiApplication::primaryScreen();
        }
        if (screen) {
            const QRect available = screen->availableGeometry();
            const QRect main = anchor->frameGeometry();
            const int x = qBound(available.left() + 12,
                                 main.center().x() - width() / 2,
                                 available.right() - width() - 12);
            const int y = qBound(available.top() + 12, main.top() + 48,
                                 available.bottom() - height() - 12);
            move(x, y);
        }
    }
    show();
    raise();
    activateWindow();
}

void FileManagerWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    ApplyWindowEffects();
    SyncMaximizedState();
    Refresh();
}

void FileManagerWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (!list_mode_ && grid_scroll_ && !cards_.isEmpty()) {
        LayoutGrid();
    }
}

void FileManagerWindow::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        SyncMaximizedState();
    }
}

bool FileManagerWindow::nativeEvent(const QByteArray& eventType,
                                    void* message, qintptr* result) {
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG") {
        auto* msg = static_cast<MSG*>(message);
        const HWND hwnd = msg->hwnd;
        if (msg->message == WM_NCCALCSIZE && msg->wParam == TRUE) {
            if (IsZoomed(hwnd)) {
                auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam);
                MONITORINFO info{};
                info.cbSize = sizeof(info);
                if (GetMonitorInfo(
                        MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST),
                        &info)) {
                    params->rgrc[0] = info.rcWork;
                }
            }
            *result = 0;
            return true;
        }
        if (msg->message == WM_SYSCOMMAND) {
            const WPARAM command = msg->wParam & 0xFFF0;
            if (command == SC_MAXIMIZE || command == SC_RESTORE) {
                QTimer::singleShot(0, this,
                                   [this] { SyncMaximizedState(); });
            }
        }
        if (msg->message == WM_NCHITTEST && !IsZoomed(hwnd)) {
            RECT window_rect{};
            GetWindowRect(hwnd, &window_rect);
            const int x = GET_X_LPARAM(msg->lParam);
            const int y = GET_Y_LPARAM(msg->lParam);
            const UINT dpi = GetDpiForWindow(hwnd);
            const int border =
                MulDiv(kResizeBorder, dpi > 0 ? static_cast<int>(dpi) : 96, 96);
            const bool left =
                x >= window_rect.left && x < window_rect.left + border;
            const bool right =
                x <= window_rect.right && x > window_rect.right - border;
            const bool top = y >= window_rect.top && y < window_rect.top + border;
            const bool bottom =
                y <= window_rect.bottom && y > window_rect.bottom - border;
            if (left || right || top || bottom) {
                if (top && left) {
                    *result = HTTOPLEFT;
                } else if (top && right) {
                    *result = HTTOPRIGHT;
                } else if (bottom && left) {
                    *result = HTBOTTOMLEFT;
                } else if (bottom && right) {
                    *result = HTBOTTOMRIGHT;
                } else if (left) {
                    *result = HTLEFT;
                } else if (right) {
                    *result = HTRIGHT;
                } else if (top) {
                    *result = HTTOP;
                } else {
                    *result = HTBOTTOM;
                }
                return true;
            }
        }
    }
#endif
    return QWidget::nativeEvent(eventType, message, result);
}

void FileManagerWindow::Scan() {
    entries_.clear();

    QDir directory(directory_);
    if (!directory_.isEmpty() && directory.exists()) {
        directory.setFilter(QDir::Files | QDir::Readable);
        directory.setSorting(QDir::NoSort);
        const QFileInfoList files = directory.entryInfoList();
        for (const QFileInfo& file : files) {
            if (!IsVideoSuffix(file.suffix())) {
                continue;
            }
            FileManagerEntry entry;
            entry.path = QDir::cleanPath(file.absoluteFilePath());
            entry.name = file.fileName();
            entry.size = file.size();
            entry.modified = file.lastModified();
            entry.mtime_ms = entry.modified.isValid()
                                 ? entry.modified.toMSecsSinceEpoch()
                                 : 0;

            const auto cached = cache_.constFind(entry.path);
            if (cached != cache_.constEnd() &&
                cached->mtime_ms == entry.mtime_ms) {
                entry.thumbnail = cached->image;
                entry.info = cached->info;
                entry.failed = cached->failed;
            } else {
                entry.loading = true;
                QueueThumbnail(entry);
            }
            entries_.push_back(entry);
        }
    }

    if (!selected_path_.isEmpty() && !FindEntry(selected_path_)) {
        selected_path_.clear();
    }
    RebuildContent();
}

void FileManagerWindow::QueueThumbnail(const FileManagerEntry& entry) {
    if (entry.path.isEmpty() || pending_.contains(entry.path)) {
        return;
    }
    pending_.insert(entry.path);
    pool_.start(new ThumbnailTask(loader_, entry.path, entry.mtime_ms));
}

void FileManagerWindow::OnThumbnailLoaded(const QString& path, qint64 mtime_ms,
                                          const QImage& image,
                                          const VideoInfo& info, bool ok) {
    pending_.remove(path);
    CacheEntry cached;
    cached.mtime_ms = mtime_ms;
    cached.image = image;
    cached.info = info;
    cached.failed = !ok;
    cache_.insert(path, cached);

    for (FileManagerEntry& entry : entries_) {
        if (entry.path == path && entry.mtime_ms == mtime_ms) {
            entry.thumbnail = image;
            entry.info = info;
            entry.failed = !ok;
            entry.loading = false;
        }
    }
    UpdateEntryWidgets(path);
}

void FileManagerWindow::UpdateEntryWidgets(const QString& path) {
    for (VideoCard* card : cards_) {
        if (card->path() == path) {
            const FileManagerEntry* entry = FindEntry(path);
            if (entry) {
                card->SetEntry(*entry);
                card->SetSelected(path == selected_path_);
            }
        }
    }
    for (VideoRow* row : rows_) {
        if (row->path() == path) {
            const FileManagerEntry* entry = FindEntry(path);
            if (entry) {
                row->SetEntry(*entry);
                row->SetSelected(path == selected_path_);
            }
        }
    }
    if (path == selected_path_) {
        UpdateDetails();
    }
}

const FileManagerEntry* FileManagerWindow::FindEntry(
    const QString& path) const {
    for (const FileManagerEntry& entry : entries_) {
        if (entry.path == path) {
            return &entry;
        }
    }
    return nullptr;
}

QVector<int> FileManagerWindow::VisibleIndices() const {
    QVector<int> indices;
    const QString needle = search_text_.trimmed();
    for (int i = 0; i < entries_.size(); ++i) {
        if (!needle.isEmpty() &&
            !entries_.at(i).name.contains(needle, Qt::CaseInsensitive)) {
            continue;
        }
        indices.push_back(i);
    }
    std::stable_sort(indices.begin(), indices.end(), [this](int a, int b) {
        const FileManagerEntry& left = entries_.at(a);
        const FileManagerEntry& right = entries_.at(b);
        switch (sort_index_) {
            case 1:
                return QString::localeAwareCompare(left.name, right.name) < 0;
            case 2:
                if (left.size != right.size) {
                    return left.size > right.size;
                }
                break;
            case 0:
            default:
                if (left.mtime_ms != right.mtime_ms) {
                    return left.mtime_ms > right.mtime_ms;
                }
                break;
        }
        return QString::localeAwareCompare(left.name, right.name) < 0;
    });
    return indices;
}

int FileManagerWindow::ColumnCountForWidth(int width) const {
    const int columns =
        (qMax(1, width) + kGridSpacing) / (kMinCardWidth + kGridSpacing);
    return qBound(1, columns, 6);
}

void FileManagerWindow::RebuildContent() {
    cards_.clear();
    rows_.clear();
    while (QLayoutItem* item = grid_layout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    while (QLayoutItem* item = list_layout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    const QVector<int> visible = VisibleIndices();
    bool selection_visible = false;
    for (const int index : visible) {
        if (entries_.at(index).path == selected_path_) {
            selection_visible = true;
            break;
        }
    }
    if (!selection_visible) {
        selected_path_ =
            visible.isEmpty() ? QString() : entries_.at(visible.first()).path;
    }

    UpdateCount();

    if (visible.isEmpty()) {
        UpdateEmptyState();
        content_stack_->setCurrentWidget(empty_page_);
        UpdateDetails();
        return;
    }

    if (list_mode_) {
        for (const int index : visible) {
            auto* row = new VideoRow(entries_.at(index), list_content_);
            row->SetSelected(entries_.at(index).path == selected_path_);
            row->clicked = [this](const QString& path) { SelectPath(path); };
            row->activated = [this](const QString& path) {
                SelectPath(path);
                PlayPath(path);
            };
            list_layout_->addWidget(row);
            rows_.push_back(row);
        }
        list_layout_->addStretch(1);
        content_stack_->setCurrentWidget(list_scroll_);
    } else {
        for (const int index : visible) {
            auto* card = new VideoCard(entries_.at(index), grid_content_);
            card->SetSelected(entries_.at(index).path == selected_path_);
            card->clicked = [this](const QString& path) { SelectPath(path); };
            card->activated = [this](const QString& path) {
                SelectPath(path);
                PlayPath(path);
            };
            cards_.push_back(card);
        }
        LayoutGrid();
        content_stack_->setCurrentWidget(grid_scroll_);
    }
    UpdateDetails();
}

void FileManagerWindow::LayoutGrid() {
    if (cards_.isEmpty()) {
        return;
    }
    const int available =
        qMax(kMinCardWidth, grid_scroll_->viewport()->width() -
                                kGridScrollbarReserve);
    const int columns = ColumnCountForWidth(available);
    grid_columns_ = columns;
    const int card_width =
        qMax(140, (available - (columns - 1) * kGridSpacing) / columns);

    while (QLayoutItem* item = grid_layout_->takeAt(0)) {
        delete item;
    }
    for (int i = 0; i < cards_.size(); ++i) {
        cards_.at(i)->SetCardWidth(card_width);
        grid_layout_->addWidget(cards_.at(i), i / columns, i % columns,
                                Qt::AlignTop | Qt::AlignLeft);
    }
    for (int row = 0; row < grid_layout_->rowCount(); ++row) {
        grid_layout_->setRowStretch(row, 0);
    }
    grid_layout_->setRowStretch((cards_.size() + columns - 1) / columns, 1);
}

void FileManagerWindow::UpdateCount() {
    if (count_label_) {
        count_label_->setText(QString::number(entries_.size()));
    }
}

void FileManagerWindow::UpdateEmptyState() {
    const bool no_files = entries_.isEmpty();
    if (no_files) {
        empty_title_->setText(QStringLiteral("暂无录制文件"));
        empty_hint_->setText(QStringLiteral("完成一次录制后，视频会显示在这里。"));
    } else {
        empty_title_->setText(QStringLiteral("未找到匹配的文件"));
        empty_hint_->setText(
            QStringLiteral("没有包含“%1”的视频，试试其他关键词。")
                .arg(search_text_.trimmed()));
    }
    const QString path = directory_.isEmpty()
                             ? QStringLiteral("未设置保存目录")
                             : QDir::toNativeSeparators(directory_);
    empty_path_->SetFullText(path);
}

void FileManagerWindow::SetListMode(bool list) {
    if (list_mode_ == list) {
        if (grid_button_ && list_button_) {
            grid_button_->setChecked(!list);
            list_button_->setChecked(list);
        }
        return;
    }
    list_mode_ = list;
    if (grid_button_ && list_button_) {
        grid_button_->setChecked(!list);
        list_button_->setChecked(list);
    }
    RebuildContent();
}

void FileManagerWindow::SetSortIndex(int index) {
    index = qBound(0, index, 2);
    if (sort_index_ == index) {
        if (sort_combo_ && sort_combo_->currentIndex() != index) {
            const QSignalBlocker blocker(sort_combo_);
            sort_combo_->setCurrentIndex(index);
        }
        return;
    }
    sort_index_ = index;
    if (sort_combo_ && sort_combo_->currentIndex() != index) {
        const QSignalBlocker blocker(sort_combo_);
        sort_combo_->setCurrentIndex(index);
    }
    RebuildContent();
}

void FileManagerWindow::SetSearchText(const QString& text) {
    if (search_text_ == text) {
        return;
    }
    search_text_ = text;
    if (search_edit_ && search_edit_->text() != text) {
        const QSignalBlocker blocker(search_edit_);
        search_edit_->setText(text);
    }
    if (clear_action_) {
        clear_action_->setVisible(!text.isEmpty());
    }
    RebuildContent();
}

void FileManagerWindow::SelectPath(const QString& path) {
    if (selected_path_ == path) {
        UpdateDetails();
        return;
    }
    if (!path.isEmpty() && !FindEntry(path)) {
        return;
    }
    selected_path_ = path;
    for (VideoCard* card : cards_) {
        card->SetSelected(card->path() == path);
    }
    for (VideoRow* row : rows_) {
        row->SetSelected(row->path() == path);
    }
    UpdateDetails();
}

void FileManagerWindow::UpdateDetails() {
    const FileManagerEntry* entry = FindEntry(selected_path_);
    const bool has_entry = entry != nullptr;
    details_stack_->setCurrentIndex(has_entry ? 0 : 1);
    for (QPushButton* button :
         {play_button_, folder_button_, rename_button_, delete_button_,
          details_edit_}) {
        button->setEnabled(has_entry);
    }
    if (!has_entry) {
        details_name_->SetFullText(QString());
        details_thumbnail_->SetState(QImage(), false, 0.0);
        detail_size_->setText(QStringLiteral("—"));
        detail_duration_->setText(QStringLiteral("—"));
        detail_resolution_->setText(QStringLiteral("—"));
        detail_modified_->setText(QStringLiteral("—"));
        return;
    }
    details_name_->SetFullText(entry->name);
    details_thumbnail_->SetState(entry->thumbnail, entry->failed,
                                 entry->info.duration_seconds);
    detail_size_->setText(FormatSize(entry->size));
    detail_duration_->setText(entry->loading && entry->info.duration_seconds <= 0
                                  ? QStringLiteral("—")
                                  : FormatDuration(entry->info.duration_seconds));
    detail_resolution_->setText(FormatResolution(entry->info));
    detail_modified_->setText(FormatModified(entry->modified));
}

void FileManagerWindow::PlayPath(const QString& path) {
    if (path.isEmpty()) {
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        ShowFileError(QStringLiteral("无法播放"),
                      QStringLiteral("系统没有可以打开该视频的播放器。"));
    }
}

QStringList FileManagerWindow::ExplorerSelectArguments(const QString& path) {
    return QStringList{QStringLiteral("/select,"),
                       QDir::toNativeSeparators(path)};
}

void FileManagerWindow::OpenContainingFolder(const QString& path) {
    if (path.isEmpty()) {
        return;
    }
    if (!QProcess::startDetached(QStringLiteral("explorer.exe"),
                                 ExplorerSelectArguments(path))) {
        if (!QDesktopServices::openUrl(
                QUrl::fromLocalFile(QFileInfo(path).absolutePath()))) {
            ShowFileError(QStringLiteral("无法打开文件夹"),
                          QStringLiteral("资源管理器未能启动。"));
        }
    }
}

void FileManagerWindow::ShowFileError(const QString& title,
                                      const QString& detail) {
    QMessageBox::warning(this, title, detail, QMessageBox::Ok);
}

void FileManagerWindow::PlaySelected() {
    if (const FileManagerEntry* entry = FindEntry(selected_path_)) {
        PlayPath(entry->path);
    }
}

void FileManagerWindow::OpenFolderSelected() {
    if (const FileManagerEntry* entry = FindEntry(selected_path_)) {
        OpenContainingFolder(entry->path);
    }
}

bool FileManagerWindow::RenameFile(const QString& path,
                                   const QString& new_base,
                                   QString* new_path, QString* error) {
    const QFileInfo info(path);
    if (!info.exists()) {
        if (error) {
            *error = QStringLiteral("文件已不存在");
        }
        return false;
    }
    const QString suffix = info.suffix();
    const QString target_name =
        suffix.isEmpty() ? new_base : new_base + QLatin1Char('.') + suffix;
    const QString target = QDir::cleanPath(info.dir().filePath(target_name));
    if (target == QDir::cleanPath(path)) {
        if (new_path) {
            *new_path = target;
        }
        return true;
    }
    if (QFileInfo::exists(target)) {
        if (error) {
            *error = QStringLiteral("同名文件已存在");
        }
        return false;
    }
    if (!RenameWithRetry(path, target)) {
        if (error) {
            *error = QStringLiteral("无法重命名文件，文件可能正在使用中");
        }
        return false;
    }
    const auto cached = cache_.find(path);
    if (cached != cache_.end()) {
        cache_.insert(target, cached.value());
        cache_.remove(path);
    }
    pending_.remove(path);
    if (selected_path_ == path) {
        selected_path_ = target;
    }
    if (new_path) {
        *new_path = target;
    }
    return true;
}

bool FileManagerWindow::MoveFileToTrash(const QString& path, QString* error) {
    if (MoveToTrashWithRetry(path)) {
        return true;
    }
    if (error) {
        *error = QStringLiteral("无法将文件移入回收站");
    }
    return false;
}

bool FileManagerWindow::DeleteFilePermanently(const QString& path,
                                              QString* error) {
    if (QFile::remove(path)) {
        return true;
    }
    if (error) {
        *error = QStringLiteral("无法删除文件，文件可能正在使用中");
    }
    return false;
}

void FileManagerWindow::RenameSelected() {
    const FileManagerEntry* entry = FindEntry(selected_path_);
    if (!entry) {
        return;
    }
    const QFileInfo info(entry->path);
    const QString base = info.completeBaseName();
    bool accepted = false;
    const QString input = QInputDialog::getText(
        this, QStringLiteral("重命名"),
        QStringLiteral("新的文件名（扩展名保持不变）："), QLineEdit::Normal,
        base, &accepted);
    if (!accepted) {
        return;
    }
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty()) {
        ShowFileError(QStringLiteral("无法重命名"),
                      QStringLiteral("文件名不能为空。"));
        return;
    }
    if (ContainsInvalidFileNameCharacter(trimmed)) {
        ShowFileError(QStringLiteral("无法重命名"),
                      QStringLiteral("文件名不能包含 < > : \" / \\ | ? *。"));
        return;
    }
    if (trimmed == base) {
        return;
    }
    QString renamed;
    QString error;
    if (!RenameFile(entry->path, trimmed, &renamed, &error)) {
        ShowFileError(QStringLiteral("无法重命名"), error);
        return;
    }
    selected_path_ = renamed;
    Scan();
}

void FileManagerWindow::DeleteSelected() {
    const FileManagerEntry* entry = FindEntry(selected_path_);
    if (!entry) {
        return;
    }
    const QString path = entry->path;
    const QString name = entry->name;
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, QStringLiteral("删除文件"),
        QStringLiteral("确定要删除“%1”吗？\n文件将移入回收站。").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }
    QString error;
    if (!MoveFileToTrash(path, &error)) {
        const QMessageBox::StandardButton permanent = QMessageBox::warning(
            this, QStringLiteral("无法移入回收站"),
            QStringLiteral("%1\n\n是否永久删除该文件？此操作无法撤销。")
                .arg(error),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (permanent != QMessageBox::Yes ||
            !DeleteFilePermanently(path, &error)) {
            if (permanent == QMessageBox::Yes) {
                ShowFileError(QStringLiteral("无法删除"), error);
            }
            return;
        }
    }
    cache_.remove(path);
    pending_.remove(path);
    if (selected_path_ == path) {
        selected_path_.clear();
    }
    Scan();
}

bool FileManagerWindow::AllThumbnailsReady() const {
    if (!pending_.isEmpty()) {
        return false;
    }
    for (const FileManagerEntry& entry : entries_) {
        if (entry.loading) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Verification hook. Mirrors --ui-harness: synchronous pumps, screenshots of
// every meaningful state, a text report and an exit code.

void FileManagerWindow::RunHarness(const QString& directory) {
    const ThemeMode original_theme = Theme::Instance().mode();
    QDir().mkpath(directory);
    const QString empty_directory =
        QDir(directory).filePath(QStringLiteral("empty-folder"));
    QDir().mkpath(empty_directory);
    {
        // A previous interrupted run may have left the temporary copies
        // behind; they must not be scanned or picked as the copy source.
        QDir root(directory);
        const QStringList stale =
            root.entryList(QStringList{QStringLiteral("harness-*")},
                           QDir::Files);
        for (const QString& leftover : stale) {
            root.remove(leftover);
        }

        QDir empty(empty_directory);
        const QStringList leftovers = empty.entryList(
            QStringList{QStringLiteral("*.mp4"), QStringLiteral("*.mov"),
                        QStringLiteral("*.mkv"), QStringLiteral("*.avi"),
                        QStringLiteral("*.flv"), QStringLiteral("*.wmv")},
            QDir::Files);
        for (const QString& leftover : leftovers) {
            QFile::remove(empty.filePath(leftover));
        }
    }

    QStringList report;
    bool passed = true;
    const auto check = [&](const QString& name, bool value,
                           const QString& detail = QString()) {
        if (!value) {
            passed = false;
        }
        report << QStringLiteral("%1=%2%3")
                      .arg(name, value ? QStringLiteral("pass")
                                       : QStringLiteral("fail"),
                           detail.isEmpty()
                               ? QString()
                               : QStringLiteral(" (%1)").arg(detail));
    };
    const auto pump = [](int milliseconds) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < milliseconds) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(5);
        }
    };
    const auto save_shot = [&](const QString& base) {
        pump(160);
        QImage image(size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Theme::Instance().colors().window);
        render(&image);
        const QString png_path =
            QDir(directory).filePath(base + QStringLiteral(".png"));
        if (image.save(png_path)) {
            return png_path;
        }
        // This Qt build ships no PNG image plugin; BMP is built in, so the
        // hook still produces inspectable output.
        const QString bmp_path =
            QDir(directory).filePath(base + QStringLiteral(".bmp"));
        return image.save(bmp_path) ? bmp_path : QString();
    };

    Theme::Instance().SetMode(ThemeMode::Light);
    SetDirectory(directory);
    show();
    raise();
    activateWindow();
    pump(400);

    QElapsedTimer wait;
    wait.start();
    while (!AllThumbnailsReady() && wait.elapsed() < 20000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(10);
    }
    pump(200);

    check(QStringLiteral("thumbnails_ready"), AllThumbnailsReady());
    check(QStringLiteral("files_scanned"), entries_.size() >= 1,
          QString::number(entries_.size()));

    QSet<QString> containers;
    bool all_decoded = true;
    for (const FileManagerEntry& entry : entries_) {
        const QString suffix = QFileInfo(entry.path).suffix().toLower();
        containers.insert(suffix);
        const bool decoded = !entry.thumbnail.isNull();
        all_decoded = all_decoded && decoded;
        report << QStringLiteral(
                      "file name=\"%1\" container=%2 size=%3 info=%4x%5 "
                      "duration=%6 thumbnail=%7")
                      .arg(entry.name, suffix)
                      .arg(entry.size)
                      .arg(entry.info.width)
                      .arg(entry.info.height)
                      .arg(entry.info.duration_seconds, 0, 'f', 2)
                      .arg(decoded ? QStringLiteral("ok")
                                   : QStringLiteral("fail"));
    }
    check(QStringLiteral("thumbnails_all_decoded"), all_decoded);
    report << QStringLiteral("containers=") +
                  QStringList(containers.values()).join(QLatin1Char(','));

    // -- Sorting: modified desc, name asc, size desc.
    SetSortIndex(0);
    {
        const QVector<int> order = VisibleIndices();
        bool sorted = true;
        for (int i = 1; i < order.size(); ++i) {
            const qint64 previous = entries_.at(order.at(i - 1)).mtime_ms;
            const qint64 current = entries_.at(order.at(i)).mtime_ms;
            sorted = sorted && previous >= current;
        }
        check(QStringLiteral("sort_modified_desc"), sorted);
    }
    SetSortIndex(1);
    {
        const QVector<int> order = VisibleIndices();
        bool sorted = true;
        for (int i = 1; i < order.size(); ++i) {
            sorted = sorted &&
                     QString::localeAwareCompare(
                         entries_.at(order.at(i - 1)).name,
                         entries_.at(order.at(i)).name) <= 0;
        }
        check(QStringLiteral("sort_name_asc"), sorted);
    }
    SetSortIndex(2);
    {
        const QVector<int> order = VisibleIndices();
        bool sorted = true;
        for (int i = 1; i < order.size(); ++i) {
            sorted = sorted && entries_.at(order.at(i - 1)).size >=
                                   entries_.at(order.at(i)).size;
        }
        check(QStringLiteral("sort_size_desc"), sorted);
    }
    SetSortIndex(0);

    const QVector<int> all_visible = VisibleIndices();
    const QString first_path =
        all_visible.isEmpty() ? QString()
                              : entries_.at(all_visible.first()).path;
    SelectPath(first_path);
    check(QStringLiteral("selection_details"),
          !first_path.isEmpty() &&
              FindEntry(selected_path_) != nullptr &&
              details_stack_->currentIndex() == 0 &&
              !detail_size_->text().isEmpty());

    const QString grid_light =
        save_shot(QStringLiteral("file-manager-grid-light"));
    check(QStringLiteral("screenshot_grid_light"), !grid_light.isEmpty(),
          grid_light);
    const QString selected_light =
        save_shot(QStringLiteral("file-manager-selected-light"));
    check(QStringLiteral("screenshot_selected_light"),
          !selected_light.isEmpty(), selected_light);

    SetListMode(true);
    pump(150);
    const QString list_light =
        save_shot(QStringLiteral("file-manager-list-light"));
    check(QStringLiteral("screenshot_list_light"), !list_light.isEmpty(),
          list_light);
    check(QStringLiteral("list_mode_rows"), rows_.size() == all_visible.size(),
          QString::number(rows_.size()));
    SetListMode(false);

    // -- Search: the complete base name of the second file matches exactly it.
    QString search_term;
    if (entries_.size() >= 2) {
        search_term = QFileInfo(entries_.at(1).path).completeBaseName();
    }
    SetSearchText(search_term);
    pump(100);
    {
        const QVector<int> filtered = VisibleIndices();
        check(QStringLiteral("search_filters"),
              !search_term.isEmpty() && !filtered.isEmpty() &&
                  filtered.size() < entries_.size(),
              QStringLiteral("%1 of %2")
                  .arg(filtered.size())
                  .arg(entries_.size()));
    }
    const QString search_light =
        save_shot(QStringLiteral("file-manager-search-light"));
    check(QStringLiteral("screenshot_search_light"), !search_light.isEmpty(),
          search_light);
    SetSearchText(QString());

    // -- Empty folder state.
    SetDirectory(empty_directory);
    pump(250);
    check(QStringLiteral("empty_state"), entries_.isEmpty());
    const QString empty_light =
        save_shot(QStringLiteral("file-manager-empty-light"));
    check(QStringLiteral("screenshot_empty_light"), !empty_light.isEmpty(),
          empty_light);

    // -- Dark theme, same set of states.
    Theme::Instance().SetMode(ThemeMode::Dark);
    SetDirectory(directory);
    pump(500);
    SetSortIndex(0);
    SelectPath(first_path);
    const QString grid_dark =
        save_shot(QStringLiteral("file-manager-grid-dark"));
    check(QStringLiteral("screenshot_grid_dark"), !grid_dark.isEmpty(),
          grid_dark);
    const QString selected_dark =
        save_shot(QStringLiteral("file-manager-selected-dark"));
    check(QStringLiteral("screenshot_selected_dark"),
          !selected_dark.isEmpty(), selected_dark);
    SetListMode(true);
    pump(150);
    const QString list_dark =
        save_shot(QStringLiteral("file-manager-list-dark"));
    check(QStringLiteral("screenshot_list_dark"), !list_dark.isEmpty(),
          list_dark);
    SetListMode(false);
    SetSearchText(search_term);
    pump(100);
    const QString search_dark =
        save_shot(QStringLiteral("file-manager-search-dark"));
    check(QStringLiteral("screenshot_search_dark"), !search_dark.isEmpty(),
          search_dark);
    SetSearchText(QString());
    SetDirectory(empty_directory);
    pump(250);
    const QString empty_dark =
        save_shot(QStringLiteral("file-manager-empty-dark"));
    check(QStringLiteral("screenshot_empty_dark"), !empty_dark.isEmpty(),
          empty_dark);

    Theme::Instance().SetMode(original_theme);
    SetDirectory(directory);
    pump(200);

    // -- Rename: exercises the same core the dialog calls, including the
    // on-disk rename and the refreshed card model.
    const FileManagerEntry* rename_source = nullptr;
    for (const FileManagerEntry& entry : entries_) {
        if (!entry.name.startsWith(QStringLiteral("harness-"))) {
            rename_source = &entry;
            break;
        }
    }
    if (rename_source) {
        const QString source = rename_source->path;
        const QString copy = QDir(directory).filePath(
            QStringLiteral("harness-rename.") +
            QFileInfo(source).suffix());
        QFile::remove(copy);
        const bool copied = QFile::copy(source, copy);
        Scan();
        QString renamed;
        QString error;
        const bool renamed_ok =
            copied && RenameFile(copy, QStringLiteral("harness-renamed"),
                                 &renamed, &error);
        check(QStringLiteral("rename_disk"),
              renamed_ok && QFileInfo::exists(renamed) &&
                  !QFileInfo::exists(copy),
              error);
        Scan();
        check(QStringLiteral("rename_refresh"),
              renamed_ok && FindEntry(renamed) != nullptr);
        QString restored;
        if (renamed_ok) {
            RenameFile(renamed, QStringLiteral("harness-rename"), &restored,
                       &error);
        }
        QFile::remove(restored.isEmpty() ? copy : restored);
        QFile::remove(renamed);
        Scan();
    }

    // -- Delete (recycle bin, with the permanent fallback path available).
    const FileManagerEntry* delete_source = nullptr;
    for (const FileManagerEntry& entry : entries_) {
        if (!entry.name.startsWith(QStringLiteral("harness-"))) {
            delete_source = &entry;
            break;
        }
    }
    if (delete_source) {
        const QString source = delete_source->path;
        const QString copy = QDir(directory).filePath(
            QStringLiteral("harness-delete.") +
            QFileInfo(source).suffix());
        QFile::remove(copy);
        const bool copied = QFile::copy(source, copy);
        Scan();
        QString error;
        const bool trashed = copied && MoveFileToTrash(copy, &error);
        if (!trashed && copied) {
            const bool removed = DeleteFilePermanently(copy, &error);
            check(QStringLiteral("delete_fallback_permanent"), removed, error);
        }
        cache_.remove(copy);
        pending_.remove(copy);
        check(QStringLiteral("delete_removes_file"),
              copied && !QFileInfo::exists(copy),
              trashed ? QStringLiteral("recycle bin") : error);
        Scan();
        check(QStringLiteral("delete_refresh"), FindEntry(copy) == nullptr);
    }

    // -- Double-click selects and takes the play path. A URL handler
    // intercepts QDesktopServices::openUrl so no external player is started.
    if (!cards_.isEmpty()) {
        VideoCard* card = cards_.first();
        const QString path = card->path();
        SelectPath(QString());
        FileManagerUrlProbe probe;
        QDesktopServices::setUrlHandler(QStringLiteral("file"), &probe,
                                        "Handle");
        const QPoint local = card->rect().center();
        const QPointF global(card->mapToGlobal(local));
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(local), global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent double_click(QEvent::MouseButtonDblClick, QPointF(local),
                                 global, Qt::LeftButton, Qt::LeftButton,
                                 Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(local), global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(card, &press);
        QApplication::sendEvent(card, &double_click);
        QApplication::sendEvent(card, &release);
        QDesktopServices::unsetUrlHandler(QStringLiteral("file"));
        check(QStringLiteral("double_click_selects_plays"),
              probe.count == 1 && selected_path_ == path &&
                  probe.last_url == QUrl::fromLocalFile(path),
              probe.last_url.toString());
    }

    // -- Refresh picks up a file that appeared after the last scan (the same
    // path FinalizeRecording uses when a save completes).
    if (!entries_.isEmpty()) {
        const QString source = entries_.first().path;
        const int before = entries_.size();
        const QString extra = QDir(directory).filePath(
            QStringLiteral("refresh-probe.") + QFileInfo(source).suffix());
        QFile::remove(extra);
        const bool copied = QFile::copy(source, extra);
        Refresh();
        check(QStringLiteral("refresh_picks_up_new_file"),
              copied && entries_.size() == before + 1,
              QStringLiteral("%1 -> %2").arg(before).arg(entries_.size()));
        wait.start();
        while (!AllThumbnailsReady() && wait.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(10);
        }
        cache_.remove(extra);
        pending_.remove(extra);
        QFile::remove(extra);
        Refresh();
        check(QStringLiteral("refresh_drops_removed_file"),
              entries_.size() == before,
              QString::number(entries_.size()));
    }

    // -- Explorer / player command paths. The window itself is closed by the
    // verification script; the player path is intercepted above.
    if (!entries_.isEmpty()) {
        const QString sample = entries_.first().path;
        const QStringList arguments = ExplorerSelectArguments(sample);
        check(QStringLiteral("explorer_select_arguments"),
              arguments.size() == 2 &&
                  arguments.at(0) == QStringLiteral("/select,") &&
                  arguments.at(1) == QDir::toNativeSeparators(sample),
              arguments.join(QLatin1Char(' ')));
        const bool launched = QProcess::startDetached(
            QStringLiteral("explorer.exe"), arguments);
        check(QStringLiteral("explorer_select_launches"), launched);
        report << QStringLiteral("play_path=QDesktopServices::openUrl(\"%1\")")
                      .arg(QUrl::fromLocalFile(sample).toString());
    }

    SetSearchText(QString());
    SetSortIndex(0);
    SetListMode(false);
    SelectPath(first_path);

    QFile file(QDir(directory).filePath(
        QStringLiteral("file-manager-harness.txt")));
    if (file.open(QIODevice::WriteOnly | QIODevice::Text |
                  QIODevice::Truncate)) {
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out << "result=" << (passed ? "pass" : "fail") << '\n';
        out << "directory=" << QDir::toNativeSeparators(directory) << '\n';
        for (const QString& line : report) {
            out << line << '\n';
        }
        out.flush();
    }

    QTimer::singleShot(0, qApp, [passed] {
        QCoreApplication::exit(passed ? 0 : 1);
    });
}

#include "FileManagerWindow.moc"
