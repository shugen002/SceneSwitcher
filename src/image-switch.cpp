#include <QFileDialog>
#include <QTextStream>
#include <obs.hpp>
#include <libswscale/swscale.h>

#include <obs-module.h>
#include <util/circlebuf.h>

#include <media-io/video-scaler.h>

#include <graphics/graphics.h>

#include "headers/advanced-scene-switcher.hpp"
#include "headers/helper-filter.hpp"

#define SCALE_TARGET_WIDTH 9
#define SCALE_TARGET_HEIGHT 8

//OBS SORUCE FRAME APPROACH DOES NOT WORK AS OBS_SOURCE FRAME ONLY USED IN ASYNC STUFF (eg Media source)
// -> so switch to using textures
// -> use image-file helpers to load texture from file

void SceneSwitcher::on_imgCmpMatchType_currentIndexChanged(int idx)
{
	if (idx == -1)
		return;

	if ((imgCmpMatchType)ui->imgCmpMatchType->currentIndex() ==
	    IMG_CMP_EXACT_MATCH) {
		ui->imgCmpSimilaritySlider->setDisabled(true);
	} else {
		ui->imgCmpSimilaritySlider->setDisabled(false);
	}
}

void SceneSwitcher::on_imgCmpBrowse_clicked()
{
	QString path = QFileDialog::getOpenFileName(
		this, tr("Select a file to compare to ..."),
		QDir::currentPath(), tr("Any files (*.*)"));
	if (!path.isEmpty())
		ui->imgCmpFilePath->setText(path);
}

void SceneSwitcher::on_imgCmpSwitches_currentRowChanged(int idx)
{
	if (loading)
		return;
	if (idx == -1)
		return;

	QListWidgetItem *item = ui->imgCmpSwitches->item(idx);

	QString imgCmpStr = item->data(Qt::UserRole).toString();

	std::lock_guard<std::mutex> lock(switcher->m);
	for (auto &s : switcher->imgCmpSwitches) {
		if (imgCmpStr.compare(s.imgCmpSwitchStr.c_str()) == 0) {
			QString sceneName = GetWeakSourceName(s.scene).c_str();
			QString sourceName =
				GetWeakSourceName(s.source).c_str();
			QString transitionName =
				GetWeakSourceName(s.transition).c_str();
			ui->imgCmpScenes->setCurrentText(sceneName);
			ui->imgCmpSources->setCurrentText(sourceName);
			ui->imgCmpTransitions->setCurrentText(transitionName);
			ui->imgCmpMatchType->setCurrentIndex(s.matchType);
			if (s.matchType == IMG_CMP_EXACT_MATCH)
				ui->imgCmpSimilaritySlider->setValue(0);
			else
				ui->imgCmpSimilaritySlider->setValue(
					s.similarity);
			ui->imgCmpFilePath->setText(
				QString::fromStdString(s.filePath));
			break;
		}
	}
}

void SceneSwitcher::on_imgCmpUp_clicked()
{
	int index = ui->imgCmpSwitches->currentRow();
	if (index != -1 && index != 0) {
		ui->imgCmpSwitches->insertItem(
			index - 1, ui->imgCmpSwitches->takeItem(index));
		ui->imgCmpSwitches->setCurrentRow(index - 1);

		std::lock_guard<std::mutex> lock(switcher->m);

		iter_swap(switcher->imgCmpSwitches.begin() + index,
			  switcher->imgCmpSwitches.begin() + index - 1);
	}
}

void SceneSwitcher::on_imgCmpDown_clicked()
{
	int index = ui->imgCmpSwitches->currentRow();
	if (index != -1 && index != ui->imgCmpSwitches->count() - 1) {
		ui->imgCmpSwitches->insertItem(
			index + 1, ui->imgCmpSwitches->takeItem(index));
		ui->imgCmpSwitches->setCurrentRow(index + 1);

		std::lock_guard<std::mutex> lock(switcher->m);

		iter_swap(switcher->imgCmpSwitches.begin() + index,
			  switcher->imgCmpSwitches.begin() + index + 1);
	}
}

