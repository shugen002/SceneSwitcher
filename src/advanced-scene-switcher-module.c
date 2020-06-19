#include <obs-module.h>

OBS_DECLARE_MODULE()

struct obs_source_info helper_filter;

void InitSceneSwitcher();
void FreeSceneSwitcher();


bool obs_module_load(void)
{
	obs_register_source(&helper_filter);
	InitSceneSwitcher();
	return true;
}

void obs_module_unload(void)
{
	FreeSceneSwitcher();
}
