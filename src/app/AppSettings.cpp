#include "app/AppSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

namespace {

constexpr auto kOrganization = "Hmmob";
constexpr auto kApplication = "Hmmob EV";

bool IsKnownContainer(const QString& extension) {
    static const QStringList kContainers = {
        QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("mkv"),
        QStringLiteral("avi"), QStringLiteral("flv"), QStringLiteral("wmv")};
    return kContainers.contains(extension);
}

}  // namespace

AppSettings LoadAppSettings() {
    AppSettings settings;
    QSettings store(QString::fromLatin1(kOrganization),
                    QString::fromLatin1(kApplication));

    const QString container = store.value(QStringLiteral("recording/container"))
                                  .toString()
                                  .toLower();
    if (IsKnownContainer(container)) {
        settings.container_extension = container;
    }

    bool ok = false;
    const qlonglong bitrate =
        store.value(QStringLiteral("recording/bitrate")).toLongLong(&ok);
    if (ok && bitrate > 0) {
        settings.video_bitrate = bitrate;
    }
    settings.hdr = store.value(QStringLiteral("recording/hdr"), settings.hdr)
                       .toBool();

    const int camera = store.value(QStringLiteral("recording/camera"), 0).toInt();
    settings.camera_index = camera >= 0 ? camera : 0;

    const int codec =
        store.value(QStringLiteral("recording/audio_codec"), 0).toInt();
    settings.audio_codec = codec == static_cast<int>(hmmob::AudioCodec::PcmS16Le)
                               ? hmmob::AudioCodec::PcmS16Le
                               : hmmob::AudioCodec::Aac;

    const int sample_rate =
        store.value(QStringLiteral("recording/audio_sample_rate"), 0).toInt();
    settings.audio_sample_rate =
        sample_rate == 44100 || sample_rate == 48000 ? sample_rate : 48000;

    const int audio_bitrate =
        store.value(QStringLiteral("recording/audio_bitrate"), 0).toInt();
    if (audio_bitrate == 96000 || audio_bitrate == 128000 ||
        audio_bitrate == 192000 || audio_bitrate == 320000) {
        settings.audio_bitrate = audio_bitrate;
    }

    const int countdown =
        store.value(QStringLiteral("recording/countdown"), -1).toInt();
    if (countdown == 0 || countdown == 3 || countdown == 5 || countdown == 10) {
        settings.countdown_seconds = countdown;
    }

    const QString video_codec =
        store.value(QStringLiteral("recording/video_codec")).toString();
    settings.video_codec = video_codec == QStringLiteral("h264")
                               ? QStringLiteral("h264")
                               : QStringLiteral("hevc");

    const int fps = store.value(QStringLiteral("recording/fps"), 0).toInt();
    settings.fps = fps == 30 || fps == 60 || fps == 120 ? fps : 60;

    const int mode = store.value(QStringLiteral("recording/mode"), 0).toInt();
    settings.capture_mode = mode >= 0 && mode <= 3 ? mode : 0;

    settings.mic_device_id =
        store.value(QStringLiteral("audio/mic_device")).toString();
    settings.system_device_id =
        store.value(QStringLiteral("audio/system_device")).toString();
    settings.microphone_enabled =
        store.value(QStringLiteral("audio/microphone_enabled"), true).toBool();
    settings.system_audio_enabled =
        store.value(QStringLiteral("audio/system_enabled"), true).toBool();

    // audio/mode is authoritative. Older builds only stored the two booleans,
    // so derive the mode from them when the key is absent.
    const int audio_mode = store.value(QStringLiteral("audio/mode"), -1).toInt();
    if (audio_mode >= 0 &&
        audio_mode <= static_cast<int>(AudioSourceMode::None)) {
        settings.audio_mode = static_cast<AudioSourceMode>(audio_mode);
    } else {
        settings.audio_mode = AudioModeFromFlags(settings.microphone_enabled,
                                                 settings.system_audio_enabled);
    }
    settings.microphone_enabled = AudioModeHasMicrophone(settings.audio_mode);
    settings.system_audio_enabled = AudioModeHasSystemAudio(settings.audio_mode);

    settings.output_directory =
        store.value(QStringLiteral("output/directory")).toString();

    const QString language =
        store.value(QStringLiteral("appearance/language")).toString();
    if (!language.isEmpty()) {
        settings.language = language;
    }

    settings.auto_start =
        store.value(QStringLiteral("general/auto_start"), false).toBool();
    settings.minimize_to_tray_on_start =
        store.value(QStringLiteral("general/minimize_to_tray_on_start"), false)
            .toBool();
    settings.close_to_tray =
        store.value(QStringLiteral("general/close_to_tray"), false).toBool();
    settings.hide_notifications_while_recording =
        store.value(QStringLiteral("general/hide_notifications"), false)
            .toBool();
    settings.show_recording_complete_notification =
        store.value(QStringLiteral("general/show_recording_complete"), true)
            .toBool();

    // An explicitly stored empty string means "disabled"; a missing key keeps
    // the default.
    if (store.contains(QStringLiteral("shortcuts/start_stop"))) {
        settings.shortcut_start_stop =
            store.value(QStringLiteral("shortcuts/start_stop")).toString();
    }
    if (store.contains(QStringLiteral("shortcuts/show_hide"))) {
        settings.shortcut_show_hide =
            store.value(QStringLiteral("shortcuts/show_hide")).toString();
    }
    if (store.contains(QStringLiteral("shortcuts/open_folder"))) {
        settings.shortcut_open_folder =
            store.value(QStringLiteral("shortcuts/open_folder")).toString();
    }

    return settings;
}

