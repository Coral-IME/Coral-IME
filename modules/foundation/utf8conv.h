/* ------------------------------------------------------------------
 * Copyright (C) 1998-2009 PacketVideo
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 * -------------------------------------------------------------------
 */
/*********************************************************************************

                             UTF-8 Bit Distribution

UTF-16                                  1st Byte 2nd Byte 3rd Byte 4th Byte
-------- -------- -------- --------     -------- -------- -------- --------
00000000 0xxxxxxx                       0xxxxxxx
00000yyy yyxxxxxx                       110yyyyy 10xxxxxx
zzzzyyyy yyxxxxxx                       1110zzzz 10yyyyyy 10xxxxxx
110110ww wwzzzzyy 110111yy yyxxxxxx     11110uuu 10uuzzzz 10yyyyyy 10xxxxxx

NOTE:
 uuuuu = wwww+1 (to account for addition of 0x10000 as in Section 3.7, Surrogates)

**********************************************************************************/
#ifndef _UTF8CONV_H
#define _UTF8CONV_H

inline std::string wstrToUtf8(const std::wstring& wstr)
{
	std::string result;
	result.reserve(wstr.size() * 4);
	constexpr uint32_t REPLACEMENT = 0xFFFDu;
	using wchar_unsigned_t = typename std::make_unsigned<wchar_t>::type;

	for (size_t i = 0; i < wstr.size(); ++i)
	{
		// safe unsigned view of code unit
		uint32_t cu = static_cast<uint32_t>(static_cast<wchar_unsigned_t>(wstr[i]));
		uint32_t cp = cu;

		if (sizeof(wchar_t) == 2)
		{
			// UTF-16: handle surrogate pairs
			if (cu >= 0xD800 && cu <= 0xDBFF)
			{
				// high surrogate
				if (i + 1 < wstr.size())
				{
					uint32_t low = static_cast<uint32_t>(static_cast<wchar_unsigned_t>(wstr[i + 1]));
					if (low >= 0xDC00 && low <= 0xDFFF)
					{
						cp = ((cu - 0xD800) << 10) + (low - 0xDC00) + 0x10000;
						++i; // consumed low surrogate
					}
					else
					{
						// invalid pair
						cp = REPLACEMENT;
					}
				}
				else
				{
					// truncated high surrogate
					cp = REPLACEMENT;
				}
			}
			else if (cu >= 0xDC00 && cu <= 0xDFFF)
			{
				// isolated low surrogate
				cp = REPLACEMENT;
			}
		}
		else
		{
			// wchar_t >= 4 (UTF-32): validate code point
			if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu))
				cp = REPLACEMENT;
		}

		if (cp <= 0x7F)
		{
			result.push_back(static_cast<char>(cp));
		}
		else if (cp <= 0x7FF)
		{
			result.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
			result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		}
		else if (cp <= 0xFFFF)
		{
			result.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
			result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
			result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		}
		else
		{
			result.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
			result.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
			result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
			result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		}
	}

	return result;
}

inline std::wstring utf8ToWstr(const std::string& str)
{
	std::wstring result;
	result.reserve(str.size());

	const unsigned char* bytes = reinterpret_cast<const unsigned char*>(str.data());
	const size_t n = str.size();
	size_t i = 0;
	constexpr wchar_t REPLACEMENT = static_cast<wchar_t>(0xFFFD);

	while (i < n)
	{
		unsigned char b0 = bytes[i];
		if (b0 < 0x80)
		{
			// ASCII
			result.push_back(static_cast<wchar_t>(b0));
			++i;
			continue;
		}

		// determine sequence length and initial bits
		uint32_t cp = 0;
		size_t needed = 0;
		if ((b0 & 0xE0) == 0xC0) { cp = b0 & 0x1F; needed = 1; }
		else if ((b0 & 0xF0) == 0xE0) { cp = b0 & 0x0F; needed = 2; }
		else if ((b0 & 0xF8) == 0xF0) { cp = b0 & 0x07; needed = 3; }
		else
		{
			// invalid leading byte (0x80..0xBF are continuation, 0xF8..0xFF invalid)
			result.push_back(REPLACEMENT);
			++i;
			continue;
		}

		// Check length fits in buffer
		if (i + needed >= n)
		{
			// incomplete sequence at end
			result.push_back(REPLACEMENT);
			++i; // skip the lead to avoid infinite loop; alternative: break
			continue;
		}

		// accumulate continuation bytes
		bool bad = false;
		for (size_t k = 1; k <= needed; ++k)
		{
			unsigned char bx = bytes[i + k];
			if ((bx & 0xC0) != 0x80)
			{
				// invalid continuation
				bad = true;
				break;
			}
			cp = (cp << 6) | (bx & 0x3F);
		}

		if (bad)
		{
			result.push_back(REPLACEMENT);
			++i; // skip one byte (lead) and retry to resynchronize
			continue;
		}

		// Validate against overlong encodings and ranges
		// Minimal values for each length:
		// 1-byte: 0x00..0x7F  (handled above)
		// 2-byte: >= 0x80
		// 3-byte: >= 0x800
		// 4-byte: >= 0x10000
		bool overlong = (needed == 1 && cp <= 0x7F) ||
						(needed == 2 && cp <= 0x7FF) ||
						(needed == 3 && cp <= 0xFFFF);
		if (overlong || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
		{
			result.push_back(REPLACEMENT);
			i += needed + 1;
			continue;
		}
		// Produce output
		if (sizeof(wchar_t) >= 4)
		{
			// wchar_t can hold full codepoints directly (common on POSIX)
			result.push_back(static_cast<wchar_t>(cp));
		}
		else
		{
			// wchar_t is 2 bytes (Windows): emit UTF-16 code units,
			// use surrogate pair for codepoints > 0xFFFF
			if (cp <= 0xFFFF)
			{
				result.push_back(static_cast<wchar_t>(cp));
			}
			else
			{
				cp -= 0x10000;
				wchar_t high = static_cast<wchar_t>(0xD800 + ((cp >> 10) & 0x3FF));
				wchar_t low  = static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
				result.push_back(high);
				result.push_back(low);
			}
		}

		i += needed + 1;
	}

	return result;
}
#endif