int SceneSwitcher::imgCmpFindByData(const QString &imgCmpStr)
{
	int count = ui->imgCmpSwitches->count();
	int idx = -1;

	for (int i = 0; i < count; i++) {
		QListWidgetItem *item = ui->imgCmpSwitches->item(i);
		QString str = item->data(Qt::UserRole).toString();

		if (str == imgCmpStr) {
			idx = i;
			break;
		}
	}

	return idx;
}

static inline void removeHelperFilter(obs_source_t *source)
{
	auto sourceEnum = [](obs_source_t *parent, obs_source_t *child,
			     void *data) -> void {
		UNUSED_PARAMETER(data);
		if (strcmp(obs_source_get_id(child), "ass_helper_filter") ==
		    0) {
			obs_source_filter_remove(parent, child);
			obs_source_get_name(parent);
			obs_source_remove(child);
		}
	};

	obs_source_enum_filters(source, sourceEnum, nullptr);
}

void SwitcherData::removeUnusedHelperFilters()
{
	auto sourceEnum = [](void *data, obs_source_t *source) -> bool {
		std::vector<ImgCmpSwitch> *switches =
			(std::vector<ImgCmpSwitch> *)data;
		OBSWeakSource ws = obs_source_get_weak_source(source);
		std::string n = obs_source_get_name(source);
		obs_weak_source_release(ws);
		for (auto &s : *switches) {
			if (ws == s.source)
				return true;
		}
		removeHelperFilter(source);
		return true;
	};

	obs_enum_sources(sourceEnum, &switcher->imgCmpSwitches);
	obs_enum_scenes(sourceEnum, &switcher->imgCmpSwitches);
}

static inline void addHelperFilter(std::string sourceName)
{
	obs_source_t *source = obs_get_source_by_name(sourceName.c_str());
	OBSWeakSource ws = obs_source_get_weak_source(source);
	obs_weak_source_release(ws);

	if (switcher->weakSoruceToFilterSource.find(ws) !=
	    switcher->weakSoruceToFilterSource.end()) {
		obs_source_release(source);
		return;
	}

	auto sourceEnum = [](obs_source_t *parent, obs_source_t *child,
			     void *data) -> void {
		UNUSED_PARAMETER(parent);
		std::string *s = (std::string *)data;
		if (strcmp(obs_source_get_id(child), "ass_helper_filter") ==
		    0) {
			*s = obs_source_get_name(child);
		}
	};
	std::string existingFilterName = "";
	obs_source_enum_filters(source, sourceEnum, &existingFilterName);

	if (existingFilterName != "") {
		obs_source_t *filter = obs_source_get_filter_by_name(
			source, existingFilterName.c_str());
		OBSWeakSource filterWs = obs_source_get_weak_source(filter);

		switcher->weakSoruceToFilterSource.insert(
			std::pair<OBSWeakSource, OBSWeakSource>(ws, filterWs));

		obs_weak_source_release(filterWs);
		obs_source_release(filter);
	} else {
		obs_source_t *filter = obs_source_create_private(
			"ass_helper_filter", "Advanced Scene Switcher helper",
			NULL);
		obs_source_filter_add(source, filter);
		OBSWeakSource ws = obs_source_get_weak_source(source);
		OBSWeakSource newFilterWs = obs_source_get_weak_source(filter);

		switcher->weakSoruceToFilterSource.insert(
			std::pair<OBSWeakSource, OBSWeakSource>(ws,
								newFilterWs));

		obs_weak_source_release(ws);
		obs_weak_source_release(newFilterWs);
		obs_source_release(filter);
	}

	obs_source_release(source);
}

