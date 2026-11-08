#include "app/WindowPickerDialog.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <windows.h>

#include <QFont>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>

#include <string>
#include <vector>

namespace {

bool IsCloaked(HWND window) {
    BOOL cloaked = FALSE;
    const HRESULT hr =
        DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    return SUCCEEDED(hr) && cloaked != FALSE;
}

BOOL CALLBACK CollectWindowProc(HWND window, LPARAM param) {
    auto& windows = *reinterpret_cast<std::vector<HWND>*>(param);

    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == GetCurrentProcessId()) {
        return TRUE;
    }
    if (!IsWindowVisible(window)) {
        return TRUE;
    }
    if ((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0) {
        return TRUE;
    }
    if (IsCloaked(window)) {
        return TRUE;
    }
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) {
        return TRUE;
    }
    std::wstring title(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(window, title.data(), length + 1);
    if (copied <= 0) {
        return TRUE;
    }
    title.resize(static_cast<size_t>(copied));
    if (title.find_first_not_of(L" \t\r\n") == std::wstring::npos) {
        return TRUE;
    }
    RECT bounds{};
    if (!GetWindowRect(window, &bounds) || bounds.right <= bounds.left ||
        bounds.bottom <= bounds.top) {
        return TRUE;
    }
    windows.push_back(window);
    return TRUE;
}

std::wstring WindowTitle(HWND window) {
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) {
        return std::wstring();
    }
    std::wstring title(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(window, title.data(), length + 1);
    if (copied <= 0) {
        return std::wstring();
    }
    title.resize(static_cast<size_t>(copied));
    return title;
}

std::wstring WindowClass(HWND window) {
    wchar_t name[256] = {};
    const int length = GetClassNameW(window, name, 255);
    if (length <= 0) {
        return std::wstring();
    }
    return std::wstring(name, static_cast<size_t>(length));
}

// Small icon for the executable behind `window`, used when the window and its
// class do not expose one. The caller owns the returned icon.
HICON ShellIconForProcess(HWND window) {
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == 0) {
        return nullptr;
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return nullptr;
    }
    wchar_t path[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    const BOOL ok = QueryFullProcessImageNameW(process, 0, path, &size);
    CloseHandle(process);
    if (!ok || size == 0) {
        return nullptr;
    }
    SHFILEINFOW info{};
    if (SHGetFileInfoW(path, 0, &info, sizeof(info),
                       SHGFI_ICON | SHGFI_SMALLICON) == 0) {
        return nullptr;
    }
    return info.hIcon;
}

QPixmap WindowIcon(HWND window) {
    HICON icon = reinterpret_cast<HICON>(
        SendMessageW(window, WM_GETICON, ICON_SMALL2, 0));
    bool owned = false;
    if (!icon) {
        icon = reinterpret_cast<HICON>(
            SendMessageW(window, WM_GETICON, ICON_SMALL, 0));
    }
    if (!icon) {
        icon = reinterpret_cast<HICON>(
            SendMessageW(window, WM_GETICON, ICON_BIG, 0));
    }
    if (!icon) {
        icon = reinterpret_cast<HICON>(GetClassLongPtrW(window, GCLP_HICONSM));
    }
    if (!icon) {
        icon = reinterpret_cast<HICON>(GetClassLongPtrW(window, GCLP_HICON));
    }
    if (!icon) {
        icon = ShellIconForProcess(window);
        owned = icon != nullptr;
    }
    QPixmap pixmap;
    if (icon) {
        const QImage image = QImage::fromHICON(icon);
        if (!image.isNull()) {
            pixmap = QPixmap::fromImage(image);
        }
        if (owned) {
            DestroyIcon(icon);
        }
    }
    return pixmap;
}

QString ElideSingleLine(const QString& text) {
    QString result = text;
    result.replace(QLatin1Char('\r'), QLatin1Char(' '));
    result.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return result;
}

}  // namespace

std::vector<HWND> WindowPickerDialog::EnumerateCapturableWindows() {
    std::vector<HWND> windows;
    EnumWindows(&CollectWindowProc, reinterpret_cast<LPARAM>(&windows));
    return windows;
}

