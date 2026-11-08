#pragma once

#include <windows.h>

#include <QDialog>
#include <vector>

class QListWidget;

// Modal picker for one visible top-level window. Shared with the smoke hook so
// automated runs select from the same list the dialog shows.
class WindowPickerDialog : public QDialog {
    Q_OBJECT

public:
    // Visible, uncloaked top-level windows with a non-empty title, excluding
    // tool windows and windows owned by this process. Z-order is preserved.
    static std::vector<HWND> EnumerateCapturableWindows();

    // Shows the picker; returns true with `out` set when the user confirms.
    static bool Pick(QWidget* parent, HWND& out);

protected:
    void accept() override;

private:
    WindowPickerDialog(QWidget* parent, const std::vector<HWND>& windows);

    HWND SelectedWindow() const;

    QListWidget* list_ = nullptr;
};
