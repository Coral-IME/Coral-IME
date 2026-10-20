#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_UI_SKINCROPDIALOG_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_UI_SKINCROPDIALOG_H_INCLUDED

#include "pch.h"
#include "settingswindow.h"
#include "candidate_theme.h"

#include <memory>

/*
 * 候选窗背景图裁剪对话框（模态）。裁剪框固定为候选窗完全展开时的宽高比，
 * 框内虚线以上为收起时显示的部分，以下为展开后的延伸区域。
 * 图片透明度实时预览；确认后返回裁剪图片并写回 imageOpacity，取消不修改。
 */
class SkinCropDialog
{
public:
	static std::shared_ptr<coral::win32::SkinImage> run(HWND owner,
		const coral::win32::SkinImage& image, const SettingsSkin& skin, int& imageOpacity,
		std::uint32_t background, std::uint32_t veil);
};

#endif // SKINCROPDIALOG_H
