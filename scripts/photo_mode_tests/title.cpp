// Isolated rendering/behaviour test of the production MainWindow dock helpers.
#include <QApplication>
#include <QDockWidget>
#include <QMainWindow>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QStyle>
#include <QImage>
#include <QDebug>
#include "photo_title_helpers.h"

static void require(bool condition, const char* message) {
    if(!condition) qFatal("%s", message);
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setStyle("Fusion");
    QMainWindow window;
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.resize(600, 400);
    auto* dock = new QDockWidget(QStringLiteral("Фоторежим"), &window);
    dock->setAttribute(Qt::WA_DontShowOnScreen);
    dock->setObjectName("photoVideoSettingsDockWidget");
    dock->setWidget(new QLabel("Panel body"));
    window.addDockWidget(Qt::RightDockWidgetArea, dock);
    installEditorDockTitleBar(dock);
    window.show();
    auto* title = dock->titleBarWidget();
    auto* label = title->findChild<QLabel*>("editorDockTitleLabel");
    auto* float_button = title->findChild<QToolButton*>("editorDockFloatButton");
    auto* close_button = title->findChild<QToolButton*>("editorDockCloseButton");
    require(label && float_button && close_button, "title controls installed");
    for(const QColor background : {QColor("#29272b"), QColor("#eeeeee"), QColor("#000000")}) {
        QPalette palette;
        palette.setColor(QPalette::Window, background);
        palette.setColor(QPalette::WindowText, background.lightness() < 128 ? Qt::white : Qt::black);
        app.setPalette(palette);
        window.setStyleSheet(QString("QDockWidget { background: %1; } QLabel { color: red; }") .arg(background.name()));
        applyEditorDockTitleBarTheme(dock, palette);
        for(bool floating : {false, true}) {
            dock->setFloating(floating);
            for(auto group : {QPalette::Active, QPalette::Inactive}) {
                QPalette title_palette = title->palette();
                title_palette.setCurrentColorGroup(group);
                title->setPalette(title_palette);
                app.processEvents();
                const QImage image = title->grab().toImage();
                require(!image.isNull(), "title renders");
                require(image.pixelColor(image.width()/2, image.height()/2) == QColor("#34343a"), "title stays dark");
                require(label->palette().color(group, QPalette::WindowText) == QColor("#e4e4e7"), "title text stays light");
                require(dock->widget()->styleSheet().isEmpty(), "no body stylesheet override");
            }
        }
    }
    dock->setWindowTitle("Photo mode translated");
    require(label->text() == dock->windowTitle(), "title follows translation");
    float_button->click();
    require(!dock->isFloating(), "float button docks panel");
    float_button->click();
    require(dock->isFloating(), "float button undocks panel");
    if(argc > 1) require(title->grab().save(QString::fromLocal8Bit(argv[1])), "save title preview");
    close_button->click();
    require(dock->isHidden(), "close button hides panel");
    qInfo("PASS: dark photo title in three themes, docked/floating, active/inactive; translation and buttons");
}