void SceneSwitcher::on_imgCmpAdd_clicked()
{
	QString sourceName = ui->imgCmpSources->currentText();
	QString sceneName = ui->imgCmpScenes->currentText();
	QString transitionName = ui->imgCmpTransitions->currentText();
	int similarity = -1;
	imgCmpMatchType matchType =
		(imgCmpMatchType)ui->imgCmpMatchType->currentIndex();
	if (matchType == IMG_CMP_SIMILAR) {
		similarity = ui->imgCmpSimilaritySlider->value();
	}
	QString filePath = ui->imgCmpFilePath->text();

	if (sceneName.isEmpty() || transitionName.isEmpty() ||
	    sourceName.isEmpty() || filePath.isEmpty())
		return;

	OBSWeakSource source = GetWeakSourceByQString(sourceName);
	OBSWeakSource scene = GetWeakSourceByQString(sceneName);
	OBSWeakSource transition = GetWeakTransitionByQString(transitionName);

	QString switchText = MakeImgCmpSwitchName(sourceName, matchType,
						  similarity, filePath,
						  sceneName, transitionName);

	if (imgCmpFindByData(switchText) != -1)
		return;

	QVariant v = QVariant::fromValue(switchText);

	QListWidgetItem *item =
		new QListWidgetItem(switchText, ui->imgCmpSwitches);
	item->setData(Qt::UserRole, v);

	std::lock_guard<std::mutex> lock(switcher->m);

	switcher->imgCmpSwitches.emplace_back(
		scene, transition, source, matchType, similarity,
		filePath.toUtf8().constData(),
		(sceneName == QString(PREVIOUS_SCENE_NAME)),
		switchText.toUtf8().constData());

	addHelperFilter(sourceName.toUtf8().constData());
}

void SceneSwitcher::on_imgCmpRemove_clicked()
{
	QListWidgetItem *item = ui->imgCmpSwitches->currentItem();
	if (!item)
		return;

	std::string imgCmpStr =
		item->data(Qt::UserRole).toString().toUtf8().constData();
	{
		std::lock_guard<std::mutex> lock(switcher->m);
		auto &switches = switcher->imgCmpSwitches;

		for (auto it = switches.begin(); it != switches.end(); ++it) {
			auto &s = *it;

			if (s.imgCmpSwitchStr == imgCmpStr) {
				switches.erase(it);
				break;
			}
		}
	}

	delete item;
}

void SwitcherData::saveImgCmpSettings(obs_data_t *obj)
{
	obs_data_array_t *imgCmpArray = obs_data_array_create();
	for (ImgCmpSwitch &s : switcher->imgCmpSwitches) {
		obs_data_t *array_obj = obs_data_create();

		obs_source_t *source = obs_weak_source_get_source(s.source);
		obs_source_t *sceneSource = obs_weak_source_get_source(s.scene);
		obs_source_t *transition =
			obs_weak_source_get_source(s.transition);
		if ((s.usePreviousScene || sceneSource) && source &&
		    transition) {
			const char *sourceName = obs_source_get_name(source);
			const char *sceneName =
				obs_source_get_name(sceneSource);
			const char *transitionName =
				obs_source_get_name(transition);
			obs_data_set_string(array_obj, "source", sourceName);
			obs_data_set_string(array_obj, "scene",
					    s.usePreviousScene
						    ? PREVIOUS_SCENE_NAME
						    : sceneName);
			obs_data_set_string(array_obj, "transition",
					    transitionName);
			obs_data_set_string(array_obj, "filePath",
					    s.filePath.c_str());
			obs_data_set_int(array_obj, "matchType", s.matchType);
			obs_data_set_int(array_obj, "similarity", s.similarity);
			obs_data_array_push_back(imgCmpArray, array_obj);
		}
		obs_source_release(source);
		obs_source_release(sceneSource);
		obs_source_release(transition);

		obs_data_release(array_obj);
	}
	obs_data_set_array(obj, "imgCmpSwitches", imgCmpArray);
	obs_data_array_release(imgCmpArray);
}

void SwitcherData::loadImgCmpSettings(obs_data_t *obj)
{
	obs_data_array_t *imgCmpArray =
		obs_data_get_array(obj, "imgCmpSwitches");
	switcher->imgCmpSwitches.clear();
	size_t count = obs_data_array_count(imgCmpArray);

	for (size_t i = 0; i < count; i++) {
		obs_data_t *array_obj = obs_data_array_item(imgCmpArray, i);

		const char *source = obs_data_get_string(array_obj, "source");
		const char *scene = obs_data_get_string(array_obj, "scene");
		const char *transition =
			obs_data_get_string(array_obj, "transition");
		const char *filePath =
			obs_data_get_string(array_obj, "filePath");
		imgCmpMatchType matchType = (imgCmpMatchType)obs_data_get_int(
			array_obj, "matchType");
		int similarity = obs_data_get_int(array_obj, "similarity");

		std::string imgCmpStr =
			MakeImgCmpSwitchName(source, matchType, similarity,
					     filePath, scene, transition)
				.toUtf8()
				.constData();

		switcher->imgCmpSwitches.emplace_back(
			GetWeakSourceByName(scene),
			GetWeakTransitionByName(transition),
			GetWeakSourceByName(source), matchType, similarity,
			filePath, (strcmp(scene, PREVIOUS_SCENE_NAME) == 0),
			imgCmpStr);

		addHelperFilter(source);

		obs_data_release(array_obj);
	}
	obs_data_array_release(imgCmpArray);
}

