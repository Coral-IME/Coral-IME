#pragma once
#include "shape_lexicon_format.h"
#include "../../storage/model_view.h"

namespace coral::assist {
class ShapeLexiconView : public coral::storage::ModelView {
public:
    bool map(const std::wstring& file, const std::wstring& name, const std::string& identifier) override {
        data_.bind(nullptr, 0);
        return coral::storage::ModelView::map(file, name, identifier) && data_.bind(view_, size_);
    }
    std::size_t size() const override { return data_.size(); }
    coral::foundation::SliceView<const coral::storage::ShapePhraseRecord> operator[](std::size_t i) const { return data_[i]; }
    coral::storage::ShapePhraseView get_phrase_view(const coral::storage::ShapePhraseRecord& info) const { return data_.get_phrase_view(info); }
private:
    coral::assist::ShapeDataView data_;
};
} // 命名空间 coral::assist 结束（批次 2 辅助输入）

