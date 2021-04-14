#include "container-widget.hpp"

#include "login-widget.hpp"
#include "dashboard-widget.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include "util/config-file.h"

#include <QMainWindow>
#include <QAction>
#include <QString>
#include <obs-service.h>
#include "../UI/obs-app.hpp"

#include <ctime>
#include <chrono>

#define ConfigSection "mike-obs-server"

// Create overall container widget. This widget is the parent that moderates both dashboard and login.
ContainerWidget::ContainerWidget(QWidget *parent) : QDockWidget(parent)
{
	setWindowTitle("Login");

	// Create a cb that gets passed to login widget when it gets created.
	// This callback will delete the login widget, and create a new dashboard widget on login.
	// It will set the active widget to the dashboard widget to interact with.
	current_widget = new LoginWidget(this, [&](Json parsed) {
		current_widget->deleteLater();
		current_widget = new DashboardWidget(this, parsed);
		setWindowTitle(QString::fromUtf8(
			parsed["title"].string_value().c_str()));
		setWidget(current_widget);
	});

	setMinimumWidth(250);

	setWidget(current_widget);
}

void clear_service()
{
	char serviceJsonPath[512];
	int ret = GetProfilePath(serviceJsonPath, sizeof(serviceJsonPath), "service.json");

	if (ret <= 0)
		return;

	obs_data_t *wrapper = obs_data_create();
	obs_data_t *settings = obs_data_create();

	obs_data_set_string(settings, "key", "");
	obs_data_set_string(settings, "server", "");
	obs_data_set_bool(settings, "bwtest", false);
	obs_data_set_bool(settings, "use_auth", false);

	obs_data_set_string(wrapper, "type", "rtmp_custom");
	obs_data_set_obj(wrapper, "settings", settings);

	obs_data_save_json_safe(wrapper, serviceJsonPath, "tmp", "bak");

	obs_data_release(wrapper);
	obs_data_release(settings);
}

void cb(obs_frontend_event e, void *)
{
	if (e == OBS_FRONTEND_EVENT_EXIT)
		clear_service();

}


// Register the plugin to OBS. This registers the dock to the frontened and puts it into the correct location
OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Ford Smith");
OBS_MODULE_USE_DEFAULT_LOCALE("mike_api", "en-US")

bool obs_module_load()
{
	obs_frontend_add_event_callback(cb, nullptr);

	const auto main_window =
		static_cast<QMainWindow *>(obs_frontend_get_main_window());
	obs_frontend_push_ui_translation(obs_module_get_string);
	auto dock = new ContainerWidget(main_window);
	auto action = (QAction *)obs_frontend_add_dock(dock);

	main_window->removeDockWidget(dock);
	auto docklocation = config_get_int(obs_frontend_get_global_config(),
					   ConfigSection, "DockLocation");
	auto visible = config_get_bool(obs_frontend_get_global_config(),
				       ConfigSection, "DockVisible");
	if (!config_has_user_value(obs_frontend_get_global_config(),
				   ConfigSection, "DockLocation")) {
		docklocation = Qt::DockWidgetArea::BottomDockWidgetArea;
	}
	if (!config_has_user_value(obs_frontend_get_global_config(),
				   ConfigSection, "DockVisible")) {
		visible = true;
	}

	main_window->addDockWidget((Qt::DockWidgetArea)docklocation, dock);
	if (visible) {
		dock->setVisible(true);
		action->setChecked(true);
	} else {
		dock->setVisible(false);
		action->setChecked(false);
	}

	return true;
}

void obs_module_unload() {}

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Description");
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return obs_module_text("MikeOBSApi");
}