__int64 SwitcherData::calcImageHash(obs_source_frame *src)
{
	if (!src) {
		return 0;
	}

	__int64 hash = 0;
	//IplImage *res = 0, *gray = 0;
	//res = cvCreateImage(cvSize(9, 8), src->depth, src->nChannels);
	//gray = cvCreateImage(cvSize(9, 8), IPL_DEPTH_8U, 1);
	//cvResize(src, res);
	//cvCvtColor(res, gray, COLOR_BGR2GRAY);
	//
	//int i = 0;
	//cout << gray->height;
	//for (int y = 0; y<gray->height; y++) {
	//	uchar* ptr = (uchar*)(gray->imageData + y * gray->widthStep);
	//	for (int x = 0; x<gray->width - 1; x++) {
	//		if (ptr[x + 1] > ptr[x]) {
	//			hash |= (__int64)1 << i;
	//		}
	//		i++;
	//	}
	//}
	//printf("[i] hash: %I64X \n", hash, "\n");
	//std::cout << endl;
	//cvReleaseImage(&res);
	//cvReleaseImage(&gray);
	return hash;
}

void matchesExactly(gs_texture *frame1, gs_texture *frame2, bool &match)
{
	obs_enter_graphics();

	uint8_t *ptr1, *ptr2;
	uint32_t linesize1, linesize2;
	gs_texture_map(frame1, &ptr1, &linesize1);
	gs_texture_map(frame2, &ptr2, &linesize2);
	//params seem incorrect
	gs_texture_unmap(frame1);
	gs_texture_unmap(frame1);
	obs_leave_graphics();
	//gs_texture_get_obj
}

void isSimilar(gs_texture *frame1, gs_texture *frame2, int &similarity,
	       bool &match)
{
	;
}

