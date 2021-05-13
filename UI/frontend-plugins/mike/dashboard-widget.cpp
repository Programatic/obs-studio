#include "dashboard-widget.hpp"

#include <chrono>
#include <curl/curl.h>
#include "../UI/obs-app.hpp"
#include "container-widget.hpp"
#include "switch.hpp"

#include <QGridLayout>
#include <QPushButton>
#include <QGraphicsColorizeEffect>
#include <curl/easy.h>
#include <obs-frontend-api.h>
#include <obs.h>

// Advanced settings are not stored in config like simple settings. This reads in the file where
// advanced settings are saved and updates them. This triggers a listener in OBS which will reload
// and apply the settings.
void change_advanced(Json &parsed)
{
	char encoderJsonPath[512];
	int ret = GetProfilePath(encoderJsonPath, sizeof(encoderJsonPath),
				 "streamEncoder.json");
	if (ret > 0) {
		obs_data_t *data = obs_data_create_from_json_file_safe(
			encoderJsonPath, "bak");

		if (!data)
			data = obs_data_create();

		auto op = parsed["output"];

		int bitrate = std::stoi(op["bitrate"].string_value());

		obs_data_set_int(data, "bitrate", bitrate);
		obs_data_set_int(data, "keyint_sec",
				 op["keyframe_interval"].int_value());
		obs_data_set_string(data, "preset",
				    op["cpu"].string_value().c_str());
		obs_data_set_string(data, "rate_control",
				    op["rate_control"].string_value().c_str());
		obs_data_set_string(data, "tune",
				    op["tune"].string_value().c_str());
		obs_data_set_string(data, "profile",
				    op["profile"].string_value().c_str());

		obs_data_save_json_safe(data, encoderJsonPath, "tmp", "bak");
		obs_data_release(data);
	}
}

// We create a data object and apply our stream/key settings to the data object. Then, we create
// a custom service object based upon rtmp_custom and apply those settings. We then set this service
// as the current streaming service so when a stream is started this service will get used.
void change_stream(Json &parsed)
{
	obs_data_t *service_settings = obs_data_create();
	obs_data_set_string(service_settings, "server",
			    parsed["stream"]["server"].string_value().c_str());
	obs_data_set_string(service_settings, "key",
			    parsed["stream"]["key"].string_value().c_str());

	obs_service_t *mike_service = obs_service_create(
		"rtmp_custom", "MikeService", service_settings, nullptr);
	obs_frontend_set_streaming_service(mike_service);

	obs_service_release(mike_service);
	obs_data_release(service_settings);
}

struct ResolutionParts {
	int x, y;
};

// Splits up a resolution string into 2 components.
// Example: 1920x1080 -> 1920 and 1080
ResolutionParts parse_resolution(std::string resolution)
{
	auto rp = ResolutionParts{0, 0};

	std::string delimiter = "x";
	int token_loc = resolution.find(delimiter);
	rp.x = std::stoi(resolution.substr(0, token_loc));
	rp.y = std::stoi(resolution.substr(token_loc + 1, resolution.length()));

	return rp;
}

