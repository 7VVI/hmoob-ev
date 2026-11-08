#include <QApplication>
#include <QCoreApplication>
#include <QStringList>

#include <cstdio>

#include "app/MainWindow.h"
#include "app/UiFont.h"

namespace {

void PrintSmokeUsage() {
    std::fprintf(stderr,
                 "usage: hmmob_ev_app [--smoke <seconds> <output path>]\n"
                 "  --smoke records the default display for <seconds> and "
                 "writes <output path>.smoke.txt\n");
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Hmmob EV"));

    app.setFont(UiFont(14));

    // Automated smoke mode: --smoke <seconds> <output path>. Malformed values
    // are a usage error (exit 2), not a reason to silently start the UI.
    const QStringList arguments = QCoreApplication::arguments();
    const int smoke_index = arguments.indexOf(QStringLiteral("--smoke"));
    int smoke_seconds = 0;
    QString smoke_path;
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
    }

    MainWindow window;
    window.show();
    if (smoke_index >= 0) {
        window.StartSmoke(smoke_seconds, smoke_path);
    }

    return app.exec();
}
