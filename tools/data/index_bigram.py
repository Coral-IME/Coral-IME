"""
@author: s_yanxiaoren
@description: 
根据主词典词语id，将m2由词语+词语->概率，转换为词语id+词语id->概率
还会输出一些特殊的项到子文件中
"""
import sys
sys.dont_write_bytecode=True
import csv
import os
import hashlib
import json
import sys
from collections import defaultdict
from datetime import datetime
from terminal_words import TERMINAL_WORDS

# --no-emoji 参数：构建系统调用时传入，禁用 emoji 输出
_use_emoji = "--no-emoji" not in sys.argv
ICON_OK = "✅" if _use_emoji else "[OK]"
ICON_WARN = "⚠️" if _use_emoji else "[WARN]"

def normalize_pinyin(pinyin):
    """规范化拼音字段，仅处理首尾空白，保留原始大小写。"""
    if pinyin is None:
        return ''
    return pinyin.strip()


def load_word_indexes(index_file):
    word_to_index = {}
    exact_word_to_index = {}
    word_to_candidates = defaultdict(list)
    seen_candidates = set()
    with open(index_file, 'r', encoding='utf-8-sig') as f:
        next(f)  # 跳过第一行
        next(f)  # 跳过第二行
        reader = csv.DictReader(f)
        for row in reader:
            word = row['word']
            index = int(row['index'])
            full_pinyin = normalize_pinyin(row.get('fullPinyin', ''))
            if word not in word_to_index:
                word_to_index[word] = index
            if word and any(ch.isalpha() for ch in word) and all(0x21 <= ord(ch) <= 0x7e for ch in word):
                word_to_index.setdefault(word.lower(), index)
            if full_pinyin:
                exact_word_to_index[(word, full_pinyin)] = index
            candidate_key = (word, index, full_pinyin)
            if candidate_key not in seen_candidates:
                word_to_candidates[word].append((index, full_pinyin))
                seen_candidates.add(candidate_key)
    return word_to_index, exact_word_to_index, word_to_candidates

def write_with_count_header(temp_file, target_file, header_name):
    # 读取临时文件内容，统计条目数（减去表头）
    with open(temp_file, 'r', encoding='utf-8') as f:
        lines = f.readlines()
    count = len(lines) - 1  # 减去表头行

    # 重新写入目标文件，第一行为条目数，第二行为表头，后面写内容
    with open(target_file, 'w', encoding='utf-8', newline='') as f:
        f.write(f"{header_name}: {count}\n")
        f.writelines(lines)

    os.remove(temp_file)  # 删除临时文件

def validate_m2_row(row, row_number):
    """验证M2文件中的单行数据，检查必需列是否有值"""
    required_fields = ['prev_word', 'next_word', 'prob']
    empty_fields = []
    
    for field in required_fields:
        if field not in row or not row[field] or row[field].strip() == '':
            empty_fields.append(field)
    
    if empty_fields:
        raise ValueError(f"M2文件第{row_number}行存在空列: {', '.join(empty_fields)}")
    
    # 验证prob字段是否为有效的数字
    try:
        float(row['prob'])
    except ValueError:
        raise ValueError(f"M2文件第{row_number}行的prob列不是有效数字: {row['prob']}")
    
    return True


def resolve_word_index(word, full_pinyin, legacy_word_to_index, exact_word_to_index):
    """优先使用显式拼音做精确映射，精确失败时回退到旧的首个 index 逻辑。"""
    normalized_pinyin = normalize_pinyin(full_pinyin)
    if word and any(ch.isalpha() for ch in word) and all(0x21 <= ord(ch) <= 0x7e for ch in word) and word.lower() in legacy_word_to_index:
        return legacy_word_to_index[word.lower()], 'english'
    if normalized_pinyin:
        exact_index = exact_word_to_index.get((word, normalized_pinyin))
        if exact_index is not None:
            return exact_index, 'exact'

        legacy_index = legacy_word_to_index.get(word)
        if legacy_index is not None:
            return legacy_index, 'fallback_after_exact_miss'
        return None, 'missing_after_exact_miss'

    legacy_index = legacy_word_to_index.get(word)
    if legacy_index is not None:
        return legacy_index, 'legacy'
    return None, 'missing'


def format_candidates(candidates):
    return ', '.join(
        f"{index}:{pinyin if pinyin else '<empty>'}"
        for index, pinyin in candidates
    )