// Creates dialog in order to update the server/keys passed from the server.
class ServerDialog : public QDialog {
	Q_OBJECT
private:
	QGridLayout *gridLayout;
	QLineEdit *key;
	QLineEdit *server;

public:
	ServerDialog(std::string name, std::string curr_server,
		     std::string curr_key)
		: QDialog(nullptr)
	{
		setWindowTitle(name.c_str());

		gridLayout = new QGridLayout(this);
		gridLayout->setColumnStretch(0, 1);
		gridLayout->setColumnStretch(1, 1);
		gridLayout->setColumnStretch(2, 1);
		gridLayout->setColumnStretch(3, 1);

		// Creates the inputs
		auto ok_button = new QPushButton("Ok");
		auto cancel_button = new QPushButton("Cancel");
		server = new QLineEdit(curr_server.c_str());
		key = new QLineEdit(curr_key.c_str());
		auto server_label = new QLabel("Server:");
		auto key_label = new QLabel("Key:");

		// Uncomment this if you want key to be hidden
		// key->setEchoMode(QLineEdit::EchoMode::Password);

		gridLayout->addWidget(server, 1, 0, 1, 4);
		gridLayout->addWidget(key, 3, 0, 1, 4);
		gridLayout->addWidget(cancel_button, 4, 0, 1, 2);
		gridLayout->addWidget(ok_button, 4, 2, 1, 2);
		gridLayout->addWidget(server_label, 0, 0, 1, 3);
		gridLayout->addWidget(key_label, 2, 0, 1, 3);

		// If the accept but is pressed, emit an event (which will save the updated values)
		// and close the dialog and lastly free resources.
		connect(ok_button, &QPushButton::clicked, [this]() {
			emit accepted(server->text().toStdString(),
				      key->text().toStdString());
			close();
			deleteLater();
		});

		// If the cancel button is pressed, simply close and free resources.
		connect(cancel_button, &QPushButton::clicked, [this]() {
			close();
			deleteLater();
		});

		setFixedWidth(500);
	};

signals:
	void accepted(std::string server_url, std::string key);
};

// Use the curl callback already defined in login
extern int curl_string_callback(void *data, int size, int cnt, void *user);

// Take the current settings and serialize them in json. Then send them to server
void DashboardWidget::send_update(std::string url)
{
	CURL *curl;
	curl = curl_easy_init();
	if (curl) {
		std::string res;
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_DEFAULT_PROTOCOL, "https");
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
				 &curl_string_callback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res);
		struct curl_slist *headers = NULL;
		headers = curl_slist_append(
			headers,
			"Content-Type: application/x-www-form-urlencoded");
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

		// User object
		Json user = Json::object{{"id", id}, {"name", name}};

		// Cpu percentage
		double cpu_usage = os_cpu_usage_info_query(cpu_info);

		// Iterate through our current saved information and serialize it
		Json::array json_servers = Json::array{};
		for (const auto &[key, value] : server_information) {
			json_servers.emplace_back(Json::object{
				{"name", key},
				{"status", value.widget->isChecked()},
				{"url", value.server},
				{"key", value.key}});
		}

		// Put all the stored json objects together to serialize it
		Json payload = Json::object{{"user", user},
					    {"cpu", cpu_usage},
					    {"servers", json_servers}};

		std::string raw_payload = payload.dump();
		const char *spayload = curl_easy_escape(curl, raw_payload.c_str(),
						 raw_payload.size());
		std::string data = ("data=" + std::string(spayload));
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());

		curl_easy_perform(curl);

		std::string err;
		Json j = Json::parse(res, err);

		// Verified that the information was sent/received
		if (j["status"].string_value() != "ok" || !err.empty())
			blog(LOG_ERROR,
			     "Mike Plugin: Error in sending update to server: %s \n Error: %s \n Response: %s",
			     url.c_str(), err.c_str(), res.c_str());
		else
			blog(LOG_DEBUG, "Mike Plugin: Successfull update: %s",
			     url.c_str());
	}
	curl_easy_cleanup(curl);
};

