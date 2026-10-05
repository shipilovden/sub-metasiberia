// Standalone panel contract test. Compile with PhotoVideoSettingsPanel.cpp,
// LucideIconUtils.cpp and moc_PhotoVideoSettingsPanel.cpp; link Qt Widgets + Test.
// No client, renderer, audio capture, real user settings or CMake changes required.
#include "../../gui_client/PhotoVideoSettingsPanel.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest/QTest>
#include <cstdio>
#include <limits>

static void require(bool ok, const char* message) { if(!ok) qFatal("%s", message); }
template<class T> static T* control(QObject& owner, const char* name) {
    T* result = owner.findChild<T*>(QString::fromLatin1(name));
    require(result != nullptr, name);
    return result;
}
static void activate(QComboBox* combo, int index) {
    combo->setCurrentIndex(index);
    require(QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, index)), "combo activation");
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setStyle(QStringLiteral("Fusion"));
    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary settings directory");
    QSettings settings(temporary.filePath("panel.ini"), QSettings::IniFormat);
    const QString root = QStringLiteral("photo_video_editor/");
    QVariantMap saved;
    {
        PhotoVideoSettingsPanel panel(&settings);
        panel.setAttribute(Qt::WA_DontShowOnScreen);
        panel.resize(330, 850);
        panel.show();
        QTest::qWait(10);
        auto* section = control<QComboBox>(panel, "photoSection");
        auto* pages = panel.findChild<QStackedWidget*>();
        require(section->count() == 6 && pages && pages->count() == 6, "six sections");
        const QVariantMap defaults = panel.currentSettings();
        const QVariantMap pro_defaults = {
            {"filter_strength", 1.0}, {"contrast", 0.0}, {"shadows", 0.0}, {"highlights", 0.0},
            {"blacks", 0.0}, {"whites", 0.0}, {"temperature_kelvin", 6500.0}, {"wb_tint", 0.0},
            {"grain", 0.0}, {"grain_size", 1.0}, {"shadow_hue", 0.0}, {"highlight_hue", 35.0},
            {"split_strength", 0.0}, {"vignette_radius", 0.75}, {"vignette_softness", 0.5},
            {"vignette_roundness", 1.0}, {"vignette_center_x", 0.0}, {"vignette_center_y", 0.0},
            {"diffusion", 0.0}, {"halation", 0.0}, {"aberration", 0.0}, {"distortion", 0.0},
            {"lut_strength", 1.0}, {"trajectory_duration", 8.0}
        };
        for(auto it = pro_defaults.cbegin(); it != pro_defaults.cend(); ++it)
            require(defaults.value(it.key()) == it.value(), qPrintable(it.key()));
        require(defaults.value("focal_length_mm").toDouble() == 25 && defaults.value("focus_distance").toDouble() == 3,
            "existing lens/focus defaults");
        require(defaults.value("frame_rate").toInt() == 30 && defaults.value("bitrate_mbps").toInt() == 20,
            "existing video defaults");
        require(defaults.value("photo_resolution") == "viewport", "viewport default");
        for(const char* key : {"grain_colour", "clipping_zebra", "focus_peaking", "show_histogram", "trajectory_loop", "microphone", "world_audio", "compare_original"})
            require(defaults.contains(key) && !defaults.value(key).toBool(), key);

        int changes = 0;
        QVariantMap emitted;
        QObject::connect(&panel, &PhotoVideoSettingsPanel::settingsChanged, [&](const QVariantMap& state) { ++changes; emitted = state; });
        // Every slider, including legacy optics/tones, resets just its own value and keeps its readout in sync.
        for(QDoubleSpinBox* spin : panel.findChildren<QDoubleSpinBox*>()) {
            const double initial = spin->value();
            spin->setValue(initial == spin->maximum() ? spin->minimum() : spin->maximum());
            auto* reset = control<QToolButton>(*spin->parentWidget(), "photoSliderReset");
            const int before = changes;
            reset->click();
            require(spin->value() == initial && changes == before + 1, "individual slider reset emits once");
            auto* slider = spin->parentWidget()->findChild<QSlider*>();
            require(slider != nullptr, "slider readout");
            int multiplier = 1;
            for(int i = 0; i < spin->decimals(); ++i) multiplier *= 10;
            require(slider->value() == qRound(initial * multiplier), "reset slider sync");
        }
        auto* contrast = control<QDoubleSpinBox>(panel, "photo_contrast");
        contrast->setValue(0.25);
        control<QDoubleSpinBox>(panel, "photo_grain")->setValue(0.3);
        control<QToolButton>(*control<QDoubleSpinBox>(panel, "photo_grain")->parentWidget(), "photoSliderReset")->click();
        require(contrast->value() == 0.25, "reset preserves other controls");

        auto* autofocus = control<QComboBox>(panel, "photoAutofocus");
        autofocus->setCurrentIndex(autofocus->findData("eye"));
        int before = changes;
        panel.updateAutofocusDistance(7.4);
        require(changes == before && panel.currentSettings().value("focus_distance").toDouble() == 7.4, "autofocus no feedback");
        int focus_signals = 0, focus_picks = 0;
        QObject::connect(&panel, &PhotoVideoSettingsPanel::autofocusModeChanged, [&](const QString& mode) { require(mode == "off", "manual focus mode signal"); ++focus_signals; });
        QObject::connect(&panel, &PhotoVideoSettingsPanel::focusPickRequested, [&]() { ++focus_picks; });
        control<QPushButton>(panel, "photoFocusPick")->click();
        panel.setManualFocusDistance(12.5);
        require(focus_picks == 1 && focus_signals == 1 && changes == before + 1, "manual focus signals");
        require(emitted.value("autofocus_mode") == "off" && emitted.value("focus_distance").toDouble() == 12.5, "manual focus atomic state");
        before = changes;
        panel.setManualFocusDistance(std::numeric_limits<double>::quiet_NaN());
        require(changes == before, "invalid manual focus ignored");
        panel.setCameraMode("free");
        require(changes == before + 1 && emitted.value("camera_mode") == "free", "setCameraMode emits state");
        panel.setCameraMode("invalid mode");
        require(changes == before + 1, "unknown camera mode ignored");
        {
            const QSignalBlocker blocker(autofocus);
            autofocus->setCurrentIndex(autofocus->findData("eye"));
        }
        before = changes;
        panel.setPathOptics(9.5, 85, -12);
        require(changes == before + 1 && focus_signals == 2, "path optics emits one complete update");
        require(emitted.value("autofocus_mode") == "off" && emitted.value("focus_distance").toDouble() == 9.5 &&
            emitted.value("focal_length_mm").toDouble() == 85 && emitted.value("roll_degrees").toDouble() == -12,
            "path optics updates focus lens roll atomically");
        panel.setPathOptics(12.5, 25, 0);

        auto* compare = control<QPushButton>(panel, "photoCompareOriginal");
        section->setCurrentIndex(1);
        QTest::mousePress(compare, Qt::LeftButton);
        require(panel.currentSettings().value("compare_original").toBool() && emitted.value("compare_original").toBool(), "hold compare");
        contrast->setValue(0.3);
        QTest::qWait(300);
        require(!settings.value(root + "current_state").toMap().contains("compare_original"), "autosave excludes compare");
        auto* presets = control<QComboBox>(panel, "photoPreset");
        presets->setEditText("Pro regression");
        control<QToolButton>(panel, "photoSavePreset")->click();
        const QString preset_key = root + "presets/" + QString::fromLatin1(QByteArray("Pro regression").toHex()) + "/state";
        require(!settings.value(preset_key).toMap().contains("compare_original"), "preset excludes compare");
        QTest::mouseRelease(compare, Qt::LeftButton);
        require(!emitted.value("compare_original").toBool(), "release compare");
        QTest::mousePress(compare, Qt::LeftButton);
        section->setCurrentIndex(2);
        require(!panel.currentSettings().value("compare_original").toBool(), "section change releases compare");
        QTest::mousePress(compare, Qt::LeftButton);
        panel.hide();
        require(!panel.currentSettings().value("compare_original").toBool(), "hidden panel releases compare");
        panel.show();

        auto* microphone = control<QCheckBox>(panel, "photoMicrophone");
        auto* world_audio = control<QCheckBox>(panel, "photoWorldAudio");
#ifdef _WIN32
        require(microphone->isEnabled() && world_audio->isEnabled(), "Windows audio controls available");
#endif
        microphone->setChecked(true); world_audio->setChecked(true);
        control<QToolButton>(panel, "photoSavePreset")->click();
        activate(presets, presets->findText("Pro regression"));
        require(!microphone->isChecked() && !world_audio->isChecked(), "preset cannot opt into audio");

        auto* camera = control<QComboBox>(panel, "photoCameraMode");
        camera->setCurrentIndex(camera->findData("free"));
        auto* looks = control<QComboBox>(panel, "photoLook");
        activate(looks, 7);
        require(panel.currentSettings().value("warmth").toDouble() > 0, "legacy vintage recipe");
        activate(looks, 8);
        require(panel.currentSettings().value("shadow_hue").toDouble() == 190 && panel.currentSettings().value("split_strength").toDouble() > 0, "cinematic recipe");
        activate(looks, 9);
        require(panel.currentSettings().value("blacks").toDouble() > 0 && panel.currentSettings().value("grain").toDouble() > 0, "matte recipe");
        activate(looks, 10);
        require(panel.currentSettings().value("saturation").toDouble() == 0 && contrast->value() > 0, "noir recipe");
        require(panel.currentSettings().value("camera_mode") == "free" && panel.currentSettings().value("focus_distance").toDouble() == 12.5, "recipes preserve optics");
        control<QLineEdit>(panel, "photoLutPath")->setText("test.cube");
        control<QToolButton>(panel, "photoLutClear")->click();
        require(panel.currentSettings().value("lut_path").toString().isEmpty(), "clear LUT");

        QStringList actions;
        QObject::connect(&panel, &PhotoVideoSettingsPanel::trajectoryActionRequested, [&](const QString& action) { actions.append(action); });
        for(const char* action : {"add", "remove", "clear", "play", "stop"})
            control<QPushButton>(panel, qPrintable(QStringLiteral("photoTrajectory_") + action))->click();
        require(actions == QStringList({"add", "remove", "clear", "play", "stop"}), "trajectory action contract");
        panel.setTrajectoryStatus("3 кадра / 8 с");
        require(control<QLabel>(panel, "photoTrajectoryStatus")->text() == "3 кадра / 8 с", "trajectory status");
        panel.setRecording(true);
#ifdef _WIN32
        require(!control<QGroupBox>(panel, "photoVideoFormat")->isEnabled() && !microphone->isEnabled(), "recording locks format and audio");
#endif
        require(control<QPushButton>(panel, "photoTrajectory_stop")->isEnabled(), "trajectory stop remains available while recording");
        panel.setRecordingFinalising();
        require(!control<QPushButton>(panel, "photoRecord")->isEnabled(), "finalising locks record");
        panel.setRecording(false);

        control<QCheckBox>(panel, "photo_show_histogram")->setChecked(true);
        QImage histogram(256, 80, QImage::Format_RGB32); histogram.fill(Qt::green);
        before = changes;
        panel.setHistogram(histogram);
        require(!control<QLabel>(panel, "photoHistogram")->pixmap(Qt::ReturnByValue).isNull() && changes == before, "histogram without settings feedback");
        panel.setHistogram(QImage());
        require(control<QLabel>(panel, "photoHistogram")->pixmap(Qt::ReturnByValue).isNull(), "clear stale histogram");
        auto* resolution = control<QComboBox>(panel, "photoResolution");
        require(resolution->count() == 4, "photo resolution choices");
        resolution->setCurrentIndex(resolution->findData("7680x4320"));

        for(int i = 0; i < section->count(); ++i) {
            section->setCurrentIndex(i);
            QTest::qWait(10);
            auto* scroll = qobject_cast<QScrollArea*>(pages->currentWidget());
            require(scroll && scroll->horizontalScrollBar()->maximum() == 0, qPrintable(QString("section %1 fits 330px sidebar").arg(i)));
            require(compare->isVisible() == (i == 1 || i == 2), "compare visible only for colour/effects");
            if(argc > 1) require(panel.grab().save(QString::fromLocal8Bit(argv[1]) + QString("/section-%1.png").arg(i)), "save panel preview");
        }
        int captures = 0;
        QObject::connect(&panel, &PhotoVideoSettingsPanel::capturePhotoRequested, [&](const QVariantMap& state) {
            ++captures; require(!state.contains("compare_original"), "capture excludes preview compare");
        });
        control<QPushButton>(panel, "photoCapture")->click();
        require(captures == 1, "immediate capture");
        auto* delay = control<QComboBox>(panel, "photoCaptureDelay");
        delay->setCurrentIndex(delay->findData(3));
        control<QPushButton>(panel, "photoCapture")->click();
        panel.cancelCapture();
        QTest::qWait(3200);
        require(captures == 1, "cancelled timer cannot capture");
        control<QPushButton>(panel, "photoCapture")->click();
        QTest::qWait(3400);
        require(captures == 2, "timer captures once");
        for(auto it = pro_defaults.cbegin(); it != pro_defaults.cend(); ++it) {
            auto* spin = control<QDoubleSpinBox>(panel, qPrintable(QStringLiteral("photo_") + it.key()));
            spin->setValue(spin->maximum());
        }
        for(const char* key : {"grain_colour", "clipping_zebra", "focus_peaking", "trajectory_loop"})
            control<QCheckBox>(panel, qPrintable(QStringLiteral("photo_") + key))->setChecked(true);
        control<QLineEdit>(panel, "photoLutPath")->setText("test.cube");
        microphone->setChecked(true); world_audio->setChecked(true);
        saved = panel.currentSettings();
    }
    QVariantMap legacy_state = settings.value(root + "current_state").toMap();
    legacy_state.insert("compare_original", true);
    settings.setValue(root + "current_state", legacy_state);
    {
        PhotoVideoSettingsPanel reopened(&settings);
        const QVariantMap restored = reopened.currentSettings();
        for(auto it = saved.cbegin(); it != saved.cend(); ++it) {
            if(it.key() == "microphone" || it.key() == "world_audio")
                require(!restored.value(it.key()).toBool(), "reopen cannot opt into audio");
            else require(restored.value(it.key()) == it.value(), qPrintable(it.key()));
        }
        control<QPushButton>(reopened, "photoReset")->click();
        require(reopened.currentSettings().value("filter_strength").toDouble() == 1 && reopened.currentSettings().value("photo_resolution") == "viewport", "global reset restores pro defaults");
    }
    std::puts("PASS: pro panel defaults/reset, presets/persistence, temporary compare, focus/histogram, recipes, audio opt-in, trajectory, compact layout, capture/timer");
    return 0;
}