def write_legacy_ambiguity_report(report_file, ambiguous_records):
    with open(report_file, 'w', encoding='utf-8') as f:
        f.write("Legacy m2 ambiguous fallback report\n")
        f.write(f"Total records: {len(ambiguous_records)}\n\n")
        for record in ambiguous_records:
            f.write(
                f"row={record['row_number']} side={record['side']} reason={record['reason']} "
                f"word={record['word']} fallback_index={record['fallback_index']} "
                f"prev_word={record['prev_word']} next_word={record['next_word']} "
                f"candidates=[{record['candidates']}]\n"
            )


def convert_m2_file(m2_file, index_file, output_file, skipped_output_file, trimmed_output_file, legacy_report_file):
    legacy_word_to_index, exact_word_to_index, word_to_candidates = load_word_indexes(index_file)
    
    # 临时文件名
    temp_output = output_file + '.tmp'
    temp_skipped = skipped_output_file + '.tmp'
    temp_trimmed = trimmed_output_file + '.tmp'
    legacy_ambiguous_records = []

    try:
        with open(m2_file, 'r', encoding='utf-8-sig') as fin, \
             open(temp_output, 'w', newline='', encoding='utf-8') as fout, \
             open(temp_skipped, 'w', newline='', encoding='utf-8') as skipped_fout, \
             open(temp_trimmed, 'w', newline='', encoding='utf-8') as trimmed_fout:

            reader = csv.DictReader(fin)
            fieldnames = reader.fieldnames

            writer = csv.writer(fout)
            skipped_writer = csv.DictWriter(skipped_fout, fieldnames=fieldnames)
            trimmed_writer = csv.DictWriter(trimmed_fout, fieldnames=fieldnames)

            writer.writerow(['prev_index', 'next_index', 'value'])
            skipped_writer.writeheader()
            trimmed_writer.writeheader()

            skipped_count = 0
            kept_count = 0
            row_number = 0
            
            for row in reader:
                row_number += 1
                
                try:
                    validate_m2_row(row, row_number)
                except ValueError as e:
                    print(f"错误: {e}")
                    return False
                
                prev_word = row['prev_word'].strip()
                next_word = row['next_word'].strip()
                if prev_word in TERMINAL_WORDS or next_word in TERMINAL_WORDS:
                    skipped_writer.writerow(row)
                    skipped_count += 1
                    continue
                prev_full_pinyin = row.get('prev_fullPinyin', '')
                next_full_pinyin = row.get('next_fullPinyin', '')
                prob = float(row['prob'])

                prev_index, prev_mode = resolve_word_index(prev_word, prev_full_pinyin, legacy_word_to_index, exact_word_to_index)
                next_index, next_mode = resolve_word_index(next_word, next_full_pinyin, legacy_word_to_index, exact_word_to_index)

                if (
                    prev_mode == 'legacy'
                    and not normalize_pinyin(prev_full_pinyin)
                    and len(word_to_candidates.get(prev_word, [])) > 1
                    and prev_word in legacy_word_to_index
                ):
                    legacy_ambiguous_records.append({
                        'row_number': row_number,
                        'side': 'prev',
                        'reason': 'legacy_without_explicit_pinyin',
                        'word': prev_word,
                        'fallback_index': legacy_word_to_index[prev_word],
                        'prev_word': prev_word,
                        'next_word': next_word,
                        'candidates': format_candidates(word_to_candidates[prev_word]),
                    })
                if (
                    next_mode == 'legacy'
                    and not normalize_pinyin(next_full_pinyin)
                    and len(word_to_candidates.get(next_word, [])) > 1
                    and next_word in legacy_word_to_index
                ):
                    legacy_ambiguous_records.append({
                        'row_number': row_number,
                        'side': 'next',
                        'reason': 'legacy_without_explicit_pinyin',
                        'word': next_word,
                        'fallback_index': legacy_word_to_index[next_word],
                        'prev_word': prev_word,
                        'next_word': next_word,
                        'candidates': format_candidates(word_to_candidates[next_word]),
                    })
                if (
                    prev_mode == 'fallback_after_exact_miss'
                    and len(word_to_candidates.get(prev_word, [])) > 1
                    and prev_word in legacy_word_to_index
                ):
                    legacy_ambiguous_records.append({
                        'row_number': row_number,
                        'side': 'prev',
                        'reason': 'fallback_after_exact_miss',
                        'word': prev_word,
                        'fallback_index': legacy_word_to_index[prev_word],
                        'prev_word': prev_word,
                        'next_word': next_word,
                        'candidates': format_candidates(word_to_candidates[prev_word]),
                    })
                if (
                    next_mode == 'fallback_after_exact_miss'
                    and len(word_to_candidates.get(next_word, [])) > 1
                    and next_word in legacy_word_to_index
                ):
                    legacy_ambiguous_records.append({
                        'row_number': row_number,
                        'side': 'next',
                        'reason': 'fallback_after_exact_miss',
                        'word': next_word,
                        'fallback_index': legacy_word_to_index[next_word],
                        'prev_word': prev_word,
                        'next_word': next_word,
                        'candidates': format_candidates(word_to_candidates[next_word]),
                    })

                if prev_index is not None and next_index is not None:
                    writer.writerow([prev_index, next_index, f"{prob}f"])
                    trimmed_writer.writerow(row)
                    kept_count += 1
                else:
                    skipped_writer.writerow(row)
                    skipped_count += 1

            print(f"{ICON_OK} 保留条目: {kept_count}")
            print(f"{ICON_WARN} 跳过条目: {skipped_count}")
            if legacy_ambiguous_records:
                reports_dir = os.path.dirname(legacy_report_file)
                os.makedirs(reports_dir, exist_ok=True)
                write_legacy_ambiguity_report(legacy_report_file, legacy_ambiguous_records)
                print(f"{ICON_WARN} 检测到 {len(legacy_ambiguous_records)} 条 legacy m2 多音词回退记录，详情见: {legacy_report_file}")

        # 写入统计条目数的第一行
        write_with_count_header(temp_output, output_file, 'Total entries')
        write_with_count_header(temp_skipped, skipped_output_file, 'Total skipped')
        write_with_count_header(temp_trimmed, trimmed_output_file, 'Total trimmed')
        
        return True
        
    except Exception as e:
        print(f"处理M2文件时发生错误: {e}")
        return False