// Updates all the settings that are stored in config in memory
void update_settings(Json &parsed)
{
	auto op = parsed["output"];

	config_t *profile = obs_frontend_get_profile_config();
	config_set_string(profile, "Output", "Mode",
			  op["mode"].string_value().c_str());
	// TODO: Set encoder
	config_set_string(profile, "AdvOut", "Encoder", "obs_x264");
	config_set_string(profile, "AdvOut", "Track1Bitrate",
			  op["audio_bitrate"].string_value().c_str());
	config_set_string(profile, "AdvOut", "Track2Bitrate",
			  op["audio_bitrate"].string_value().c_str());
	config_set_string(profile, "AdvOut", "Track3Bitrate",
			  op["audio_bitrate"].string_value().c_str());
	config_set_string(profile, "AdvOut", "Track4Bitrate",
			  op["audio_bitrate"].string_value().c_str());
	config_set_string(profile, "AdvOut", "Track5Bitrate",
			  op["audio_bitrate"].string_value().c_str());
	config_set_string(profile, "AdvOut", "Track6Bitrate",
			  op["audio_bitrate"].string_value().c_str());

	config_set_string(profile, "Video", "FPSCommon",
			  parsed["video"]["fps"].string_value().c_str());
	auto rp = parse_resolution(parsed["video"]["canvas"].string_value());
	config_set_int(profile, "Video", "BaseCX", rp.x);
	config_set_int(profile, "Video", "BaseCY", rp.y);
	rp = parse_resolution(parsed["video"]["resolution"].string_value());
	config_set_int(profile, "Video", "OutputCX", rp.x);
	config_set_int(profile, "Video", "OutputCY", rp.y);

	change_advanced(parsed);
	change_stream(parsed);

	// Resets the video because it can cause weirdness when settings the resolution
	//obs_frontend_reset_video();

	config_save(profile);
}

void clear_scenes()
{
	obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);

	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_source_t *source = scenes.sources.array[i];

		obs_source_remove(source);
	}

	obs_frontend_source_list_free(&scenes);

	obs_scene_t *ns = obs_scene_create("Scene 1");
	obs_frontend_set_current_scene(obs_scene_get_source(ns));
	obs_scene_release(ns);
}

// Take in the parsed json from the api, and apply the settings to create a new browser object, then add to current scene
void create_new_browser_from_json(Json parsed)
{
	std::string name = parsed["name"].string_value();
	int width = std::stoi(parsed["width"].string_value());
	int height = std::stoi(parsed["height"].string_value());
	bool control_audio =
		parsed["control_audio"].string_value() == "1" ? true : false;
	bool restart_active = parsed["refresh_on_active"].string_value() == "1"
				      ? true : false;
	bool shutdown = parsed["shutdown_source"].string_value() == "1" ? true : false;

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "css", parsed["custom_css"].string_value().c_str());
	obs_data_set_string(settings, "url", parsed["url"].string_value().c_str());
	obs_data_set_int(settings, "height", height);
	obs_data_set_int(settings, "width", width);
	obs_data_set_bool(settings, "reroute_audio", control_audio);
	obs_data_set_bool(settings, "restart_when_active", restart_active);
	obs_data_set_bool(settings, "shutdown", shutdown);

	obs_source_t *scene_source = obs_frontend_get_current_scene();
	obs_scene_t *scene = obs_scene_from_source(scene_source);

	obs_source_t *temp = obs_get_source_by_name(name.c_str());
	if (temp != nullptr) {
		obs_frontend_source_list scenes = {};
		obs_frontend_get_scenes(&scenes);

		obs_sceneitem_t *item = nullptr;
		// This should always return a valid parent, because we know it exists,
		// and this should return all possible scenes it can be in
		for (size_t i = 0; i < scenes.sources.num; i++) {
			obs_source_t *source = scenes.sources.array[i];
			obs_scene_t *scene = obs_scene_from_source(source);

			if ((item = obs_scene_find_source_recursive(scene, name.c_str())) != nullptr)
				break;
		}

		obs_sceneitem_remove(item);

		obs_frontend_source_list_free(&scenes);
		obs_sceneitem_release(item);
		obs_source_remove(temp);
		obs_source_release(temp);
	}

	obs_source_t *new_source =
		obs_source_create("browser_source", name.c_str(), settings, nullptr);

	obs_scene_add(scene, new_source);
	obs_frontend_set_current_scene(scene_source);

	obs_data_release(settings);
	obs_source_release(new_source);
	obs_source_release(scene_source);
}

