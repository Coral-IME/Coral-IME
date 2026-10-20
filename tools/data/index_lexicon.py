"""
@author: s_yanxiaoren
@description
为主词典词语 ID 排序。

排序依据：
- 全拼和汉字相同的词语认为是一个词。
- 现在支持回退概率列。
"""
import sys
sys.dont_write_bytecode=True
import csv
import os
import hashlib
import json

def validate_row(row, row_number):
    """验证行数据，检查是否有空列"""
    required_fields = ['pinyin', 'frequency', 'word', 'fullPinyin']
    empty_fields = []
    
    for field in required_fields:
        if field not in row or not row[field] or row[field].strip() == '':
            empty_fields.append(field)
    
    if empty_fields:
        raise ValueError(f"第{row_number}行存在空列: {', '.join(empty_fields)}")
    
    return True

def add_index_and_filter_fields(input_file, output_file):
    seen = {}
    next_index = 0
    row_count = 0

    try:
        with open(input_file, newline='', encoding='utf-8-sig') as fin, \
             open(output_file, 'w', newline='', encoding='utf-8') as fout:
            
            reader = csv.DictReader(fin)
            
            # 保留需要的字段 + index，包括 backprob、in_m2（m2 端点严格匹配标记，默认 0）
            kept_fields = ['index', 'pinyin', 'frequency', 'word', 'fullPinyin', 'backprob', 'in_m2']
            writer = csv.DictWriter(fout, fieldnames=kept_fields)

            for row in reader:
                row_count += 1
                
                # 验证行数据
                validate_row(row, row_count)
                
                key = (row['word'], row['fullPinyin'])
                if key in seen:
                    row_index = seen[key]
                else:
                    row_index = next_index
                    seen[key] = next_index
                    next_index += 1

                # 构造精简后的行，保留 backprob、in_m2
                in_m2_val = '0'
                for k, v in row.items():
                    if k and k.strip() == 'in_m2':
                        in_m2_val = str(v).strip() if v else '0'
                        break

                new_row = {
                    'index': row_index,
                    'pinyin': row['pinyin'],
                    'frequency': row['frequency'],
                    'word': row['word'],
                    'fullPinyin': row['fullPinyin'],
                    'backprob': row.get('backprob', '0.000000'),  # 如果没有 backprob 列，使用默认值
                    'in_m2': in_m2_val or '0',
                }
                writer.writerow(new_row)

    except ValueError as e:
        print(f"错误: {e}")
        return False
    except Exception as e:
        print(f"处理文件时发生错误: {e}")
        return False

    with open(output_file, 'r', newline='', encoding='utf-8') as f:
        content = f.read()
    
    with open(output_file, 'w', newline='', encoding='utf-8') as f:
        f.write("HEADER_size\n")
        f.write(f"{row_count}\n")
        f.write("index,pinyin,frequency,word,fullPinyin,backprob,in_m2\n")
        f.write(content)
    
    return True

def read_english_vocabulary(path):
    with open(path, encoding='utf-8-sig', newline='') as source:
        reader = csv.reader(source, strict=True)
        if next(reader, None) != ['search_word', 'display_word', 'frequency', 'can_split']:
            raise ValueError('Unexpected English source CSV header')
        for number, fields in enumerate(reader, 2):
            if not fields or fields == ['']:
                continue
            if len(fields) != 4:
                raise ValueError(f'Invalid English row {number}')
            spelling, word, frequency, split = fields
            frequency = int(frequency)
            if not english_identity_key(word) or not english_identity_key(spelling) or not 0 < frequency <= 1000000000 or split not in ('0', '1'):
                raise ValueError(f'Invalid English row {number}')
            yield spelling, word, frequency, split


def english_identity_key(word):
    return word.lower() if word and any(ch.isalpha() for ch in word) and all(0x21 <= ord(ch) <= 0x7e for ch in word) else None


def merge_english_vocabulary(index_file, english_file):
    """Preserve Chinese IDs; canonicalize ASCII words and append model-only keys.

    Main-dictionary frequencies are authoritative for shared words (maximum across
    aliases). English-source frequency is used only for words absent from the main
    dictionary. The reserved ~en: key is never added to the pinyin syllable table.
    """
    latin_key = english_identity_key

    identities = {}
    next_id = 0
    with open(index_file, encoding='utf-8', newline='') as source:
        next(source)
        count = int(next(source))
        reader = csv.DictReader(source)
        fields = reader.fieldnames
        for row in reader:
            index = int(row['index'])
            next_id = max(next_id, index + 1)
            key = latin_key(row['word'])
            if key:
                old = identities.get(key, (index, 0))
                identities[key] = (min(old[0], index), max(old[1], int(row['frequency'])))

    main_words = set(identities)
    english = []
    for spelling, word, frequency, split in read_english_vocabulary(english_file):
        key = latin_key(word)
        if key not in identities:
            if next_id >= 100000000:
                raise ValueError('Model vocabulary exceeds ID range')
            identities[key] = (next_id, frequency)
            next_id += 1
        elif key not in main_words:
            index, previous = identities[key]
            identities[key] = (index, max(previous, frequency))
        index, frequency = identities[key]
        english.append(dict(index=index, pinyin='~en:' + spelling.lower(),
            frequency=frequency, word=word, fullPinyin=spelling.lower(),
            backprob='0.000000', in_m2='0'))

    temporary = index_file + '.mixed.tmp'
    try:
        with open(index_file, encoding='utf-8', newline='') as source, open(temporary, 'w', encoding='utf-8', newline='') as output:
            output.write(f'HEADER_size\n{count + len(english)}\n')
            next(source)
            next(source)
            reader = csv.DictReader(source)
            writer = csv.DictWriter(output, fieldnames=fields)
            writer.writeheader()
            for row in reader:
                key = latin_key(row['word'])
                if key:
                    row['index'], row['frequency'] = identities[key]
                writer.writerow(row)
            for row in english:
                row['index'], row['frequency'] = identities[latin_key(row['word'])]
                writer.writerow(row)
        os.replace(temporary, index_file)
    finally:
        if os.path.exists(temporary):
            os.remove(temporary)


if __name__ == "__main__":
    import argparse
    from paths import output_path
    parser=argparse.ArgumentParser(description="Index the lexicon without changing its row order or deduplication policy")
    parser.add_argument('--input',required=True)
    parser.add_argument('--output',required=True)
    parser.add_argument('--english',help='Optional English source CSV for shared model identities')
    args=parser.parse_args()
    try:
        dest=output_path(args.output,[args.input] + ([args.english] if args.english else []))
        if not add_index_and_filter_fields(args.input,str(dest)):raise ValueError('Lexicon indexing failed')
        if args.english:merge_english_vocabulary(str(dest),args.english)
    except (ValueError,OSError) as error:parser.exit(1,str(error)+'\n')