def write_bundle_source(index_file, trimmed_file, output_file):
    """Bind the kept M2 rows to the exact endpoints already chosen by indexing.

    The existing DictPack codec uses exact references, whereas indexing also
    supports legacy word-only fallback. Canonicalizing endpoints here preserves
    those selected IDs and their original probability tokens through the bundle.
    """
    legacy, exact, candidates = load_word_indexes(index_file)
    pinyin_by_id = {index: pinyin for rows in candidates.values() for index, pinyin in rows}
    fields = ['prob','prev_word','next_word','prev_fullPinyin','next_fullPinyin']
    with open(trimmed_file, encoding='utf-8-sig', newline='') as source, open(output_file,'x',encoding='utf-8',newline='') as output:
        next(source)  # 计数文件头来自配套的索引转换器
        reader=csv.DictReader(source);writer=csv.DictWriter(output,fieldnames=fields);writer.writeheader()
        for row in reader:
            previous,_=resolve_word_index(row['prev_word'].strip(),row.get('prev_fullPinyin',''),legacy,exact)
            following,_=resolve_word_index(row['next_word'].strip(),row.get('next_fullPinyin',''),legacy,exact)
            if previous is None or following is None:raise ValueError('A kept endpoint could not be resolved')
            writer.writerow({'prob':row['prob'],'prev_word':row['prev_word'].strip(),'next_word':row['next_word'].strip(),
                'prev_fullPinyin':pinyin_by_id[previous],'next_fullPinyin':pinyin_by_id[following]})

if __name__ == "__main__":
    import argparse
    from paths import output_path
    parser=argparse.ArgumentParser(description="Map bigram endpoints to paired lexicon IDs")
    parser.add_argument('--input',required=True)
    parser.add_argument('--index',required=True)
    parser.add_argument('--output-dir',required=True)
    parser.add_argument('--no-emoji',action='store_true')
    args=parser.parse_args()
    try:
        dest=output_path(args.output_dir,[args.input,args.index],directory=True)
        ok=convert_m2_file(args.input,args.index,str(dest/'converted_coral_languagemodel.csv'),str(dest/'skipped_coral_languagemodel.csv'),str(dest/'m2_trimmed.csv'),str(dest/'legacy_m2_ambiguous.txt'))
        if not ok:raise ValueError('Bigram indexing failed')
        write_bundle_source(args.index,str(dest/'m2_trimmed.csv'),str(dest/'bundle_coral_languagemodel.csv'))
    except (ValueError,OSError) as error:parser.exit(1,str(error)+'\n')
