#pragma once
#include <QtCore/QObject>
#include <functional>
#include <memory>
#include <string>

class AnimationEditorPanel;
class GUIClient;
class QSettings;

// Native-only adapter; reuses GestureUI/ResourceManager and keeps Qt out of GUIClient.
class AnimationEditorController final : public QObject
{
public:
	AnimationEditorController(AnimationEditorPanel* panel, const std::string& base_dir,
		QSettings* settings, GUIClient* client, std::function<void()> restore_main_context);
	~AnimationEditorController() override;
	void shutdown();
protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
