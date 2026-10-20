#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
字母三元语言模型构建脚本
用于为拼音纠错系统的4种纠错规则（插入、删除、替换、交换）提供字母级别的概率支持

技术方案：
- 采用多进制编码+稠密数组的数据结构，实现O(1)查询性能
- 字母表包含26个小写字母和1个句首符<s>
- 支持Unigram、Bigram、Trigram三种N-gram模型
- 生成高效的二进制模型文件供C++高效读取
"""
import sys
sys.dont_write_bytecode=True

import pandas as pd
import struct
import re
import math
from collections import defaultdict

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

# ========== 字母语言模型配置 ==========
# 字母表定义：句首符 + 26个小写字母 + 句尾符
ALPHABET = ['<s>'] + [chr(ord('a') + i) for i in range(26)] + ['</s>']
ALPHABET_MAP = {char: i + 1 for i, char in enumerate(ALPHABET)}
BASE_SIZE = len(ALPHABET)  # 28

# 数组大小计算
UNIGRAM_ARRAY_SIZE = BASE_SIZE + 1                          # 29
BIGRAM_ARRAY_SIZE = BASE_SIZE * BASE_SIZE + 1              # 785
TRIGRAM_ARRAY_SIZE = BASE_SIZE * BASE_SIZE * BASE_SIZE + 1 # 21,953

# 拼音组合长度范围配置
MIN_PINYIN_LENGTH = 0  # 修改为从0开始，0对应声母，1对应长度1的韵母
MAX_PINYIN_LENGTH = 16
PINYIN_LENGTH_RANGE = MAX_PINYIN_LENGTH - MIN_PINYIN_LENGTH + 1  # 17个长度 (0-16)

print(f"字母表大小: {BASE_SIZE}")
print(f"Unigram数组大小: {UNIGRAM_ARRAY_SIZE}")
print(f"Bigram数组大小: {BIGRAM_ARRAY_SIZE}")
print(f"Trigram数组大小: {TRIGRAM_ARRAY_SIZE}")
print(f"拼音长度范围: {MIN_PINYIN_LENGTH}-{MAX_PINYIN_LENGTH} ({PINYIN_LENGTH_RANGE}个)")
print(f"预计内存占用: {(UNIGRAM_ARRAY_SIZE + BIGRAM_ARRAY_SIZE + TRIGRAM_ARRAY_SIZE) * 8 / 1024:.1f} KB")

def char_to_id(char):
    """将字符转换为ID"""
    # 特殊处理边界符
    if char == '<':
        return ALPHABET_MAP.get('<s>', -1)
    elif char == '>':
        return ALPHABET_MAP.get('</s>', -1)
    else:
        return ALPHABET_MAP.get(char, -1)

def compute_unigram_key(c1_id):
    """计算Unigram的key（数组索引）"""
    return c1_id

def compute_bigram_key(c1_id, c2_id):
    """计算Bigram的key（数组索引）"""
    return c1_id * BASE_SIZE + c2_id

def compute_trigram_key(c1_id, c2_id, c3_id):
    """计算Trigram的key（数组索引）"""
    return c1_id * BASE_SIZE * BASE_SIZE + c2_id * BASE_SIZE + c3_id



# 声母列表
INITIALS = ['b', 'p', 'm', 'f', 'd', 't', 'n', 'l', 'g', 'k', 'h', 
           'j', 'q', 'x', 'zh', 'ch', 'sh', 'r', 'z', 'c', 's', 'y', 'w']

# 韵母列表（修正：移除单独的 v，v 只在特定组合中出现）
FINALS = ['a', 'o', 'e', 'i', 'u', 'ai', 'ei', 'ui', 'ao', 'ou', 'iu', 'ie', 'ue', 've',
          'an', 'en', 'in', 'un', 'vn', 'ang', 'eng', 'ing', 'ong', 'er', 'ia', 'iao', 'ian', 
          'iang', 'ua', 'uo', 'uai', 'uan', 'uang']

# 有效的完整拼音音节（用于最终验证）
VALID_SYLLABLES = {
    # 单韵母
    'a', 'o', 'e', 'ai', 'ei', 'ao', 'ou', 'an', 'en', 'ang', 'eng', 'er',
    # b组
    'ba', 'bo', 'bai', 'bei', 'bao', 'ban', 'ben', 'bang', 'beng', 'bi', 'bie', 'biao', 'bian', 'bin', 'bing',
    # p组  
    'pa', 'po', 'pai', 'pei', 'pao', 'pou', 'pan', 'pen', 'pang', 'peng', 'pi', 'pie', 'piao', 'pian', 'pin', 'ping',
    # m组
    'ma', 'mo', 'me', 'mai', 'mei', 'mao', 'mou', 'man', 'men', 'mang', 'meng', 'mi', 'mie', 'miao', 'miu', 'mian', 'min', 'ming',
    # f组
    'fa', 'fo', 'fei', 'fou', 'fan', 'fen', 'fang', 'feng',
    # d组
    'da', 'de', 'dai', 'dei', 'dao', 'dou', 'dan', 'den', 'dang', 'deng', 'di', 'die', 'diao', 'diu', 'dian', 'ding',
    # t组
    'ta', 'te', 'tai', 'tao', 'tou', 'tan', 'tang', 'teng', 'ti', 'tie', 'tiao', 'tian', 'ting',
    # n组
    'na', 'ne', 'nai', 'nei', 'nao', 'nou', 'nan', 'nen', 'nang', 'neng', 'ni', 'nie', 'niao', 'niu', 'nian', 'nin', 'ning', 'nv',
    # l组
    'la', 'le', 'lai', 'lei', 'lao', 'lou', 'lan', 'lang', 'leng', 'li', 'lia', 'lie', 'liao', 'liu', 'lian', 'lin', 'ling', 'lv',
    # g组
    'ga', 'ge', 'gai', 'gei', 'gao', 'gou', 'gan', 'gen', 'gang', 'geng', 'gong', 'gu', 'gua', 'guo', 'guai', 'gui', 'guan', 'gun', 'guang',
    # k组
    'ka', 'ke', 'kai', 'kao', 'kou', 'kan', 'ken', 'kang', 'keng', 'kong', 'ku', 'kua', 'kuo', 'kuai', 'kui', 'kuan', 'kun', 'kuang',
    # h组
    'ha', 'he', 'hai', 'hei', 'hao', 'hou', 'han', 'hen', 'hang', 'heng', 'hong', 'hu', 'hua', 'huo', 'huai', 'hui', 'huan', 'hun', 'huang',
    # j组
    'ji', 'jia', 'jie', 'jiao', 'jiu', 'jian', 'jin', 'jiang', 'jing', 'jiong', 'ju', 'jue', 'juan', 'jun',
    # q组
    'qi', 'qia', 'qie', 'qiao', 'qiu', 'qian', 'qin', 'qiang', 'qing', 'qiong', 'qu', 'que', 'quan', 'qun',
    # x组
    'xi', 'xia', 'xie', 'xiao', 'xiu', 'xian', 'xin', 'xiang', 'xing', 'xiong', 'xu', 'xue', 'xuan', 'xun',
    # zh组
    'zha', 'zhe', 'zhi', 'zhao', 'zhou', 'zhan', 'zhen', 'zhang', 'zheng', 'zhong', 'zhu', 'zhua', 'zhuo', 'zhuai', 'zhui', 'zhuan', 'zhuang', 'zhun',
    # ch组
    'cha', 'che', 'chi', 'chao', 'chou', 'chan', 'chen', 'chang', 'cheng', 'chong', 'chu', 'chua', 'chuo', 'chuai', 'chui', 'chuan', 'chuang', 'chun',
    # sh组
    'sha', 'she', 'shi', 'shao', 'shou', 'shan', 'shen', 'shang', 'sheng', 'shu', 'shua', 'shuo', 'shuai', 'shui', 'shuan', 'shuang', 'shun',
    # r组
    'ra', 're', 'ri', 'rao', 'rou', 'ran', 'ren', 'rang', 'reng', 'rong', 'ru', 'rua', 'ruo', 'rui', 'ruan', 'run',
    # z组
    'za', 'ze', 'zi', 'zai', 'zei', 'zao', 'zou', 'zan', 'zen', 'zang', 'zeng', 'zong', 'zu', 'zuo', 'zui', 'zuan', 'zun',
    # c组
    'ca', 'ce', 'ci', 'cai', 'cao', 'cou', 'can', 'cen', 'cang', 'ceng', 'cong', 'cu', 'cuo', 'cui', 'cuan', 'cun',
    # s组
    'sa', 'se', 'si', 'sai', 'sao', 'sou', 'san', 'sen', 'sang', 'seng', 'song', 'su', 'suo', 'sui', 'suan', 'sun',
    # y组
    'ya', 'yo', 'ye', 'yao', 'you', 'yan', 'yang', 'ying', 'yong', 'yi', 'yin',
    # w组
    'wa', 'wo', 'wai', 'wei', 'wan', 'wen', 'wang', 'weng', 'wu'
}

def is_valid_pinyin_syllable(syllable):
    """检查是否为有效的拼音音节，排除单字母（纯简拼）"""
    # 排除单字母（包括 a, o, e 等），因为这些属于纯简拼
    if len(syllable) <= 1:
        return False
    return syllable.lower() in VALID_SYLLABLES

def extract_initial(syllable):
    """从完整音节中提取声母，更严格的验证"""
    if not syllable or len(syllable) < 2:
        return None
        
    for initial in sorted(INITIALS, key=len, reverse=True):  # 先匹配长声母
        if syllable.startswith(initial):
            remaining = syllable[len(initial):]
            # 检查剩余部分是否为有效韵母
            if remaining and remaining in FINALS:
                return initial
    return None

def generate_mixed_patterns_for_char_model(syllables):
    """
    生成混拼模式用于字符模型，不包含纯简拼
    返回拼音字符串列表
    
    例如: ['zhong', 'guo'] -> 
    [
        'zhongg',      # 首字全拼 + 后续简拼
        'zhguo',       # 前面简拼 + 尾字全拼 (仅2字词)
        'zhguor'       # 首尾简拼 + 中间全拼 (仅3+字词)
    ]
    """
    if not syllables or len(syllables) < 2:
        return []
    
    # 首先验证所有输入音节的有效性
    for syl in syllables:
        if not is_valid_pinyin_syllable(syl):
            return []  # 如果有任何无效音节，跳过整个词
    
    patterns = []
    n = len(syllables)
    
    # 模式1: 首字全拼，后续简拼 (例如 "zhongg")
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
            patterns.append(''.join(head_full_pattern))

    # 模式2: 尾字全拼，前面简拼 (例如 "zhguo") - 仅用于2字词
    if n == 2:
        initial_first = extract_initial(syllables[0])
        if initial_first:
            tail_full_pattern = [initial_first, syllables[1]]
            patterns.append(''.join(tail_full_pattern))
    
    # 模式3: 对于特别长的词(>=3音节)，添加中间全拼模式
    if n >= 3:
        # 首尾简拼，中间全拼
        initial_first = extract_initial(syllables[0])
        initial_last = extract_initial(syllables[-1])
        if initial_first and initial_last:
            middle_full_pattern = [initial_first] + syllables[1:-1] + [initial_last]
            patterns.append(''.join(middle_full_pattern))
    
    return patterns

def extract_pinyin_patterns_from_corpus(csv_filepath):
    """
    从拼音语料库中提取真实的拼音组合模式，包括混拼组合
    返回: 按长度分组的拼音集合列表
    """
    print("\n=== 从语料库提取拼音组合 ===")
    
    # 获取声母集合
    initials = INITIALS.copy()
    print(f"声母数量: {len(initials)}")
    print(f"声母列表: {sorted(initials)}")
    
    # 初始化结果列表：索引0对应声母，索引1对应长度1，...，索引10对应长度10
    pinyin_sets = [set() for _ in range(PINYIN_LENGTH_RANGE)]
    
    # 设置list[0]为声母集合
    pinyin_sets[0] = set(initials)
    
    # 读取CSV数据，根据文件路径判断格式
    columns = pd.read_csv(csv_filepath, encoding="utf-8", nrows=0).columns
    if "fullPinyin" in columns:
        # coral_lexicon.csv：首行为表头，含 fullPinyin、frequency；可含 identifier、isPolyphonic、in_m2 等扩展列
        df = pd.read_csv(csv_filepath, encoding='utf-8')
        pinyin_column = 'fullPinyin'
        frequency_column = 'frequency'
        for col in (pinyin_column, frequency_column):
            if col not in df.columns:
                raise ValueError(f"coral_lexicon.csv 缺少必需列 {col!r}，当前列: {list(df.columns)}")
    else:
        # merged_pinyin_frequency.csv 格式：第1列是 pinyinstr
        df = pd.read_csv(csv_filepath, encoding='utf-8')
        pinyin_column = 'pinyinstr'
        frequency_column = 'frequency'  # 假设也有frequency列
    
    # 合并相同拼音的条目，累加频率
    print("合并相同拼音条目...")
    pinyin_frequency = df.groupby(pinyin_column)[frequency_column].sum().reset_index()
    pinyin_frequency = pinyin_frequency.sort_values(by=frequency_column, ascending=False)
    
    # 计算频率阈值：前10%的词用于生成混拼
    total_unique_pinyin = len(pinyin_frequency)
    top_10_percent_count = int(total_unique_pinyin * 0.1)
    frequency_threshold = pinyin_frequency.iloc[top_10_percent_count - 1][frequency_column] if top_10_percent_count > 0 else 0
    
    print(f"数据统计:")
    print(f"  原始词条数: {len(df)}")
    print(f"  唯一拼音数: {total_unique_pinyin}")
    print(f"  前10%拼音数: {top_10_percent_count}")
    print(f"  频率阈值: {frequency_threshold}")
    
    # 创建高频拼音集合，用于快速查找
    high_freq_pinyin_set = set(pinyin_frequency.head(top_10_percent_count)[pinyin_column])
    
    total_patterns = 0
    mixed_patterns_generated = 0
    high_freq_mixed_generated = 0
    
    for _, row in tqdm(df.iterrows(), desc="提取拼音模式", total=len(df)):
        pinyin_str = row[pinyin_column]
        
        if not isinstance(pinyin_str, str):
            continue
        
        # 处理全拼音节
        if "'" in pinyin_str:
            # 包含分隔符的多音节拼音
            syllables = pinyin_str.split("'")
            
            # 验证所有音节的有效性
            all_valid = all(is_valid_pinyin_syllable(syl) for syl in syllables)
            if not all_valid:
                continue  # 跳过包含无效音节的词
            
            # 添加原始全拼字符序列
            full_char_sequence = extract_characters_from_pinyin(pinyin_str)
            if len(full_char_sequence) >= 1:
                length = len(full_char_sequence)
                if 1 <= length <= MAX_PINYIN_LENGTH:
                    pinyin_sets[length].add(full_char_sequence)
                    total_patterns += 1
            
            # 只对高频拼音生成混拼模式（基于合并后的拼音频率）
            if pinyin_str in high_freq_pinyin_set:
                mixed_patterns = generate_mixed_patterns_for_char_model(syllables)
                for mixed_pattern in mixed_patterns:
                    mixed_char_sequence = extract_characters_from_pinyin(mixed_pattern)
                    if len(mixed_char_sequence) >= 1:
                        length = len(mixed_char_sequence)
                        if 1 <= length <= MAX_PINYIN_LENGTH:
                            pinyin_sets[length].add(mixed_char_sequence)
                            high_freq_mixed_generated += 1
            
            mixed_patterns_generated += len(generate_mixed_patterns_for_char_model(syllables))
        else:
            # 单音节拼音，验证有效性后处理
            if is_valid_pinyin_syllable(pinyin_str):
                char_sequence = extract_characters_from_pinyin(pinyin_str)
                if len(char_sequence) >= 1:
                    length = len(char_sequence)
                    if 1 <= length <= MAX_PINYIN_LENGTH:
                        pinyin_sets[length].add(char_sequence)
                        total_patterns += 1
    
    # 统计提取结果
    print(f"语料库拼音提取完成:")
    print(f"  声母(索引0): {len(pinyin_sets[0])}个")
    total_combinations = len(pinyin_sets[0])
    for i in range(1, len(pinyin_sets)):
        count = len(pinyin_sets[i])
        total_combinations += count
        print(f"  长度{i}: {count}个")
    
    print(f"总组合数: {total_combinations}")
    print(f"原始全拼模式: {total_patterns}个")
    print(f"总混拼模式: {mixed_patterns_generated}个")
    print(f"高频混拼模式(实际添加): {high_freq_mixed_generated}个")
    return pinyin_sets

def calculate_trigram_probability(pinyin_string, unigram_array, bigram_array, trigram_array):
    """
    计算给定拼音字符串的字符三元概率 - 匹配C++实现
    使用与C++相同的算法：平均对数概率
    """
    if not pinyin_string:
        return float('-inf')  # 空字符串返回负无穷
    
    # 构建扩展序列：< + chars + > (匹配C++的 "<" + sequence + ">")
    extended_sequence = '<' + pinyin_string.lower() + '>'
    
    log_prob = 0.0
    valid_trigrams = 0
    
    # 计算所有trigram的对数概率
    for i in range(len(extended_sequence) - 2):
        c1 = extended_sequence[i]
        c2 = extended_sequence[i + 1]
        c3 = extended_sequence[i + 2]
        
        c1_id = char_to_id(c1)
        c2_id = char_to_id(c2)
        c3_id = char_to_id(c3)
        
        if c1_id < 0 or c2_id < 0 or c3_id < 0:
            # 包含无效字符，使用小的平滑概率
            log_prob += math.log(1e-10)
            continue
        
        # 获取trigram计数
        trigram_key = compute_trigram_key(c1_id, c2_id, c3_id)
        trigram_count = trigram_array[trigram_key] if 0 <= trigram_key < len(trigram_array) else 0
        
        if trigram_count > 0:
            # 使用最大似然估计：P(c3|c1,c2) = count(c1,c2,c3) / count(c1,c2)
            bigram_key = compute_bigram_key(c1_id, c2_id)
            bigram_count = bigram_array[bigram_key] if 0 <= bigram_key < len(bigram_array) else 0
            
            if bigram_count > 0:
                prob = trigram_count / bigram_count
                log_prob += math.log(prob)
                valid_trigrams += 1
            else:
                # bigram计数为0，使用平滑概率
                log_prob += math.log(1e-10)
        else:
            # trigram计数为0，使用平滑概率
            log_prob += math.log(1e-10)
    
    # 如果没有有效的trigram，返回负无穷
    if valid_trigrams == 0:
        return float('-inf')
    
    # 返回平均对数概率 (匹配C++的 logProb / validTrigrams)
    return log_prob / valid_trigrams

def extract_characters_from_pinyin(pinyin_str):
    """
    从拼音字符串中提取字母序列
    例如: "zhong'guo" -> "zhongguo"
    """
    # 移除分隔符，只保留字母
    chars = re.sub(r"[^a-zA-Z]", "", pinyin_str.lower())
    return chars

def build_char_ngram_from_corpus(csv_filepath):
    """
    从拼音语料库构建字符N-gram统计
    """
    print("\n=== 阶段 1: 从拼音语料库提取字符序列 ===")
    
    # 统计容器
    unigram_counts = defaultdict(int)
    bigram_counts = defaultdict(int)
    trigram_counts = defaultdict(int)
    
    # 读取CSV数据
    df = pd.read_csv(csv_filepath, encoding='utf-8')
    total_frequency = 0
    total_chars_processed = 0
    
    for _, row in tqdm(df.iterrows(), desc="处理拼音数据", total=len(df)):
        pinyin_str = row['pinyinstr']
        frequency = int(row['frequency'])
        
        if not isinstance(pinyin_str, str):
            continue
            
        # 提取字符序列
        char_sequence = extract_characters_from_pinyin(pinyin_str)
        if len(char_sequence) < 1:
            continue
            
        total_frequency += frequency
        total_chars_processed += len(char_sequence) * frequency
        
        # 构建完整序列：<s> + chars + </s>
        extended_sequence = ['<s>'] + list(char_sequence) + ['</s>']
        
        # 统计Unigram (包含结束符)
        for char in list(char_sequence) + ['</s>']:
            char_id = char_to_id(char)
            if char_id >= 0:
                unigram_counts[char_id] += frequency
        
        # 统计Bigram
        for i in range(len(extended_sequence) - 1):
            c1_id = char_to_id(extended_sequence[i])
            c2_id = char_to_id(extended_sequence[i + 1])
            if c1_id >= 0 and c2_id >= 0:
                key = compute_bigram_key(c1_id, c2_id)
                bigram_counts[key] += frequency
        
        # 统计Trigram
        for i in range(len(extended_sequence) - 2):
            c1_id = char_to_id(extended_sequence[i])
            c2_id = char_to_id(extended_sequence[i + 1])
            c3_id = char_to_id(extended_sequence[i + 2])
            if c1_id >= 0 and c2_id >= 0 and c3_id >= 0:
                key = compute_trigram_key(c1_id, c2_id, c3_id)
                trigram_counts[key] += frequency
    
    print(f"处理完成:")
    print(f"  总词条数: {len(df)}")
    print(f"  总频率: {total_frequency}")
    print(f"  总字符数: {total_chars_processed}")
    print(f"  Unigram类型数: {len(unigram_counts)}")
    print(f"  Bigram类型数: {len(bigram_counts)}")
    print(f"  Trigram类型数: {len(trigram_counts)}")
    
    return unigram_counts, bigram_counts, trigram_counts

def create_dense_arrays(unigram_counts, bigram_counts, trigram_counts):
    """
    创建稠密数组，用于O(1)查询
    """
    print("\n=== 阶段 2: 创建稠密数组 ===")
    
    # 初始化稠密数组
    unigram_array = [0] * UNIGRAM_ARRAY_SIZE
    bigram_array = [0] * BIGRAM_ARRAY_SIZE
    trigram_array = [0] * TRIGRAM_ARRAY_SIZE
    
    # 填充Unigram数组
    for char_id, count in unigram_counts.items():
        if 0 <= char_id < UNIGRAM_ARRAY_SIZE:
            unigram_array[char_id] = count
    
    # 填充Bigram数组
    for key, count in bigram_counts.items():
        if 0 <= key < BIGRAM_ARRAY_SIZE:
            bigram_array[key] = count
    
    # 填充Trigram数组
    for key, count in trigram_counts.items():
        if 0 <= key < TRIGRAM_ARRAY_SIZE:
            trigram_array[key] = count
    
    # 统计非零元素
    unigram_nonzero = sum(1 for x in unigram_array if x > 0)
    bigram_nonzero = sum(1 for x in bigram_array if x > 0)
    trigram_nonzero = sum(1 for x in trigram_array if x > 0)
    
    print(f"稠密数组创建完成:")
    print(f"  Unigram非零元素: {unigram_nonzero}/{UNIGRAM_ARRAY_SIZE} ({100*unigram_nonzero/UNIGRAM_ARRAY_SIZE:.1f}%)")
    print(f"  Bigram非零元素: {bigram_nonzero}/{BIGRAM_ARRAY_SIZE} ({100*bigram_nonzero/BIGRAM_ARRAY_SIZE:.1f}%)")
    print(f"  Trigram非零元素: {trigram_nonzero}/{TRIGRAM_ARRAY_SIZE} ({100*trigram_nonzero/TRIGRAM_ARRAY_SIZE:.1f}%)")
    
    return unigram_array, bigram_array, trigram_array

def write_binary_model(unigram_array, bigram_array, trigram_array, pinyin_sets, output_path):
    """
    将模型写入二进制文件
    
    文件格式:
    [ CharLMHeader (16 bytes) ]
    [ Unigram Counts (UNIGRAM_ARRAY_SIZE * 8 bytes) ]
    [ Bigram Counts (BIGRAM_ARRAY_SIZE * 8 bytes) ]  
    [ Trigram Counts (TRIGRAM_ARRAY_SIZE * 8 bytes) ]
    [ Pinyin Length Thresholds ((MAX_PINYIN_LENGTH-1) * 8 bytes) ] - 长度2-MAX_PINYIN_LENGTH的最小概率阈值
    """
    print(f"\n=== 阶段 3: 写入二进制文件 {output_path} ===")
    
    # 计算每个长度的最小概率阈值（只保存长度2以上的阈值）
    pinyin_length_thresholds = []
    
    # 跳过声母(索引0)和长度1(索引1)，只处理长度2-MAX_PINYIN_LENGTH
    for i in range(2, MAX_PINYIN_LENGTH + 1):
        pinyin_set = pinyin_sets[i]
        pinyin_list = sorted(list(pinyin_set))  # 排序保证一致性
        
        # 计算当前长度的最小概率阈值
        min_threshold = float('inf')
        min_threshold_pinyin = ""
        valid_count = 0
        inf_count = 0
        for pinyin in pinyin_list:
            prob = calculate_trigram_probability(pinyin, unigram_array, bigram_array, trigram_array)
            if prob != float('-inf'):  # 不是负无穷
                valid_count += 1
                if prob < min_threshold:
                    min_threshold = prob
                    min_threshold_pinyin = pinyin
            else:
                inf_count += 1
        
        # 真实语料库中的拼音应该都有有效概率，如果没有则是异常情况
        if min_threshold == float('inf') or valid_count == 0:
            print(f"警告: 长度{i}的拼音组合没有有效概率，这不应该发生!")
            min_threshold = -50.0  # 极小的备用值
            min_threshold_pinyin = "(异常情况)"
        
        pinyin_length_thresholds.append(min_threshold)
        
        actual_length = i
        print(f"  长度{actual_length}: {len(pinyin_list)}个拼音, 最小阈值: {min_threshold:.6f} (拼音: '{min_threshold_pinyin}'), 有效数: {valid_count}, -inf数: {inf_count}")
    
    # 定义文件头格式 - 压缩为16字节  
    # magic(8) + version(4) + alphabet_size(4) = 16 字节
    header_format = '<8sLL'
    header_size = struct.calcsize(header_format)
    
    # 计算各部分偏移量
    current_offset = header_size
    unigram_offset = current_offset
    unigram_size = UNIGRAM_ARRAY_SIZE * 8  # uint64_t
    current_offset += unigram_size
    
    bigram_offset = current_offset
    bigram_size = BIGRAM_ARRAY_SIZE * 8
    current_offset += bigram_size
    
    trigram_offset = current_offset
    trigram_size = TRIGRAM_ARRAY_SIZE * 8
    current_offset += trigram_size
    
    # 拼音长度阈值偏移 = TRIGRAM_ARRAY_SIZE * 8 + header_size + unigram_size + bigram_size
    pinyin_length_thresholds_offset = current_offset
    pinyin_length_thresholds_size = (MAX_PINYIN_LENGTH - 2 + 1) * 8  # 长度2-MAX_PINYIN_LENGTH的double
    current_offset += pinyin_length_thresholds_size
    
    # 打包文件头
    header = struct.pack(header_format,
                        b'CHAR_LM\x00',  # magic（8 字节，含空终止符）
                        2,               # version (增加到2，支持阈值数据)
                        BASE_SIZE)       # alphabet_size（字母表大小）
    
    # 打包数据 - 使用64位无符号整数格式 Q 来避免溢出
    unigram_data = struct.pack(f'<{UNIGRAM_ARRAY_SIZE}Q', *unigram_array)
    bigram_data = struct.pack(f'<{BIGRAM_ARRAY_SIZE}Q', *bigram_array)
    trigram_data = struct.pack(f'<{TRIGRAM_ARRAY_SIZE}Q', *trigram_array)
    
    # 打包拼音长度阈值 - 使用double格式d，只包含长度2-MAX_PINYIN_LENGTH的阈值
    threshold_count = MAX_PINYIN_LENGTH - 2 + 1
    pinyin_length_thresholds_data = struct.pack(f'<{threshold_count}d', *pinyin_length_thresholds)
    
    # 写入文件
    with open(output_path, 'wb') as f:
        f.write(header)
        f.write(unigram_data)
        f.write(bigram_data)
        f.write(trigram_data)
        f.write(pinyin_length_thresholds_data)
    
    file_size = (header_size + unigram_size + bigram_size + trigram_size + 
                pinyin_length_thresholds_size)
    
    print(f"二进制文件写入完成:")
    print(f"  文件大小: {file_size} bytes ({file_size/1024:.1f} KB)")
    print(f"  文件头: {header_size} bytes")
    print(f"  Unigram数据: {unigram_size} bytes (偏移: {unigram_offset})")
    print(f"  Bigram数据: {bigram_size} bytes (偏移: {bigram_offset})")
    print(f"  Trigram数据: {trigram_size} bytes (偏移: {trigram_offset})")
    print(f"  拼音长度阈值: {pinyin_length_thresholds_size} bytes (偏移: {pinyin_length_thresholds_offset})")
    
    # 验证访问公式：只包含长度2-10的阈值
    print(f"\n访问公式验证:")
    print(f"  声母(索引0): trigram数组中的原始数据不被覆盖")
    print(f"  长度1: trigram数组中的原始数据不被覆盖")
    for length in range(2, MAX_PINYIN_LENGTH + 1):
        array_index = TRIGRAM_ARRAY_SIZE + length  # 直接使用length作为偏移
        file_offset = trigram_offset + trigram_size + (length - 2) * 8  # 文件中仍然是从0开始的
        print(f"  长度{length}: 数组索引={array_index}, 文件偏移={file_offset}")

def build_char_language_model(csv_filepath, lexicon_path, output_path):
    """
    主函数：构建字符语言模型
    """
    print("=" * 60)
    print("字符三元语言模型构建工具")
    print("用途: 为拼音纠错系统提供字母级别的概率支持")
    print("=" * 60)
    
    # 阶段1: 从语料库构建N-gram统计
    unigram_counts, bigram_counts, trigram_counts = build_char_ngram_from_corpus(csv_filepath)
    
    # 阶段2: 创建稠密数组
    unigram_array, bigram_array, trigram_array = create_dense_arrays(
        unigram_counts, bigram_counts, trigram_counts)
    
    # 阶段4: 从大词库提取合法拼音组合（用于阈值计算）
    print(f"使用词库提取合法拼音组合: {lexicon_path}")
    pinyin_sets = extract_pinyin_patterns_from_corpus(lexicon_path)

    # 阶段5: 写入二进制文件
    write_binary_model(unigram_array, bigram_array, trigram_array, pinyin_sets, output_path)
    
    print("\n" + "=" * 60)
    print("字符语言模型构建完成!")
    print(f"输出文件: {output_path}")
    print(f"每长度最小概率阈值已添加到文件（长度2-16）")
    print(f"C++访问方式:")
    print(f"  if (length < 2) {{ /* 使用trigram数组原始数据 */ }}")
    print(f"  else {{ threshold = thresholds[length - 2]; /* 长度2-10使用阈值数组 */ }}")
    print(f"注意: 声母(索引0)和长度1保留trigram数组原始数据，长度2-10使用独立的阈值数组")
    print(f"     文件中阈值数组: thresholds[0] = 长度2, thresholds[1] = 长度3, ..., thresholds[8] = 长度10")
    print("=" * 60)

if __name__ == '__main__':
    import argparse
    from paths import output_path
    parser=argparse.ArgumentParser(description="Build the existing character model from explicit sources")
    parser.add_argument('--input',required=True,help='CSV: pinyinstr,frequency')
    parser.add_argument('--lexicon',required=True,help='CSV: fullPinyin,frequency')
    parser.add_argument('--output',required=True)
    args=parser.parse_args()
    try:
        dest=output_path(args.output,[args.input,args.lexicon])
        build_char_language_model(args.input,args.lexicon,str(dest))
    except (ValueError,OSError) as error:parser.exit(1,str(error)+'\n')
