#pragma once
#ifndef CORAL_MODULES_STORAGE_MODEL_VIEW_H_INCLUDED
#define CORAL_MODULES_STORAGE_MODEL_VIEW_H_INCLUDED

#include "verified_resource.h"
#include "../runtime/resource_access.h"

namespace coral::storage {

// 借出的模型元数据必须在替换其底层资源前重置。
class ModelView {
public:
    virtual ~ModelView() = default;
    ModelView() = default;
    ModelView(const ModelView&) = delete;
    ModelView& operator=(const ModelView&) = delete;

    virtual bool map(const std::wstring& file, const std::wstring& name, const std::string& checksum) {
        reset();
        if (!resource_.load(name, std::filesystem::path(file), checksum, runtime::resource_access()))
            return false;
        view_ = resource_.data();
        size_ = resource_.size();
        return true;
    }

    virtual std::size_t size() const { return size_; }
    const char* data() const { return static_cast<const char*>(view_); }

protected:
    virtual void reset() {
        view_ = nullptr;
        size_ = 0;
        resource_.reset();
    }
    const void* view_ = nullptr;
    std::size_t size_ = 0;

private:
    VerifiedResource resource_;
};

} // 命名空间 coral::storage 结束
#endif