void SaveAppSettings(const AppSettings& settings) {
    QSettings store(QString::fromLatin1(kOrganization),
                    QString::fromLatin1(kApplication));
    store.setValue(QStringLiteral("recording/container"),
                   settings.container_extension);
    store.setValue(QStringLiteral("recording/bitrate"),
                   QVariant::fromValue<qlonglong>(settings.video_bitrate));
    store.setValue(QStringLiteral("recording/hdr"), settings.hdr);
    store.setValue(QStringLiteral("recording/camera"), settings.camera_index);
    store.setValue(QStringLiteral("recording/audio_codec"),
                   static_cast<int>(settings.audio_codec));
    store.setValue(QStringLiteral("recording/audio_sample_rate"),
                   settings.audio_sample_rate);
    store.setValue(QStringLiteral("recording/audio_bitrate"),
                   settings.audio_bitrate);
    store.setValue(QStringLiteral("recording/countdown"),
                   settings.countdown_seconds);
    store.setValue(QStringLiteral("recording/video_codec"),
                   settings.video_codec);
    store.setValue(QStringLiteral("recording/fps"), settings.fps);
    store.setValue(QStringLiteral("recording/mode"), settings.capture_mode);
    store.setValue(QStringLiteral("audio/mic_device"),
                   settings.mic_device_id);
    store.setValue(QStringLiteral("audio/system_device"),
                   settings.system_device_id);
    store.setValue(QStringLiteral("audio/microphone_enabled"),
                   settings.microphone_enabled);
    store.setValue(QStringLiteral("audio/system_enabled"),
                   settings.system_audio_enabled);
    store.setValue(QStringLiteral("audio/mode"),
                   static_cast<int>(settings.audio_mode));
    store.setValue(QStringLiteral("output/directory"),
                   settings.output_directory);
    store.setValue(QStringLiteral("appearance/language"), settings.language);
    store.setValue(QStringLiteral("general/auto_start"), settings.auto_start);
    store.setValue(QStringLiteral("general/minimize_to_tray_on_start"),
                   settings.minimize_to_tray_on_start);
    store.setValue(QStringLiteral("general/close_to_tray"),
                   settings.close_to_tray);
    store.setValue(QStringLiteral("general/hide_notifications"),
                   settings.hide_notifications_while_recording);
    store.setValue(QStringLiteral("general/show_recording_complete"),
                   settings.show_recording_complete_notification);
    store.setValue(QStringLiteral("shortcuts/start_stop"),
                   settings.shortcut_start_stop);
    store.setValue(QStringLiteral("shortcuts/show_hide"),
                   settings.shortcut_show_hide);
    store.setValue(QStringLiteral("shortcuts/open_folder"),
                   settings.shortcut_open_folder);
    store.sync();
}

void ApplyAutoStart(bool enabled) {
    QSettings run(
        QStringLiteral(
            "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\"
            "Run"),
        QSettings::NativeFormat);
    if (enabled) {
        const QString executable =
            QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        run.setValue(QStringLiteral("HmmobEV"),
                     QStringLiteral("\"%1\"").arg(executable));
    } else {
        run.remove(QStringLiteral("HmmobEV"));
    }
    run.sync();
}

QString DefaultOutputDirectory() {
    QString directory =
        QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (directory.isEmpty()) {
        directory = QDir::tempPath();
    }
    return directory;
}
