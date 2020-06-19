#pragma once
#include <mutex>
#include <obs.hpp>

struct frameData {
	obs_source_t *context;
	gs_texture *tex;
	int interval = 1000;
	float secSinceLastCheck = 0;
	bool grabFrame = false;
	std::timed_mutex m;
};

extern "C" struct obs_source_info helper_filter;