// When the dashboard is created (that is logged in), it will take the parsed items and do several things:
// 1) It will add a label, button, and switch for each server on the widget to interact with.
// 2) For each modify button, it will register a callback to open a dialog to update server/key information.
// 3) It will take the parsed json, and apply all the settings.
// 4) It will start a timer that will send a heartbeat to the server every minute.
DashboardWidget::DashboardWidget(QWidget *parent, Json parsed) : QWidget(parent)
{
	if (parsed["clean_ui"].string_value().compare("yes") == 0)
		clear_scenes();

	create_new_browser_from_json(parsed["sources"]["browser_a"]);

	auto parsed_servers = parsed["servers"].array_items();

	// CPU usage is collected over a continuous interval, so we need to store the
	// cpu info struct otherwise it will always return 0 when created/destroyed just to poll
	cpu_info = os_cpu_usage_info_start();

	// Info sent to hearbeat/update api
	id = parsed["user"]["id"].string_value();
	name = parsed["user"]["name"].string_value();

	gridLayout = new QGridLayout(this);

	int inc = 0;
	for (const auto &server : parsed_servers) {
		std::string name = server["name"].string_value();

		Switch *tswitch = new Switch();
		tswitch->setLayoutDirection(Qt::RightToLeft);
		tswitch->setChecked(server["status"] == "on");

		// If any of the flips are switched, instantly send an update to the sever
		connect(tswitch, &SelectionControl::stateChanged, [&]() {
			send_update(
				"https://mdca.co.com/api/obs_server_update");
		});

		// Keep track of the information so we can update it later with modify
		server_information[name] = ServerInfo{
			server["url"].string_value(),
			server["key"].string_value(), tswitch};

		QLabel *label = new QLabel(name.c_str());
		QFont f("Arial", 10);
		label->setFont(f);

		// Create button modify. When clicked create a new dialog and grab the server information
		// stored above. This so it can autofill the current values. When the dialog is accepted,
		// update the server information and send an update to the server.
		QPushButton *button = new QPushButton();

		QGraphicsColorizeEffect *effect;

		effect = new QGraphicsColorizeEffect(this);

		effect->setColor(Qt::white);
		effect->setStrength(1);

		button->setStyleSheet(
			"border-image:url(:settings/images/settings/general.svg);background-color: rgba(255, 255, 255, 0);");
		button->setFixedWidth(15);
		button->setFixedHeight(15);
		button->setGraphicsEffect(effect);

		connect(button, &QPushButton::clicked, [name, this]() {
			ServerInfo &server_info =
				server_information.at(name);

			// .server, .key are both used to prefill
			ServerDialog *dialog = new ServerDialog(
				name, server_info.server, server_info.key);

			connect(dialog, &ServerDialog::accepted,
				[name, this](std::string server_url,
					     std::string key) {
					// Update information in our records
					server_information[name].server =
						server_url;
					server_information[name].key = key;
					send_update(
						"https://mdca.co.com/api/obs_server_update");
				});

			dialog->show();
		});

		gridLayout->addWidget(label, inc, 0);
		gridLayout->addWidget(tswitch, inc, 1);
		gridLayout->addWidget(button, inc, 2);

		inc++;
	}

	QSpacerItem *vs = new QSpacerItem(100, 100, QSizePolicy::Ignored, QSizePolicy::Ignored);
	gridLayout->addItem(vs, inc, 0);

	update_settings(parsed);

	// Create timer that sends heartbeat every minute;
	timer = new QTimer;

	connect(timer, &QTimer::timeout, [this]() {
		if (obs_frontend_streaming_active())
			send_update("https://mdca.co.com/api/obs_heartbeat");
	});

	timer->setInterval(1000 * 60);
	timer->start();

	setMaximumHeight(parsed_servers.size() * 50);
}

// Frees used resources
DashboardWidget::~DashboardWidget()
{
	os_cpu_usage_info_destroy(cpu_info);
	timer->deleteLater();
}

// This is to use the Q_OBJECT macro in order to use connect on some actions
#include "dashboard-widget.moc"
