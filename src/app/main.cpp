#include <QApplication>
#include <QCoreApplication>
#include <QStringList>

#include "app/MainWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Hmmob EV"));

    MainWindow window;
    window.show();

    // Automated smoke mode: --smoke <seconds> <output path>. Harmless when
    // absent; documented in the README for scripted verification.
    const QStringList arguments = QCoreApplication::arguments();
    const int smoke_index = arguments.indexOf(QStringLiteral("--smoke"));
    if (smoke_index >= 0 && smoke_index + 2 < arguments.size()) {
        bool ok = false;
        const int seconds = arguments.at(smoke_index + 1).toInt(&ok);
        if (ok && seconds > 0) {
            window.StartSmoke(seconds, arguments.at(smoke_index + 2));
        }
    }

    return app.exec();
}
