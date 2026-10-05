// Standalone: link Qt5::Core; does not create a widget or start the client.
#include "../../gui_client/PhotoFrameRenderer.h"
#include <iostream>
#include <limits>

static void require(bool condition, const char* message)
{
	if(!condition) throw std::runtime_error(message);
}

template<class F> static void rejects(F action)
{
	bool rejected = false;
	try { action(); } catch(const std::runtime_error&) { rejected = true; }
	require(rejected, "Unsafe capture size/aspect was accepted");
}

int main()
{
	try
	{
		using namespace PhotoFrameRenderer;
		QVariantMap values;
		auto full = layout(QSize(1600, 900), values);
		require(full.scene_size == QSize(1600, 900) && full.output_size == full.scene_size, "Viewport capture changed dimensions");
		values.insert("aspect_ratio", 1.0);
		auto square = layout(QSize(1600, 900), values, QSize(3840, 2160));
		require(square.scene_size == QSize(3840, 2160), "Square capture did not render native detail");
		require(square.crop == QRect(840, 0, 2160, 2160) && square.output_size == QSize(2160, 2160), "Square crop/bounding box mismatch");
		values.insert("aspect_ratio", 16.0 / 9.0);
		auto widescreen = layout(QSize(1200, 900), values, QSize(3840, 2160));
		require(widescreen.scene_size == QSize(3840, 2880), "Capture changed FOV instead of rendering the full source view");
		require(widescreen.crop == QRect(0, 360, 3840, 2160), "Widescreen crop is not centered");
		values.insert("photo_resolution", "1920x1080");
		require(layout(QSize(1200, 900), values).output_size == QSize(1920, 1080), "Photo resolution ignored");
		require(layout(QSize(1200, 900), values, QSize(1280, 720)).output_size == QSize(1280, 720), "Video resolution did not override photo resolution");
		for(QSize viewport : {QSize(1600, 900), QSize(901, 1601), QSize(1235, 777)})
			for(double aspect : {0.0, 1.0, 4.0/3.0, 16.0/9.0, 9.0/16.0})
				for(QSize box : {QSize(320, 180), QSize(1920, 1080), QSize(2160, 3840)})
				{
					values.insert("aspect_ratio", aspect);
					const auto frame = layout(viewport, values, box);
					require(frame.output_size.width() <= box.width() && frame.output_size.height() <= box.height(), "Output exceeded bounding box");
					require(frame.output_size.width() <= frame.crop.width() && frame.output_size.height() <= frame.crop.height(), "Output requires upscaling readback");
					require(QRect(QPoint(), frame.scene_size).contains(frame.crop), "Crop outside scene");
					require(std::abs(double(frame.scene_size.width()) / frame.scene_size.height() - double(viewport.width()) / viewport.height()) < 0.02, "Original view aspect changed");
				}
		values.insert("aspect_ratio", 16.0/9.0);
		rejects([&]() { layout(QSize(1200, 900), values, QSize(7680, 4320)); }); // 44 MP full view
		rejects([&]() { layout(QSize(1600, 900), values, QSize(8193, 1)); });
		rejects([&]() { layout(QSize(), values); });
		rejects([&]() { layout(QSize(1600, 900), values, QSize(0, 1080)); });
		rejects([&]() { checkSize(QSize(4000, 2000), 2048, 2048); });
		values.insert("photo_resolution", "bad");
		rejects([&]() { layout(QSize(1600, 900), values); });
		values.insert("aspect_ratio", std::numeric_limits<double>::quiet_NaN());
		rejects([&]() { layout(QSize(1600, 900), values); });
		std::cout << "Photo frame geometry: PASS\n";
		return 0;
	}
	catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