void SwitcherData::checkImageSwitch(bool &match, OBSWeakSource &scene,
				    OBSWeakSource &transition)
{
	if (!helperFilterM.try_lock())
		return;
	for (auto &s : imgCmpSwitches) {
		auto it = weakSoruceToFilterSource.find(s.source);
		if (it == weakSoruceToFilterSource.end())
			break;
		auto it2 = filterSourceToFrames.find(it->second);
		if (it2 == filterSourceToFrames.end())
			break;
		frameData *data = it2->second;
		if (!data->m.try_lock_for(
			    std::chrono::milliseconds(switcher->interval)))
			break;
		;
		if (s.matchType == IMG_CMP_EXACT_MATCH)
			matchesExactly(data->tex, s.tex, match);
		else if (s.matchType == IMG_CMP_SIMILAR)
			isSimilar(data->tex, s.tex, s.similarity, match);
		data->m.unlock();
		if (match) {
			scene = (s.usePreviousScene) ? previousScene : s.scene;
			transition = s.transition;
			break;
		}
	}
	helperFilterM.unlock();
	//obs_source_t *source = obs_get_source_by_name("Window Capture");

	////does not work with display / window capture - so try using something else
	//obs_source_frame *curFrame = obs_source_get_frame(source);

	////not sure how to use or if even useful
	////EXPORT video_t *obs_output_video(const obs_output_t *output);

	//auto sourceEnum = [](void *data, obs_source_t *source) -> bool /* -- */
	//{
	//	const char *name = obs_source_get_name(source);
	//	obs_source_frame *curFrame = obs_source_get_frame(source);
	//	obs_source_release_frame(source, curFrame);
	//	//obs_source_release(source);
	//	return true;
	//};

	//obs_enum_sources(sourceEnum, nullptr);
	//return;

	//if (!curFrame) {
	//	std::string test = obs_source_get_id(source);
	//	obs_source_release_frame(source, curFrame);
	//	obs_source_release(source);
	//	return;
	//}

	//gs_texture_t *test = obs_get_main_texture();

	////struct SwsContext *resize;
	////resize = sws_getContext(frame->width, frame->height, frame->format, width2, height2, PIX_FMT_RGB24, SWS_BICUBIC, NULL, NULL, NULL);
	////
	////AVFrame* frame1 = av_frame_alloc(); // this is your original frame
	////AVFrame* frame2 = av_frame_alloc();
	////int num_bytes = avpicture_get_size(AV_PIX_FMT_RGB24, width2, height2);
	////uint8_t* frame2_buffer = (uint8_t *)av_malloc(num_bytes * sizeof(uint8_t));
	////avpicture_fill((AVPicture*)frame2, frame2_buffer, AV_PIX_FMT_RGB24, width2, height2);

	//video_scaler_t *scaler;

	//const video_scale_info src_info{
	//	curFrame->format, curFrame->width, curFrame->height,
	//	curFrame->full_range ? VIDEO_RANGE_FULL : VIDEO_RANGE_PARTIAL,
	//	// what even is this?
	//	VIDEO_CS_DEFAULT};

	//const video_scale_info dst_info{
	//	VIDEO_FORMAT_YUVA, SCALE_TARGET_WIDTH, SCALE_TARGET_HEIGHT,
	//	curFrame->full_range ? VIDEO_RANGE_FULL : VIDEO_RANGE_PARTIAL,
	//	// what even is this?
	//	VIDEO_CS_DEFAULT};

	//video_scaler_create(&scaler, &src_info, &dst_info, VIDEO_SCALE_DEFAULT);

	//obs_source_frame scaledFrame;

	//video_scaler_scale(scaler, (uint8_t **)&scaledFrame.data,
	//		   (const uint32_t *)&scaledFrame.linesize,
	//		   curFrame->data, curFrame->linesize);

	//video_scaler_destroy(scaler);
	//obs_source_release_frame(source, curFrame);
	//obs_source_release(source);

	/** Releases the current async video frame */
	/*if (timeSwitches.size() == 0)
		return;

	QTime now = QTime::currentTime();

	for (TimeSwitch &s : timeSwitches) {

		QTime validSwitchTimeWindow = s.time.addMSecs(interval);

		match = s.time <= now && now <= validSwitchTimeWindow;
		if (!match &&
			validSwitchTimeWindow.msecsSinceStartOfDay() < interval) {
			// check for overflow
			match = now >= s.time || now <= validSwitchTimeWindow;
		}

		if (match) {
			scene = (s.usePreviousScene) ? previousScene : s.scene;
			transition = s.transition;
			match = true;

			if (verbose)
				blog(LOG_INFO,
					"Advanced Scene Switcher time match");

			break;
		}
	}*/
}

void SceneSwitcher::setupImageTab()
{
	populateSceneSelection(ui->imgCmpScenes, true);
	populateTransitionSelection(ui->imgCmpTransitions);

	auto sourceEnumVideoOut = [](void *data, obs_source_t *source) -> bool {
		QComboBox *combo = reinterpret_cast<QComboBox *>(data);
		uint32_t flags = obs_source_get_output_flags(source);

		if ((flags & OBS_SOURCE_VIDEO) != 0) {
			const char *name = obs_source_get_name(source);
			combo->addItem(name);
		}

		return true;
	};

	obs_enum_sources(sourceEnumVideoOut, ui->imgCmpSources);

	ui->imgCmpMatchType->addItem("exactly matches");
	ui->imgCmpMatchType->addItem("is similar to");

	for (auto &s : switcher->imgCmpSwitches) {
		std::string sceneName = (s.usePreviousScene)
						? PREVIOUS_SCENE_NAME
						: GetWeakSourceName(s.scene);
		std::string transitionName = GetWeakSourceName(s.transition);
		std::string sourceName = GetWeakSourceName(s.source);

		QString listText = MakeImgCmpSwitchName(
			sourceName.c_str(), s.matchType, s.similarity,
			s.filePath.c_str(), sceneName.c_str(),
			transitionName.c_str());
		QListWidgetItem *item =
			new QListWidgetItem(listText, ui->imgCmpSwitches);
		item->setData(Qt::UserRole, listText);
	}
}
