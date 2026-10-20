#include "clipboard_processor.h"
#include "clipboard_monitor.h"
ClipboardProcessor::ClipboardProcessor()
{
	get_k_clipboard_monitor_instance().start();
}
ClipboardProcessor::~ClipboardProcessor()
{
	get_k_clipboard_monitor_instance().stop();
}
bool ClipboardProcessor::process(const std::string& input, std::vector<coral::phonetics::CandidateResult>& sents)
{
	if (input != "vv")
		return false;

	ClipboardMonitor& monitor = get_k_clipboard_monitor_instance();
	const std::vector<std::string>& records = monitor.get_recently_data();
	for (const std::string& str : records)
	{
		coral::phonetics::CandidateResult ansSln;
		ansSln.sentence = str;
		ansSln.pinyin.push_back(input);
		sents.push_back(std::move(ansSln));
	}


	return true;
}
