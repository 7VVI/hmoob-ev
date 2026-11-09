#include <QApplication>
#include <QCoreApplication>
#include <QRect>
#include <QStringList>

#include <cstdio>

#include "app/Logo.h"
#include "app/MainWindow.h"
#include "app/Theme.h"
#include "app/UiFont.h"

namespace {

void PrintSmokeUsage() {
    std::fprintf(
        stderr,
        "usage: hmmob_ev_app [--smoke <seconds> <output path> [options]]\n"
        "  --smoke-mode screen|region|window|camera  capture mode "
        "(default: screen)\n"
        "  --smoke-region <x,y,w,h>  display-local region for region mode\n"
        "  --smoke-window <n>        n-th capturable window (1-based)\n"
        "  --smoke-camera <n>        camera device index (default: 0)\n"
        "  --settings-screenshot <dir>  render every settings page to image "
        "files and exit\n"
        "  --ui-harness <dir>        exercise countdown/文件夹/滚轮 and write "
        "screenshots, then exit\n");
}

bool ParseInt(const QString& text, int& out) {
    bool ok = false;
    const int value = text.toInt(&ok);
    if (!ok) {
        return false;
    }
    out = value;
    return true;
}

bool ParseRegion(const QString& text, QRect& out) {
    const QStringList parts = text.split(QLatin1Char(','));
    if (parts.size() != 4) {
        return false;
    }
    int values[4] = {};
    for (int i = 0; i < 4; ++i) {
        if (!ParseInt(parts.at(i), values[i])) {
            return false;
        }
    }
    if (values[0] < 0 || values[1] < 0 || values[2] <= 0 ||
        values[3] <= 0) {
        return false;
    }
    out = QRect(values[0], values[1], values[2], values[3]);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Hmmob"));
    QApplication::setApplicationName(QStringLiteral("Hmmob EV"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setWindowIcon(Logo::Icon());

    app.setFont(UiFont(14));

    // Reads the saved mode, sets the application palette and starts following
    // the OS when the user chose 跟随系统.
    Theme::Instance();

    // Automated smoke mode:
    //   --smoke <seconds> <output path> [--smoke-mode ...] ...
    // Malformed values are a usage error (exit 2), not a reason to silently
    // start the UI.
    const QStringList arguments = QCoreApplication::arguments();
    const int smoke_index = arguments.indexOf(QStringLiteral("--smoke"));
    int smoke_seconds = 0;
    QString smoke_path;
    SmokeOptions smoke_options;
    if (smoke_index >= 0) {
        bool ok = false;
        if (smoke_index + 2 >= arguments.size()) {
            PrintSmokeUsage();
            return 2;
        }
        smoke_seconds = arguments.at(smoke_index + 1).toInt(&ok);
        smoke_path = arguments.at(smoke_index + 2);
        if (!ok || smoke_seconds <= 0 || smoke_path.isEmpty()) {
            PrintSmokeUsage();
            return 2;
        }

        bool has_region = false;
        bool has_window = false;
        for (int i = smoke_index + 3; i < arguments.size(); ++i) {
            const QString arg = arguments.at(i);
            const auto next_value = [&](const char* name,
                                        QString& value) -> bool {
                if (i + 1 >= arguments.size()) {
                    std::fprintf(stderr, "missing value for %s\n", name);
                    return false;
                }
                value = arguments.at(++i);
                return true;
            };

            if (arg == QStringLiteral("--smoke-mode")) {
                QString value;
                if (!next_value("--smoke-mode", value)) {
                    PrintSmokeUsage();
                    return 2;
                }
                if (value != QStringLiteral("screen") &&
                    value != QStringLiteral("region") &&
                    value != QStringLiteral("window") &&
                    value != QStringLiteral("camera")) {
                    std::fprintf(stderr,
                                 "--smoke-mode must be screen, region, window "
                                 "or camera\n");
                    PrintSmokeUsage();
                    return 2;
                }
                smoke_options.mode = value;
            } else if (arg == QStringLiteral("--smoke-region")) {
                QString value;
                QRect region;
                if (!next_value("--smoke-region", value) ||
                    !ParseRegion(value, region)) {
                    std::fprintf(
                        stderr,
                        "--smoke-region needs x,y,w,h with non-negative x,y "
                        "and positive w,h\n");
                    PrintSmokeUsage();
                    return 2;
                }
                smoke_options.region = region;
                has_region = true;
            } else if (arg == QStringLiteral("--smoke-window")) {
                QString value;
                int index = 0;
                if (!next_value("--smoke-window", value) ||
                    !ParseInt(value, index) || index < 1) {
                    std::fprintf(stderr,
                                 "--smoke-window needs a 1-based index\n");
                    PrintSmokeUsage();
                    return 2;
                }
                smoke_options.window = index;
                has_window = true;
            } else if (arg == QStringLiteral("--smoke-camera")) {
                QString value;
                int index = 0;
                if (!next_value("--smoke-camera", value) ||
                    !ParseInt(value, index) || index < 0) {
                    std::fprintf(stderr,
                                 "--smoke-camera needs a non-negative index\n");
                    PrintSmokeUsage();
                    return 2;
                }
                smoke_options.camera = index;
            } else {
                std::fprintf(stderr, "unknown option: %s\n",
                             arg.toLocal8Bit().constData());
                PrintSmokeUsage();
                return 2;
            }
        }

        // Region and window smoke runs are explicitly scripted; require the
        // coordinates/index instead of silently recording the whole desktop.
        if (smoke_options.mode == QStringLiteral("region") && !has_region) {
            std::fprintf(stderr, "--smoke-mode region needs --smoke-region\n");
            PrintSmokeUsage();
            return 2;
        }
        if (smoke_options.mode == QStringLiteral("window") && !has_window) {
            std::fprintf(stderr, "--smoke-mode window needs --smoke-window\n");
            PrintSmokeUsage();
            return 2;
        }
    }

    // Verification hook: render all settings pages without a real event
    // session. Treated as automation so the tray and auto-start are inert.
    const int screenshot_index =
        arguments.indexOf(QStringLiteral("--settings-screenshot"));
    QString screenshot_directory;
    if (screenshot_index >= 0) {
        if (screenshot_index + 1 >= arguments.size()) {
            PrintSmokeUsage();
            return 2;
        }
        screenshot_directory = arguments.at(screenshot_index + 1);
        if (screenshot_directory.isEmpty()) {
            PrintSmokeUsage();
            return 2;
        }
    }

    // Verification hook: drives the countdown overlay, folder-only output and
    // wheel handling, then exits 0/1. Treated as automation like the other
    // hooks, so it never touches the tray or auto-start.
    const int harness_index =
        arguments.indexOf(QStringLiteral("--ui-harness"));
    QString harness_directory;
    if (harness_index >= 0) {
        if (harness_index + 1 >= arguments.size()) {
            PrintSmokeUsage();
            return 2;
        }
        harness_directory = arguments.at(harness_index + 1);
        if (harness_directory.isEmpty()) {
            PrintSmokeUsage();
            return 2;
        }
    }

    const bool automation =
        smoke_index >= 0 || screenshot_index >= 0 || harness_index >= 0;
    MainWindow window(automation);
    if (harness_index >= 0) {
        window.show();
        window.RunUiHarness(harness_directory);
        return app.exec();
    }
    if (screenshot_index >= 0) {
        window.show();
        window.CaptureSettingsScreenshots(screenshot_directory);
        return app.exec();
    }

    // 启动后最小化到托盘: keep the window hidden; the tray icon exists and
    // the idle preview starts only when the window is first shown.
    if (!window.StartHiddenToTray()) {
        window.show();
    }
    if (smoke_index >= 0) {
        window.StartSmoke(smoke_seconds, smoke_path, smoke_options);
    }

    return app.exec();
}
