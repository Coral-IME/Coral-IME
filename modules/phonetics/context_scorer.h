/*
* @author: xuxiang
* @date: 2021-6-10 11:30:25
* @brief: 二元语言模型。按独立 magic 选择旧静态图或表面词概率模型。
*/
#pragma once
#ifndef CORAL_MODULES_PHONETICS_CONTEXT_SCORER_H_INCLUDED
#define CORAL_MODULES_PHONETICS_CONTEXT_SCORER_H_INCLUDED

#include <thirdparty/parallel_hashmap/phmap.h>
#include <marisa.h>
#include "bigram_table.h"
#include "../storage/phrase_archive_view.hpp"
#include "verified_resource.h"
#include "coral/language_model/surface_model.h"
#if CORAL_ENABLE_CHARACTER_MODEL
#include "coral/language_model/character_model.h"
#endif
#include <cmath>

namespace coral::phonetics {
class ContextScorer
{
public:
	ContextScorer();
	~ContextScorer();
	bool build_g_bin_only(const filesystem::path& modelBigramFile,
		const filesystem::path& unigramSourceFile);
	// 点查兼容包装（拼音纠错等点查场景）：
	// 从 start 态模拟一次组合转移，命中返回二元 log 概率，未命中返回 backoff 平滑结果，
	// 前词不在模型中返回 LOG_INFINITE_SIMAL（与旧 wsfa 语义一致）。
	coral::phonetics::Score get_bigram(const uint32_t prevId, const uint32_t successor_id) const;
	bool load_g_bin_only(const coral::phonetics::ResourceLocation& gbinFile,
		const string& versionId);

	// 组合解码接口：直接暴露 languagemodel.coral 模型视图
	const coral::phonetics::BigramTable* gbin() const {
        return !surface_model_.valid() && gbin_view_.valid() ? &gbin_view_.model() : nullptr;
    }
    const coral::language_model::SurfaceModel* surface_model() const {
        return surface_model_.valid() ? &surface_model_ : nullptr;
    }
    bool load_surface_model(const filesystem::path& model, const filesystem::path& lexicon,
        std::string& error);
#if CORAL_ENABLE_CHARACTER_MODEL
    void configure_character_model(const std::filesystem::path& default_path, bool enabled = true);
    const coral::language_model::CharacterModel* character_model() const {
        return character_model_.get();
    }
    std::shared_ptr<const coral::language_model::CharacterModel> shared_character_model() const { return character_model_; }
    double character_weight() const { return character_weight_; }
    bool load_character_model(const filesystem::path& path, double weight, std::string& error) {
        if (!std::isfinite(weight) || weight < 0 || weight > 8) return false;
        auto model = std::make_shared<coral::language_model::CharacterModel>();
        if (!model->load_file(path, error)) return false;
        character_model_ = std::move(model);
        character_weight_ = weight; return true;
    }
#endif

private:
	coral::storage::BigramMappedView gbin_view_;
    coral::language_model::SurfaceModel surface_model_;
#if CORAL_ENABLE_CHARACTER_MODEL
    std::shared_ptr<const coral::language_model::CharacterModel> character_model_;
    double character_weight_ = 0;
#endif
};
} // 命名空间 coral::phonetics 结束（批次 2）


#endif // LANGUAGEMODEL_H
