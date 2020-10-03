#pragma once
#include <string>
#include "utility.hpp"
#include "switch-generic.hpp"
#include "helper-filter.hpp"

constexpr auto img_func = 9;
constexpr auto default_priority_9 = img_func;

typedef enum {
	IMG_CMP_EXACT_MATCH,
	IMG_CMP_SIMILAR,
} imgCmpMatchType;

struct ImgCmpSwitch : virtual SceneSwitcherEntry {
	OBSWeakSource source;
	imgCmpMatchType matchType;
	int similarity;
	std::string filePath;
	gs_texture *tex = nullptr;
	std::string imgCmpSwitchStr;

	const char *getType() { return "image"; }

	bool valid()
	{

		return (usePreviousScene || WeakSourceValid(scene)) &&
		       WeakSourceValid(source) && WeakSourceValid(transition);
	}

	inline ImgCmpSwitch(OBSWeakSource scene_, OBSWeakSource transition_,
			    OBSWeakSource source_, imgCmpMatchType matchType_,
			    int similarity_, std::string filePath_,
			    bool usePreviousScene_,
			    std::string imgCmpSwitchStr_)
		: SceneSwitcherEntry(scene_, transition_, usePreviousScene_),
		  source(source_),
		  matchType(matchType_),
		  similarity(similarity_),
		  filePath(filePath_),
		  imgCmpSwitchStr(imgCmpSwitchStr_)
	{
		obs_enter_graphics();
		tex = gs_texture_create_from_file(filePath_.c_str());
		obs_leave_graphics();
	}

	ImgCmpSwitch(const ImgCmpSwitch &other)
		: SceneSwitcherEntry(other.scene, other.transition,
				     other.usePreviousScene),
		  source(other.source),
		  matchType(other.matchType),
		  similarity(other.similarity),
		  filePath(other.filePath),
		  imgCmpSwitchStr(other.imgCmpSwitchStr)
	{
		obs_enter_graphics();
		tex = gs_texture_create_from_file(other.filePath.c_str());
		obs_leave_graphics();
	}

	ImgCmpSwitch(ImgCmpSwitch &&other)
		: SceneSwitcherEntry(other.scene, other.transition,
				     other.usePreviousScene),
		  source(other.source),
		  matchType(other.matchType),
		  similarity(other.similarity),
		  filePath(other.filePath),
		  imgCmpSwitchStr(other.imgCmpSwitchStr),
		  tex(other.tex)
	{
		other.tex = nullptr;
	}

	inline ~ImgCmpSwitch()
	{
		obs_enter_graphics();
		gs_texture_destroy(tex);
		tex = nullptr;
		obs_leave_graphics();
	}

	ImgCmpSwitch &operator=(const ImgCmpSwitch &other)
	{
		return *this = ImgCmpSwitch(other);
	}

	ImgCmpSwitch &operator=(ImgCmpSwitch &&other) noexcept
	{
		if (this == &other) {
			return *this;
		}
		gs_texture_destroy(tex);
		tex = other.tex;
		other.tex = nullptr;
		return *this;
	}
};

static inline QString MakeImgCmpSwitchName(const QString &source,
					   imgCmpMatchType matchType,
					   int similarity, const QString &file,
					   const QString &scene,
					   const QString &transition);
