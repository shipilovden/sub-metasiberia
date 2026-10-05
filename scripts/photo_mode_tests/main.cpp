// Isolated Qt panel regression: no GUIClient, network, world or user settings.
#include "../../gui_client/PhotoVideoSettingsPanel.h"
#include "../../gui_client/PhotoModeEffects.h"
#include <QApplication>
#include <QComboBox>
#include <QEventLoop>
#include <QPushButton>
#include <QSettings>
#include <QGroupBox>
#include <QTemporaryDir>
#include <QToolButton>
#include <QDebug>
#include <cstdio>

static void require(bool ok, const char* message) { if(!ok) qFatal("%s", message); }
static void waitFor(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}
template<class T> static T* control(QObject& owner, const char* name) {
    auto* result = owner.findChild<T*>(QString::fromLatin1(name));
    require(result != nullptr, name);
    return result;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setStyle(QStringLiteral("Fusion"));
    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary directory");
    QSettings settings(temporary.filePath("photo.ini"), QSettings::IniFormat);
    {
        PhotoVideoSettingsPanel panel(&settings);
        panel.setAttribute(Qt::WA_DontShowOnScreen);
        if(argc > 2) panel.setIconDirectory(QString::fromLocal8Bit(argv[2]));
        panel.resize(420, 850);
        panel.show();
        auto* camera = control<QComboBox>(panel, "photoCameraMode");
        auto* autofocus = control<QComboBox>(panel, "photoAutofocus");
        auto* aspect = control<QComboBox>(panel, "photoAspectRatio");
        auto* delay = control<QComboBox>(panel, "photoCaptureDelay");
        auto* preset = control<QComboBox>(panel, "photoPreset");
        auto* capture = control<QPushButton>(panel, "photoCapture");
        int changes = 0, captures = 0;
        QObject::connect(&panel, &PhotoVideoSettingsPanel::settingsChanged, [&changes](const QVariantMap&) { ++changes; });
        QObject::connect(&panel, &PhotoVideoSettingsPanel::capturePhotoRequested, [&captures](const QVariantMap&) { ++captures; });
        require(panel.currentSettings().value("focal_length_mm").toDouble() == 25, "default lens");
        camera->setCurrentIndex(camera->findData("free"));
        autofocus->setCurrentIndex(autofocus->findData("eye"));
        aspect->setCurrentIndex(aspect->findData(1.0));
        preset->setEditText("Regression preset");
        control<QToolButton>(panel, "photoSavePreset")->click();
        control<QPushButton>(panel, "photoReset")->click();
        require(panel.currentSettings().value("camera_mode") == "standard", "reset camera");
        require(panel.currentSettings().value("aspect_ratio").toDouble() == 0, "reset crop");
        const int index = preset->findText("Regression preset");
        require(index >= 0, "preset saved");
        preset->setCurrentIndex(index);
        QMetaObject::invokeMethod(preset, "activated", Q_ARG(int, index));
        const auto restored = panel.currentSettings();
        require(restored.value("camera_mode") == "free" && restored.value("autofocus_mode") == "eye", "preset restores modes");
        require(restored.value("aspect_ratio").toDouble() == 1.0, "preset restores crop");
        require(changes >= 5, "controls emit apply state");
		const int before_focus = changes;
		panel.updateAutofocusDistance(7.4);
		require(changes == before_focus && panel.currentSettings().value("focus_distance").toDouble() == 7.4, "live focus does not reapply settings");
#ifdef _WIN32
        require(control<QComboBox>(panel, "photoSection")->count() == 6, "six compact photo sections");
#endif
        auto* looks = control<QComboBox>(panel, "photoLook");
        QMetaObject::invokeMethod(looks, "activated", Q_ARG(int, 7));
        require(panel.currentSettings().value("warmth").toDouble() > 0 && panel.currentSettings().value("vignette").toDouble() > 0, "vintage applies actual controls");
        require(panel.currentSettings().value("camera_mode") == "free", "looks preserve camera mode");
        panel.setRecording(true);
        require(!control<QGroupBox>(panel, "photoVideoFormat")->isEnabled(), "recording locks encoder settings");
        panel.setRecordingFinalising();
        require(!control<QPushButton>(panel,"photoRecord")->isEnabled(), "finalisation cannot be restarted");
        panel.setRecording(false);
        capture->click();
        require(captures == 1, "immediate capture");
        delay->setCurrentIndex(delay->findData(3));
        capture->click();
        panel.cancelCapture();
        waitFor(3200);
        require(captures == 1, "cancelled timer cannot take photo");
        capture->click();
        waitFor(3400);
        require(captures == 2, "timer takes exactly one photo");
        if(argc > 1) require(panel.grab().save(QString::fromLocal8Bit(argv[1])), "panel preview saved");
    }
    PhotoVideoSettingsPanel restored(&settings);
    require(restored.currentSettings().value("camera_mode") == "free", "saved preset survives reopen");
	require(restored.currentSettings().value("capture_delay").toInt() == 3, "latest changes survive reopen even after saving a preset");
    require(PhotoModeEffects::frameRect(QSize(1920,1080),1.0)==QRect(420,0,1080,1080),"square crop geometry");
    QImage source(100,100,QImage::Format_RGB888); source.fill(QColor(100,100,100));
    QImage neutral=source; PhotoModeEffects::apply(neutral,{});
    require(neutral==source,"neutral look is exact identity");
    QImage warm=source; PhotoModeEffects::apply(warm,{{"warmth",1.0}});
    require(warm.pixelColor(50,50).red()>warm.pixelColor(50,50).blue(),"warm toning export");
    QImage vignette=source; PhotoModeEffects::apply(vignette,{{"vignette",1.0}});
    require(vignette.pixelColor(50,50)==source.pixelColor(50,50),"vignette keeps centre visible");
    require(vignette.pixelColor(0,0).red()<source.pixelColor(0,0).red(),"vignette darkens only edges");
    require(restored.currentSettings().value("warmth").toDouble()>0,"look survives reopen");
    std::puts("PASS: panel, presets, timer/cancel, look persistence, recording UI, crop, toning, vignette");
    return 0;
}
