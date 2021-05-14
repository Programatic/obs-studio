#pragma once

#include <json11.hpp>
#include "container-widget.hpp"
#include "switch.hpp"

#include <QWidget>
#include <QGridLayout>
#include <unordered_map>
#include <util/platform.h>

using namespace json11;

struct ServerInfo {
	std::string server;
	std::string key;
	Switch *widget;
};

class DashboardWidget : public QWidget {
private:
	QTimer *timer;
	QGridLayout *gridLayout;

	std::map<std::string, Json> passthroughs;
	std::unordered_map<std::string, ServerInfo> server_information;
	std::string id;
	std::string name;
	bool send_screenshot;
	os_cpu_usage_info_t *cpu_info;

	void send_update(std::string url);

public:
	DashboardWidget(QWidget *parent, Json parsed);
	~DashboardWidget();
};
