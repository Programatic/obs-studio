// Copied and modified from UI/screenshot-obj.hpp (because QT wouldnt let me subclass it)

#pragma once

#include "obs.hpp"

#include <QImage>

class ScreenshotObj {
public:
	ScreenshotObj(obs_source_t *source);
	~ScreenshotObj();
	void Screenshot();
	void Download();
	void Copy();
	std::string GetData();

	gs_texrender_t *texrender = nullptr;
	gs_stagesurf_t *stagesurf = nullptr;
	OBSWeakSource weakSource;
	std::string path;
	QImage image;
	uint32_t cx;
	uint32_t cy;
	unsigned char *mem = NULL;
    unsigned long mem_size = 0;

	int stage = 0;
	bool data_ready = false;
};
