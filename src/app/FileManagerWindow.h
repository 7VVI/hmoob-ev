#pragma once

#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QSet>
#include <QString>
#include <QThreadPool>
#include <QVector>
#include <QWidget>
#include <functional>
#include <memory>
#include <vector>

#include "app/IconRenderer.h"
#include "app/Theme.h"
#include "app/VideoThumbnail.h"

class QAction;
class QComboBox;
class QFrame;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;
class ElidedLabel;
class TitleBar;
class VideoCard;
class VideoRow;
class ThumbnailView;

// One scanned file. The thumbnail and stream metadata are filled in
// asynchronously and cached by path + modification time.
struct FileManagerEntry {
    QString path;
    QString name;
    qint64 size = 0;
    qint64 mtime_ms = 0;
    QDateTime modified;
    VideoInfo info;
    QImage thumbnail;
    bool failed = false;
    bool loading = false;
};

// QObject used as the thread-safe hand-off for background thumbnail jobs.
class ThumbnailLoader : public QObject {
    Q_OBJECT

public:
    explicit ThumbnailLoader(QObject* parent = nullptr);

signals:
    void Loaded(const QString& path, qint64 mtime_ms, const QImage& image,
                const VideoInfo& info, bool ok);
};

// Frameless, non-modal video library. Scans the recorder's output folder and
// shows the recordings as a card grid or a compact list, with a details panel.
class FileManagerWindow : public QWidget {
    Q_OBJECT

public:
    explicit FileManagerWindow(QWidget* parent = nullptr);
    ~FileManagerWindow() override;

    // Scans `directory` (empty = the configured/default output folder). Does
    // nothing when the folder is already current.
    void SetDirectory(const QString& directory);
    [[nodiscard]] QString directory() const { return directory_; }

    // Rescans the current folder.
    void Refresh();

    // Shows the window on top of `anchor`, centred over it and clamped to the
    // anchor's screen.
    void ShowNear(QWidget* anchor);

    // Verification hook: exercises scanning, sorting, search, both view
    // modes, rename/delete and both themes, writes screenshots plus
    // "file-manager-harness.txt" to `directory`, then exits 0/1.
    void RunHarness(const QString& directory);

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message,
                     qintptr* result) override;

private:
    struct CacheEntry {
        qint64 mtime_ms = 0;
        QImage image;
        VideoInfo info;
        bool failed = false;
    };

    void BuildUi();
    QWidget* BuildHeader();
    QWidget* BuildContent();
    QFrame* BuildDetailsPanel();
    QWidget* BuildEmptyPage();
    void ApplyTheme();
    void UpdateViewToggleIcons();
    void UpdateActionIcons();
    void UpdateDetailsIcons();
    void ApplyWindowEffects();
    void ToggleMaximize();
    void SyncMaximizedState();

    void Scan();
    void RebuildContent();
    void LayoutGrid();
    void SetListMode(bool list);
    void SetSortIndex(int index);
    void SetSearchText(const QString& text);
    void SelectPath(const QString& path);
    void UpdateDetails();
    void UpdateCount();
    void UpdateEmptyState();

    [[nodiscard]] const FileManagerEntry* FindEntry(
        const QString& path) const;
    [[nodiscard]] QVector<int> VisibleIndices() const;
    [[nodiscard]] int ColumnCountForWidth(int width) const;
    [[nodiscard]] bool AllThumbnailsReady() const;
    void QueueThumbnail(const FileManagerEntry& entry);
    void OnThumbnailLoaded(const QString& path, qint64 mtime_ms,
                           const QImage& image, const VideoInfo& info,
                           bool ok);
    void UpdateEntryWidgets(const QString& path);

    void PlayPath(const QString& path);
    void OpenContainingFolder(const QString& path);
    void PlaySelected();
    void OpenFolderSelected();
    void RenameSelected();
    void DeleteSelected();
    void ShowFileError(const QString& title, const QString& detail);
    // Core operations shared with the harness hooks.
    bool RenameFile(const QString& path, const QString& new_base,
                    QString* new_path, QString* error);
    bool DeleteFilePermanently(const QString& path, QString* error);
    bool MoveFileToTrash(const QString& path, QString* error);
    [[nodiscard]] static QStringList ExplorerSelectArguments(
        const QString& path);

    QString directory_;
    QVector<FileManagerEntry> entries_;
    QHash<QString, CacheEntry> cache_;
    QSet<QString> pending_;
    QString selected_path_;
    QString search_text_;
    int sort_index_ = 0;  // 0 modified desc, 1 name asc, 2 size desc
    bool list_mode_ = false;
    int grid_columns_ = 0;

    TitleBar* title_bar_ = nullptr;
    QLabel* count_label_ = nullptr;
    QLineEdit* search_edit_ = nullptr;
    QAction* search_action_ = nullptr;
    QAction* clear_action_ = nullptr;
    QComboBox* sort_combo_ = nullptr;
    QPushButton* grid_button_ = nullptr;
    QPushButton* list_button_ = nullptr;
    QPushButton* refresh_button_ = nullptr;

    QStackedWidget* content_stack_ = nullptr;
    QScrollArea* grid_scroll_ = nullptr;
    QWidget* grid_content_ = nullptr;
    QGridLayout* grid_layout_ = nullptr;
    QScrollArea* list_scroll_ = nullptr;
    QWidget* list_content_ = nullptr;
    QVBoxLayout* list_layout_ = nullptr;
    QWidget* empty_page_ = nullptr;
    QLabel* empty_title_ = nullptr;
    QLabel* empty_hint_ = nullptr;
    ElidedLabel* empty_path_ = nullptr;
    QVector<VideoCard*> cards_;
    QVector<VideoRow*> rows_;

    QFrame* details_panel_ = nullptr;
    QStackedWidget* details_stack_ = nullptr;
    ThumbnailView* details_thumbnail_ = nullptr;
    ElidedLabel* details_name_ = nullptr;
    QPushButton* details_edit_ = nullptr;
    QLabel* detail_size_ = nullptr;
    QLabel* detail_duration_ = nullptr;
    QLabel* detail_resolution_ = nullptr;
    QLabel* detail_modified_ = nullptr;
    QPushButton* play_button_ = nullptr;
    QPushButton* folder_button_ = nullptr;
    QPushButton* rename_button_ = nullptr;
    QPushButton* delete_button_ = nullptr;

    struct ThemedIconLabel {
        QLabel* label = nullptr;
        Icon icon = Icon::File;
        int size = 16;
        IconTone tone = IconTone::Muted;
    };
    std::vector<ThemedIconLabel> icon_labels_;

    std::shared_ptr<ThumbnailLoader> loader_;
    QThreadPool pool_;
    bool maximized_ = false;
    bool frame_prepared_ = false;
};
