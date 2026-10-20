#pragma once
#ifndef CORAL_MODULES_PHONETICS_INVALID_INITIALS_H_INCLUDED
#define CORAL_MODULES_PHONETICS_INVALID_INITIALS_H_INCLUDED

#include <string>
#include <unordered_set>


// 使用 constexpr 在编译时构建哈希表，运行时性能最优
constexpr std::string_view invalid_short_pinyins_array[] = {
	"wi",  "we",  "fe",	 "cva", "cvb", "cvc", "cvd", "cve", "cvf", "cvg", "cvh", "cvi", "cvj", "cvk", "cvl", "cvm", "cvn", "cvo", "cvp", "cvq", "cvr",
	"cvs", "cvt", "cvu", "cvv", "cvw", "cvx", "cvy", "cvz", "doa", "dob", "dod", "doe", "dof", "doh", "doi", "doj", "dok", "dol", "dom", "don", "doo",
	"dop", "doq", "dor", "dov", "dow", "dox", "doy", "doz", "dva", "dvb", "dvc", "dve", "dvf", "dvg", "dvh", "dvi", "dvj", "dvk", "dvl", "dvm", "dvn",
	"dvo", "dvp", "dvq", "dvr", "dvs", "dvt", "dvu", "dvv", "dvw", "dvx", "dvy", "dvz", "fea", "feb", "fec", "feg", "feh", "fej", "fek", "fel", "fem",
	"feo", "fep", "feq", "fes", "fet", "feu", "fev", "fex", "fey", "fez", "fia", "fic", "fid", "fie", "fif", "fih", "fii", "fij", "fik", "fil", "fim",
	"fio", "fip", "fiq", "fis", "fiu", "fiv", "fiw", "fiy", "fiz", "jaa", "jac", "jad", "jae", "jaf", "jag", "jah", "jaj", "jak", "jal", "jap", "jaq",
	"jas", "jat", "jau", "jav", "jax", "jaz", "jea", "jeb", "jec", "jed", "jee", "jef", "jeg", "jeh", "jej", "jek", "jel", "jem", "jeo", "jep", "jeq",
	"jes", "jeu", "jev", "jex", "jey", "jez", "joa", "joc", "jod", "jof", "joh", "joi", "joj", "jok", "jol", "jom", "jon", "joo", "jop", "joq", "jor",
	"jos", "jot", "jov", "jow", "jox", "joz", "jua", "jui", "juo", "juu", "juv", "noa", "noc", "noe", "nof", "nog", "noh", "noi", "noj", "nok", "nol",
	"nom", "noo", "nop", "noq", "nos", "nov", "nox", "noy", "noz", "qaa", "qab", "qac", "qad", "qae", "qaf", "qag", "qah", "qaj", "qak", "qal", "qam",
	"qap", "qaq", "qar", "qas", "qat", "qau", "qav", "qaw", "qax", "qay", "qaz", "qea", "qeb", "qec", "qed", "qee", "qef", "qeg", "qeh", "qej", "qek",
	"qel", "qem", "qeo", "qep", "qeq", "qes", "qet", "qeu", "qev", "qew", "qex", "qey", "qez", "qoa", "qob", "qoc", "qod", "qoe", "qof", "qog", "qoh",
	"qoi", "qoj", "qok", "qol", "qom", "qon", "qoo", "qop", "qoq", "qor", "qos", "qot", "qov", "qow", "qox", "qoy", "qoz", "qua", "qui", "quo", "quu",
	"quv", "rva", "rvb", "rvc", "rvd", "rve", "rvf", "rvg", "rvh", "rvi", "rvj", "rvk", "rvl", "rvm", "rvn", "rvo", "rvp", "rvq", "rvr", "rvs", "rvt",
	"rvu", "rvv", "rvw", "rvx", "rvy", "rvz", "sva", "svb", "svc", "svd", "sve", "svf", "svg", "svh", "svi", "svj", "svk", "svl", "svm", "svn", "svo",
	"svp", "svq", "svr", "svs", "svt", "svu", "svv", "svw", "svx", "svy", "svz", "toa", "tob", "toc", "tod", "toe", "tof", "tog", "toh", "toi", "toj",
	"tok", "tol", "tos", "tov", "tox", "toy", "toz", "tva", "tvb", "tvc", "tvd", "tve", "tvf", "tvg", "tvh", "tvi", "tvj", "tvk", "tvl", "tvm", "tvn",
	"tvo", "tvp", "tvq", "tvr", "tvs", "tvt", "tvu", "tvv", "tvw", "tvx", "tvy", "tvz", "wea", "wec", "weh", "wej", "wek", "wel", "wem", "weo", "wep",
	"weq", "wes", "weu", "wev", "wew", "wex", "wey", "wez", "wia", "wib", "wic", "wid", "wie", "wif", "wih", "wii", "wij", "wik", "wil", "wim", "wio",
	"wip", "wiq", "wir", "wis", "wit", "wiu", "wiv", "wiw", "wix", "wiy", "wiz", "wva", "wvb", "wvc", "wvd", "wve", "wvf", "wvg", "wvh", "wvi", "wvj",
	"wvk", "wvl", "wvm", "wvn", "wvo", "wvp", "wvq", "wvr", "wvs", "wvt", "wvu", "wvv", "wvw", "wvx", "wvy", "wvz", "xaa", "xab", "xac", "xad", "xae",
	"xaf", "xag", "xah", "xaj", "xak", "xal", "xam", "xap", "xaq", "xar", "xas", "xat", "xau", "xav", "xaw", "xax", "xay", "xaz", "xea", "xeb", "xec",
	"xed", "xee", "xef", "xeg", "xeh", "xej", "xek", "xel", "xem", "xeo", "xep", "xeq", "xes", "xet", "xeu", "xev", "xew", "xex", "xey", "xez", "xoa",
	"xob", "xoc", "xod", "xoe", "xof", "xog", "xoh", "xoi", "xoj", "xok", "xol", "xom", "xon", "xoo", "xop", "xoq", "xor", "xos", "xot", "xov", "xow",
	"xox", "xoy", "xoz", "xua", "xui", "xuo", "xuu", "xuv", "yau", "yav", "yeo", "yeu", "yev", "yoa", "yob", "yoc", "yod", "yoe", "yof", "yog", "yoh",
	"yoi", "yoj", "yok", "yol", "yom", "yon", "yoo", "yop", "yoq", "yor", "yos", "yot", "yov", "yow", "yox", "yoy", "yoz", "yua", "yui", "yuo", "yur",
	"yut", "yuu", "yuv", "yva", "yvb", "yvc", "yvd", "yve", "yvf", "yvg", "yvh", "yvi", "yvj", "yvk", "yvl", "yvm", "yvn", "yvo", "yvp", "yvq", "yvr",
	"yvs", "yvt", "yvu", "yvv", "yvw", "yvx", "yvy", "yvz", "zva", "zvb", "zvc", "zvd", "zve", "zvf", "zvg", "zvh", "zvi", "zvj", "zvk", "zvl", "zvm",
	"zvn", "zvo", "zvp", "zvq", "zvr", "zvs", "zvt", "zvu", "zvv", "zvw", "zvx", "zvy", "zvz", "hau"};

// 使用函数静态变量，避免资源泄露
inline const std::unordered_set<std::string> &getInvalidShortPinyins()
{
	// 进程退出（DLL_PROCESS_DETACH / CRT atexit）析构该函数静态 set 时曾发生堆内存野指针崩溃（0xC0000005，读地址 0x2700000020）。
	// 该 set 为只读固定数据，改用堆分配且永不析构，由 OS 在进程退出时回收，彻底规避静态对象析构崩溃。
	static auto *set = new std::unordered_set<std::string>(std::begin(invalid_short_pinyins_array), std::end(invalid_short_pinyins_array));
	static bool initialized = false;
	if (!initialized)
	{
		set->rehash(set->size() * 2);
		initialized = true;
	}
	return *set;
}

// 便于使用的全局引用
inline const std::unordered_set<std::string> &invalidShortPinyins = getInvalidShortPinyins();

#endif // INVALIDSHORTPINYIN_H
