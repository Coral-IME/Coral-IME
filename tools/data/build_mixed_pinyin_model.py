import sys
sys.dont_write_bytecode=True
import pandas as pd
from collections import defaultdict
import struct
import marisa_trie
import re
import math
try:
    from tqdm import tqdm
    HAS_TQDM = True
except ImportError:
    HAS_TQDM = False
    print("提示: 安装 tqdm 可获得更好的进度显示体验 (pip install tqdm)")
    
    class tqdm:
        """简单的 tqdm 替代品"""
        def __init__(self, iterable, desc="", total=None):
            self.iterable = iterable
            self.desc = desc
            self.total = total or (len(iterable) if hasattr(iterable, '__len__') else None)
            self.count = 0
            
        def __iter__(self):
            for item in self.iterable:
                self.count += 1
                if self.count % max(1, (self.total or 1000) // 50) == 0:
                    if self.total:
                        print(f"\r{self.desc}: {self.count}/{self.total} ({100*self.count//self.total}%)", end="", flush=True)
                    else:
                        print(f"\r{self.desc}: {self.count}", end="", flush=True)
                yield item
            if self.total:
                print(f"\r{self.desc}: {self.total}/{self.total} (100%)")
            else:
                print(f"\r{self.desc}: 完成")
                
        def __enter__(self):
            return self
            
        def __exit__(self, *args):
            pass

# ========== 配置参数 ==========
# 混拼频率控制比例：混拼总频率 vs 全拼总频率
MIXED_PINYIN_RATIO = 0.1   # 混拼频率为全拼的3%，提高基础比例
# 混拼模式权重配置
PURE_ABBR_WEIGHT = 1.0      # 纯简拼权重 (如 "zh'g")
HEAD_FULL_WEIGHT = 0.6      # 首字全拼权重 (如 "zhong'g") - 降低以突出纯简拼
# 频率分配策略参数
LENGTH_BOOST_FACTOR = 1.2   # 长音节的简拼倾向加成因子（降低以减少过度偏向）
FREQ_DECAY_STRENGTH = 0.7   # 频率衰减强度 (0.5-1.0，越小衰减越强)

# 声母列表
INITIALS = ['b', 'p', 'm', 'f', 'd', 't', 'n', 'l', 'g', 'k', 'h', 
           'j', 'q', 'x', 'zh', 'ch', 'sh', 'r', 'z', 'c', 's', 'y', 'w']

# 韵母列表（用于验证声母后面的部分）
FINALS = ['a', 'o', 'e', 'i', 'u', 'v', 'ai', 'ei', 'ui', 'ao', 'ou', 'iu', 'ie', 'ue', 've',
          'an', 'en', 'in', 'un', 'vn', 'ang', 'eng', 'ing', 'ong', 'er', 'ia', 'iao', 'ian', 
          'iang', 'ua', 'uo', 'uai', 'uan', 'uang']

def is_valid_syllable(syllable):
    """检查是否为有效的完整拼音音节"""
    return len(syllable) > 1 and not syllable in INITIALS

def extract_initial(syllable):
    """从完整音节中提取声母"""
    for initial in sorted(INITIALS, key=len, reverse=True):  # 先匹配长声母
        if syllable.startswith(initial):
            remaining = syllable[len(initial):]
            if remaining in FINALS:  # 确保剩余部分是有效韵母
                return initial
    return None

def generate_weighted_mixed_patterns(syllables):
    """
    生成带权重的混拼模式，只生成最常用的模式
    返回: [(pattern, weight), ...]
    
    例如: ['zhong', 'guo'] -> 
    [
        (['zh', 'g'], 1.0),        # 纯简拼 (用户输入: zhg) - 最高权重
        (['zhong', 'g'], 0.6),     # 首字全拼 (用户输入: zhongg) - 次高权重
        (['zh', 'guo'], 0.4),      # 尾字全拼 (用户输入: zhguo) - 较低权重
    ]
    """
    if not syllables or len(syllables) < 2:
        return []

    patterns_with_weights = []
    n = len(syllables)
    
    # 模式1: 纯简拼 (例如 "zh'g") - 基础权重最高
    pure_abbr_pattern = []
    is_valid = True
    for s in syllables:
        initial = extract_initial(s)
        if initial:
            pure_abbr_pattern.append(initial)
        else:
            is_valid = False
            break
    if is_valid:
        patterns_with_weights.append((pure_abbr_pattern, PURE_ABBR_WEIGHT))

    # 模式2: 首字全拼，后续简拼 (例如 "zhong'g") - 权重也很高
    if n >= 2:
        head_full_pattern = [syllables[0]]
        is_valid = True
        for i in range(1, n):
            initial = extract_initial(syllables[i])
            if initial:
                head_full_pattern.append(initial)
            else:
                is_valid = False
                break
        if is_valid:
            patterns_with_weights.append((head_full_pattern, HEAD_FULL_WEIGHT))

    # 模式3: 尾字全拼，前面简拼 (例如 "zh'guo") - 对于2字词
    if n == 2:
        initial_first = extract_initial(syllables[0])
        if initial_first:
            tail_full_pattern = [initial_first, syllables[1]]
            patterns_with_weights.append((tail_full_pattern, HEAD_FULL_WEIGHT * 0.7))
    
    # 模式4: 对于特别长的词(>=3音节)，添加中间全拼模式
    if n >= 3:
        # 首尾简拼，中间全拼
        initial_first = extract_initial(syllables[0])
        initial_last = extract_initial(syllables[-1])
        if initial_first and initial_last:
            middle_full_pattern = [initial_first] + syllables[1:-1] + [initial_last]
            patterns_with_weights.append((middle_full_pattern, HEAD_FULL_WEIGHT * 0.5))

    return patterns_with_weights

def create_mixed_pinyin_model(csv_filepath, output_path="mixed_pinyin_model.dat"):
    print("=== 构建混合拼音模型 ===")
    print(f"混拼频率比例: {MIXED_PINYIN_RATIO}")
    
    # ========== 阶段 1: 加载原始数据并统计全拼 ==========
    print("阶段 1/5: 加载CSV并统计全拼N-gram...")
    df = pd.read_csv(csv_filepath, encoding='utf-8')
    
    # 全拼统计
    full_unigram_counts = defaultdict(int)
    full_bigram_counts = defaultdict(int)
    full_trigram_counts = defaultdict(int)
    total_full_frequency = 0
    
    # 收集所有拼音序列用于后续混拼生成
    all_pinyin_sequences = []
    
    for _, row in tqdm(df.iterrows(), desc="统计全拼", total=len(df)):
        pinyin_str = row['fullPinyin']
        frequency = int(row['frequency'])
        if not isinstance(pinyin_str, str): 
            continue
            
        syllables = pinyin_str.split("'")
        all_pinyin_sequences.append((syllables, frequency))
        total_full_frequency += frequency
        
        # 统计全拼N-gram
        seq = ['<s>', '<s>'] + syllables + ['</s>']
        for s in syllables: 
            full_unigram_counts[s] += frequency
        for i in range(len(seq) - 1): 
            full_bigram_counts[(seq[i], seq[i + 1])] += frequency
        for i in range(len(seq) - 2): 
            full_trigram_counts[(seq[i], seq[i + 1], seq[i + 2])] += frequency
    
    print(f"全拼总频率: {total_full_frequency}")
    
    # ========== 阶段 2: 生成混拼数据 ==========
    print("阶段 2/5: 生成混拼模式...")
    mixed_unigram_counts = defaultdict(int)
    mixed_bigram_counts = defaultdict(int)
    mixed_trigram_counts = defaultdict(int)
    total_mixed_frequency = 0
    
    # 计算目标混拼频率
    target_mixed_frequency = int(total_full_frequency * MIXED_PINYIN_RATIO)
    
    # 为每个拼音序列生成混拼模式
    mixed_patterns_generated = 0
    for syllables, original_frequency in tqdm(all_pinyin_sequences, desc="生成混拼"):
        
        patterns_with_weights = generate_weighted_mixed_patterns(syllables)
        
        if not patterns_with_weights:
            continue

        # 计算总权重，用于归一化
        total_weight = sum(weight for _, weight in patterns_with_weights)

        # === 智能频率分配策略 ===
        
        # 1. 计算音节序列总长度（字符数）
        total_chars = sum(len(syl) for syl in syllables)
        
        # 2. 长度因子：采用渐进式增长，所有长度的词都可以有混拼
        # 使用平滑的增长函数，避免硬阈值
        length_factor = 0.8 + (total_chars * 0.1 * LENGTH_BOOST_FACTOR)
        
        # 3. 频率衰减：使用幂函数而不是对数，更温和的衰减
        # 原频率^0.7 比 log 函数保留了更多的频率差异
        decay_factor = math.pow(original_frequency, FREQ_DECAY_STRENGTH) / math.pow(10000, FREQ_DECAY_STRENGTH)
        
        # 4. 组合所有因子计算最终频率
        base_mixed_freq = original_frequency * MIXED_PINYIN_RATIO
        total_freq_for_this_word = int(base_mixed_freq * length_factor * max(0.1, decay_factor))

        for pattern, weight in patterns_with_weights:
            # 按权重分配频率
            pattern_frequency = max(1, int(total_freq_for_this_word * (weight / total_weight)))
            
            if total_mixed_frequency + pattern_frequency > target_mixed_frequency:
                pattern_frequency = target_mixed_frequency - total_mixed_frequency

            if pattern_frequency <= 0:
                continue
                
            total_mixed_frequency += pattern_frequency
            mixed_patterns_generated += 1
            
            # 统计混拼N-gram
            seq = ['<s>', '<s>'] + pattern + ['</s>']
            for s in pattern:
                mixed_unigram_counts[s] += pattern_frequency
            for i in range(len(seq) - 1):
                mixed_bigram_counts[(seq[i], seq[i + 1])] += pattern_frequency
            for i in range(len(seq) - 2):
                mixed_trigram_counts[(seq[i], seq[i + 1], seq[i + 2])] += pattern_frequency
            
            # 控制总频率不超过目标
            if total_mixed_frequency >= target_mixed_frequency:
                break
        
        if total_mixed_frequency >= target_mixed_frequency:
            break
    
    print(f"生成混拼模式: {mixed_patterns_generated}个")
    print(f"混拼总频率: {total_mixed_frequency}")
    print(f"频率比例: {total_mixed_frequency/total_full_frequency:.3f}")
    
    # ========== 阶段 3: 合并统计数据 ==========
    print("阶段 3/5: 合并全拼和混拼统计...")
    
    # 合并Unigram
    final_unigram_counts = defaultdict(int)
    for k, v in full_unigram_counts.items():
        final_unigram_counts[k] += v
    for k, v in mixed_unigram_counts.items():
        final_unigram_counts[k] += v
    
    # 合并Bigram
    final_bigram_counts = defaultdict(int)
    for k, v in full_bigram_counts.items():
        final_bigram_counts[k] += v
    for k, v in mixed_bigram_counts.items():
        final_bigram_counts[k] += v
    
    # 合并Trigram
    final_trigram_counts = defaultdict(int)
    for k, v in full_trigram_counts.items():
        final_trigram_counts[k] += v
    for k, v in mixed_trigram_counts.items():
        final_trigram_counts[k] += v
    
    # ========== 阶段 4: 构建Trie和数据结构 ==========
    print("阶段 4/5: 构建Trie和数据结构...")
    
    # 构建包含所有音节（全拼+声母）的Trie
    all_syllables_set = set(final_unigram_counts.keys()) | {'<s>', '</s>'}
    # 添加所有声母到Trie中
    all_syllables_set.update(INITIALS)
    
    VALID_SINGLE_LETTER_SYLLABLES = {'a', 'o', 'e'}
    # 特殊字符（必须保留，可能被C++代码依赖）
    SPECIAL_CHARS = {'（', '）'}
    
    filtered_syllables = {s for s in all_syllables_set 
                         if len(s) > 1 or s in VALID_SINGLE_LETTER_SYLLABLES or s in INITIALS or s in SPECIAL_CHARS}
    final_syllables_for_trie = sorted(list(filtered_syllables | {'<s>', '</s>'}))
    
    syllable_trie = marisa_trie.Trie(final_syllables_for_trie)
    print(f"Trie包含音节数: {len(final_syllables_for_trie)}")
    
    # 构建Unigram数组
    unigram_bin_data = [0] * len(final_syllables_for_trie)
    for syllable, count in final_unigram_counts.items():
        if syllable in syllable_trie:
            unigram_bin_data[syllable_trie.key_id(syllable)] = count
    
    # 构建Bigram列表
    bigram_list = []
    for (s1, s2), count in final_bigram_counts.items():
        if s1 in syllable_trie and s2 in syllable_trie:
            key = (syllable_trie.key_id(s1) << 16) | syllable_trie.key_id(s2)
            bigram_list.append({'key': key, 'count': count})
    bigram_list.sort(key=lambda x: x['key'])
    
    # 构建Trigram列表
    trigram_list = []
    for (s1, s2, s3), count in final_trigram_counts.items():
        if s1 in syllable_trie and s2 in syllable_trie and s3 in syllable_trie:
            key = (syllable_trie.key_id(s1) << 32) | (syllable_trie.key_id(s2) << 16) | syllable_trie.key_id(s3)
            trigram_list.append({'key': key, 'count': count})
    trigram_list.sort(key=lambda x: x['key'])
    
    # ========== 阶段 5: 生成最终模型文件 ==========
    print("阶段 5/5: 生成最终模型文件...")
    
    # 打包二进制数据
    trie_data = syllable_trie.tobytes()
    unigram_data = struct.pack(f'<{len(unigram_bin_data)}Q', *unigram_bin_data)
    
    bigram_data_packer = [struct.pack('<L', len(bigram_list))]
    for item in bigram_list:
        bigram_data_packer.append(struct.pack('<LQ', item['key'], item['count']))
    bigram_data = b"".join(bigram_data_packer)
    
    trigram_data_packer = [struct.pack('<L', len(trigram_list))]
    for item in trigram_list:
        trigram_data_packer.append(struct.pack('<QQ', item['key'], item['count']))
    trigram_data = b"".join(trigram_data_packer)
    
    # 构建文件头
    header_format = '<8sLQQQQQQQQ'
    header_size = struct.calcsize(header_format)
    current_offset = header_size
    
    trie_offset, trie_size = current_offset, len(trie_data)
    current_offset += trie_size
    unigram_offset, unigram_size = current_offset, len(unigram_data)
    current_offset += unigram_size
    bigram_offset, bigram_size = current_offset, len(bigram_data)
    current_offset += bigram_size
    trigram_offset, trigram_size = current_offset, len(trigram_data)
    
    header = struct.pack(header_format,
                         b'PINYINLM', 1,
                         trie_offset, trie_size,
                         unigram_offset, unigram_size,
                         bigram_offset, bigram_size,
                         trigram_offset, trigram_size)
    
    # 写入文件
    with open(output_path, 'wb') as f:
        f.write(header)
        f.write(trie_data)
        f.write(unigram_data)
        f.write(bigram_data)
        f.write(trigram_data)
    
    print("=== 混合拼音模型构建完成 ===")
    print(f"输出文件: {output_path}")
    print(f"全拼频率: {total_full_frequency}")
    print(f"混拼频率: {total_mixed_frequency}")
    print(f"混拼比例: {total_mixed_frequency/total_full_frequency:.3f}")
    print(f"总音节数: {len(final_syllables_for_trie)}")
    
    # 输出一些统计信息用于调试
    print("\n=== 声母频率统计 ===")
    initial_frequencies = {}
    for initial in INITIALS:
        if initial in final_unigram_counts:
            initial_frequencies[initial] = final_unigram_counts[initial]
    
    sorted_initials = sorted(initial_frequencies.items(), key=lambda x: x[1], reverse=True)
    print("前10个高频声母:")
    for initial, freq in sorted_initials[:10]:
        print(f"  {initial}: {freq}")


if __name__ == '__main__':
    import argparse
    from paths import output_path
    parser=argparse.ArgumentParser(description="Build the existing mixed-pinyin model")
    parser.add_argument('--input',required=True)
    parser.add_argument('--output',required=True)
    args=parser.parse_args()
    try:
        dest=output_path(args.output,[args.input])
        create_mixed_pinyin_model(args.input,str(dest))
    except (ValueError,OSError) as error:parser.exit(1,str(error)+'\n')