WindowPickerDialog::WindowPickerDialog(QWidget* parent,
                                       const std::vector<HWND>& windows)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("选择窗口"));
    setModal(true);
    setMinimumSize(440, 340);
    resize(580, 480);

    setStyleSheet(QStringLiteral(
        "QDialog { background: #FFFFFF; }"
        "QLabel#dialogTitle { font-size: 17px; font-weight: 600; "
        "color: #1D2129; }"
        "QLabel#dialogHint { font-size: 13px; color: #86909C; }"
        "QListWidget { background: #FFFFFF; border: 1px solid #E5E6EB; "
        "border-radius: 10px; padding: 4px; outline: 0; font-size: 14px; }"
        "QListWidget::item { padding: 6px; border-radius: 8px; "
        "color: #1D2129; }"
        "QListWidget::item:hover { background: #F4F7FD; }"
        "QListWidget::item:selected { background: #EAF2FE; color: #2F6BFF; }"
        "QPushButton { min-height: 36px; padding: 0 22px; border-radius: 8px; "
        "font-size: 14px; }"
        "QPushButton#secondary { background: #FFFFFF; border: 1px solid "
        "#E5E6EB; color: #4E5969; }"
        "QPushButton#secondary:hover { background: #F7F8FA; }"
        "QPushButton#primary { background: #2F6BFF; border: 1px solid #2F6BFF; "
        "color: #FFFFFF; }"
        "QPushButton#primary:hover { background: #245BF0; }"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("选择要录制的窗口"), this);
    title->setObjectName(QStringLiteral("dialogTitle"));
    layout->addWidget(title);

    auto* hint = new QLabel(
        QStringLiteral("双击窗口或选中后点击“确定”。仅显示可见的顶层窗口。"),
        this);
    hint->setObjectName(QStringLiteral("dialogHint"));
    layout->addWidget(hint);

    list_ = new QListWidget(this);
    list_->setIconSize(QSize(32, 32));
    list_->setSpacing(2);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    for (HWND window : windows) {
        const std::wstring title_text = WindowTitle(window);
        const std::wstring class_text = WindowClass(window);
        RECT bounds{};
        GetWindowRect(window, &bounds);
        const int width = bounds.right - bounds.left;
        const int height = bounds.bottom - bounds.top;
        const QString text =
            ElideSingleLine(QString::fromStdWString(title_text)) +
            QStringLiteral("\n") +
            ElideSingleLine(QString::fromStdWString(class_text)) +
            QStringLiteral(" · %1 × %2").arg(width).arg(height);
        auto* item = new QListWidgetItem(WindowIcon(window), text, list_);
        item->setData(Qt::UserRole,
                      QVariant::fromValue<quintptr>(
                          reinterpret_cast<quintptr>(window)));
        item->setSizeHint(QSize(0, 58));
    }
    layout->addWidget(list_, 1);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    buttons->addStretch(1);
    auto* cancel = new QPushButton(QStringLiteral("取消"), this);
    cancel->setObjectName(QStringLiteral("secondary"));
    cancel->setCursor(Qt::PointingHandCursor);
    auto* ok = new QPushButton(QStringLiteral("确定"), this);
    ok->setObjectName(QStringLiteral("primary"));
    ok->setCursor(Qt::PointingHandCursor);
    ok->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(ok);
    layout->addLayout(buttons);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(ok, &QPushButton::clicked, this, &WindowPickerDialog::accept);
    connect(list_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { accept(); });

    if (list_->count() > 0) {
        list_->setCurrentRow(0);
    }
}

void WindowPickerDialog::accept() {
    if (!SelectedWindow()) {
        return;
    }
    QDialog::accept();
}

HWND WindowPickerDialog::SelectedWindow() const {
    const QListWidgetItem* item = list_ ? list_->currentItem() : nullptr;
    if (!item) {
        return nullptr;
    }
    return reinterpret_cast<HWND>(
        item->data(Qt::UserRole).value<quintptr>());
}

bool WindowPickerDialog::Pick(QWidget* parent, HWND& out) {
    const std::vector<HWND> windows = EnumerateCapturableWindows();
    if (windows.empty()) {
        QMessageBox::information(parent, QStringLiteral("选择窗口"),
                                 QStringLiteral("未找到可录制的窗口"));
        return false;
    }
    WindowPickerDialog dialog(parent, windows);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    const HWND selected = dialog.SelectedWindow();
    if (!selected) {
        return false;
    }
    out = selected;
    return true;
}
