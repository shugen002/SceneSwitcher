#include <obs-module.h>
#include "headers/helper-filter.hpp"
#include "headers/advanced-scene-switcher.hpp"

#define INVERVAL_S "interval"

static const char *helper_filter_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "Advanced Scene Switcher helper";
}

static void helper_filter_update(void *data, obs_data_t *settings)
{
	struct frameData *filter_data = (frameData *)data;
	filter_data->interval = obs_data_get_int(settings, INVERVAL_S);
	if (filter_data->interval == 0)
		filter_data->interval = 300;
}

static void *helper_filter_create(obs_data_t *settings, obs_source_t *context)
{
	struct frameData *filter_data = new frameData();
	filter_data->context = context;
	filter_data->interval = 300;
	OBSWeakSource ws = obs_source_get_weak_source(context);
	switcher->helperFilterM.lock();
	switcher->filterSourceToFrames.insert(
		std::pair<OBSWeakSource, frameData *>(ws, filter_data));
	switcher->helperFilterM.unlock();
	obs_weak_source_release(ws);
	helper_filter_update(filter_data, settings);

	return filter_data;
}

static void helper_filter_destroy(void *data)
{
	struct frameData *filter_data = (frameData *)data;
	OBSWeakSource ws = obs_source_get_weak_source(filter_data->context);
	//switcher->helperFilterM.lock();
	//auto it = switcher->filterSourceToFrames.find(ws);
	//if (it != switcher->filterSourceToFrames.end()) {
	//	switcher->filterSourceToFrames.erase(it);
	//}
	//switcher->helperFilterM.unlock();
	obs_weak_source_release(ws);
	gs_texture_destroy(filter_data->tex);
	delete filter_data;
}

static obs_properties_t *helper_filter_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();

	obs_property_t *p = obs_properties_add_int(
		props, INVERVAL_S, "Grab frame every", 300, 60000, 1);
	obs_property_int_set_suffix(p, " ms");
	return props;
}

static void helper_filter_remove(void *data, obs_source_t *parent)
{
	UNUSED_PARAMETER(parent);
	UNUSED_PARAMETER(data);
}
static void helper_filter_tick(void *data, float t)
{
	struct frameData *filter_data = (frameData *)data;
	filter_data->secSinceLastCheck += t;
	if (filter_data->secSinceLastCheck * 1000 < filter_data->interval) {
		return;
	}
	filter_data->secSinceLastCheck = 0;
	filter_data->grabFrame = true;
}

static void helper_filter_render(void *data, gs_effect_t *effect)
{
	struct frameData *filter_data = (frameData *)data;

	// do nothing
	gs_effect_t *default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	if (!obs_source_process_filter_begin(filter_data->context, GS_RGBA,
					     OBS_ALLOW_DIRECT_RENDERING))
		return;

	if (filter_data->grabFrame && filter_data->m.try_lock()) {
		gs_texture *cur_tex = gs_get_render_target();
		gs_texture_destroy(filter_data->tex);

		// probably need to switch to staging surface
		filter_data->tex =
			gs_texture_create(gs_texture_get_width(cur_tex),
					  gs_texture_get_height(cur_tex),
					  GS_RGBA, 1, nullptr, GS_DYNAMIC);
		gs_copy_texture(filter_data->tex, cur_tex);

		gs_stagesurf_t *stage = gs_stagesurface_create(
			gs_texture_get_width(cur_tex),
			gs_texture_get_height(cur_tex), GS_RGBA);
		gs_stage_texture(stage, cur_tex);

		// testing
		uint8_t *ptr1, *ptr2;
		uint32_t linesize1, linesize2;
		gs_stagesurface_map(stage, &ptr1, &linesize1);

		filter_data->grabFrame = false;
		filter_data->m.unlock();
	}

	obs_source_process_filter_end(
		filter_data->context, default_effect,
		obs_source_get_width(
			obs_filter_get_target(filter_data->context)),
		obs_source_get_height(
			obs_filter_get_target(filter_data->context)));

	UNUSED_PARAMETER(effect);
}

//gs_get_render_target
struct obs_source_info helper_filter = {
	"ass_helper_filter",
	OBS_SOURCE_TYPE_FILTER,
	OBS_SOURCE_VIDEO,
	helper_filter_name,
	helper_filter_create,
	helper_filter_destroy,
	NULL,
	NULL,
	NULL,
	helper_filter_properties,
	helper_filter_update,
	NULL,
	NULL,
	NULL,
	NULL,
	helper_filter_tick,
	helper_filter_render,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	helper_filter_remove,
};
