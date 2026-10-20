/*
 * @brief: mainlexicon.coral / languagemodel.coral 离线生成工具
 * 用法:
 *   coral_lexicon_builder --csv <coral_lexicon.csv> <coral_languagemodel.csv> <out_dir> [eng_lexicon.csv]  直接从 coral_lexicon.csv + coral_languagemodel.csv 生成 <dir>/mainlexicon.coral 与 <dir>/languagemodel.coral
 *   coral_lexicon_builder <generallexicon_dir>                     在 <dir>/mainlexicon.coral 生成（输入 <dir>/primary.marisa + primary.dat）
 *   coral_lexicon_builder <marisa_file> <dat_file> <out>           显式指定输入输出
 * --csv 模式生成后内嵌 bindData 自校验；
 * marisa 模式生成后内嵌全量校验（键/条目/字段与 marisa+dat 逐一比对）。
 */
#include "lexicon_table.h"
#include <iostream>

int main(int argc, char** argv)
{
	if ((argc == 5 || argc == 6) && std::string(argv[1]) == "--csv")
	{
		const std::filesystem::path lsortPath = argv[2];
		const std::filesystem::path m2Path = argv[3];
		const std::filesystem::path outDir = argv[4];
		std::error_code ec;
		std::filesystem::create_directories(outDir, ec);
		if (!build_resource_tables(lsortPath, m2Path, outDir / "mainlexicon.coral", outDir / "languagemodel.coral",
			argc == 6 ? std::filesystem::path(argv[5]) : std::filesystem::path{}))
			return 1;
		return 0;
	}

	std::filesystem::path marisaPath, datPath, outPath;
	if (argc == 2)
	{
		const std::filesystem::path dir = argv[1];
		marisaPath = dir / "primary.marisa";
		datPath = dir / "primary.dat";
		outPath = dir / "mainlexicon.coral";
	}
	else if (argc == 4)
	{
		marisaPath = argv[1];
		datPath = argv[2];
		outPath = argv[3];
	}
	else
	{
		std::cerr << "usage: coral_lexicon_builder --csv <coral_lexicon.csv> <coral_languagemodel.csv> <out_dir> [eng_lexicon.csv]\n"
					 "       coral_lexicon_builder <generallexicon_dir>\n"
					 "       coral_lexicon_builder <primary.marisa> <primary.dat> <mainlexicon.coral>\n";
		return 2;
	}

	if (!import_legacy_lexicon_index(marisaPath, datPath, outPath))
		return 1;
	return 0;
}
