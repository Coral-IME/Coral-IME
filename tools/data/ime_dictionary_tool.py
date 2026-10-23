# -*- coding: utf-8 -*-
"""词库导出工具：微软拼音 UDL / 搜狗 SGPU / 微信 LevelDB。

直接运行打开窗口，--help 查看命令行用法。仅使用 Python 标准库。
格式研究来源：taxueseek/ime-lexicon、studyzy/imewlconverter。
音节编号表随本文件内置，参考资料与范围说明见 docs/other_ime_import.md。
"""
from __future__ import annotations
import argparse
import collections
import csv
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import queue
import re
import shutil
import struct
import sys
import tempfile
import threading
import time
import uuid

class FormatError(ValueError):
    """输入格式、完整性或一致性校验失败。"""


def require(condition, message):
    if not condition:
        raise FormatError(message)

MS_PINYIN = 'a ai an ang ao ba bai ban bang bao bei ben beng bi bian biao bie bin bing bo bu ca cai can cang cao ce cen ceng cha chai chan chang chao che chen cheng chi chong chou chu chua chuai chuan chuang chui chun chuo ci cong cou cu cuan cui cun cuo da dai dan dang dao de dei den deng di dia dian diao die ding diu dong dou du duan dui dun duo e ei en eng er fa fan fang fei fen feng fiao fo fou fu ga gai gan gang gao ge gei gen geng gong gou gu gua guai guan guang gui gun guo ha hai han hang hao he hei hen heng hong hou hu hua huai huan huang hui hun huo ji jia jian jiang jiao jie jin jing jiong jiu ju juan jue jun ka kai kan kang kao ke kei ken keng kong kou ku kua kuai kuan kuang kui kun kuo la lai lan lang lao le lei leng li lia lian liang liao lie lin ling liu lo long lou lu luan lue lun luo lv ma mai man mang mao me mei men meng mi mian miao mie min ming miu mo mou mu na nai nan nang nao ne nei nen neng ni nian niang niao nie nin ning niu nong nou nu nuan nue nun nuo nv o ou pa pai pan pang pao pei pen peng pi pian piao pie pin ping po pou pu qi qia qian qiang qiao qie qin qing qiong qiu qu quan que qun ran rang rao re ren reng ri rong rou ru rua ruan rui run ruo sa sai san sang sao se sen seng sha shai shan shang shao she shei shen sheng shi shou shu shua shuai shuan shuang shui shun shuo si song sou su suan sui sun suo ta tai tan tang tao te tei teng ti tian tiao tie ting tong tou tu tuan tui tun tuo wa wai wan wang wei wen weng wo wu xi xia xian xiang xiao xie xin xing xiong xiu xu xuan xue xun ya yan yang yao ye yi yin ying yo yong you yu yuan yue yun za zai zan zang zao ze zei zen zeng zha zhai zhan zhang zhao zhe zhei zhen zheng zhi zhong zhou zhu zhua zhuai zhuan zhuang zhui zhun zhuo zi zong zou zu zuan zui zun zuo'.split()

SOGOU_PINYIN = 'a ai an ang ao ba bai ban bang bao bei ben beng bi bian biao bie bin bing bo bu ca cai can cang cao ce cen ceng cha chai chan chang chao che chen cheng chi chong chou chu chua chuai chuan chuang chui chun chuo ci cong cou cu cuan cui cun cuo da dai dan dang dao de dei den deng di dia dian diao die ding diu dong dou du duan dui dun duo e ei en eng er fa fan fang fei fen feng fiao fo fou fu ga gai gan gang gao ge gei gen geng gong gou gu gua guai guan guang gui gun guo ha hai han hang hao he hei hen heng hong hou hu hua huai huan huang hui hun huo ji jia jian jiang jiao jie jin jing jiong jiu ju juan jue jun ka kai kan kang kao ke kei ken keng kong kou ku kua kuai kuan kuang kui kun kuo la lai lan lang lao le lei leng li lia lian liang liao lie lin ling liu lo long lou lu luan lve lun luo lv ma mai man mang mao me mei men meng mi mian miao mie min ming miu mo mou mu na nai nan nang nao ne nei nen neng ni nian niang niao nie nin ning niu nong nou nu nuan nve nun nuo nv o ou pa pai pan pang pao pei pen peng pi pian piao pie pin ping po pou pu qi qia qian qiang qiao qie qin qing qiong qiu qu quan que qun ran rang rao re ren reng ri rong rou ru rua ruan rui run ruo sa sai san sang sao se sen seng sha shai shan shang shao she shei shen sheng shi shou shu shua shuai shuan shuang shui shun shuo si song sou su suan sui sun suo ta tai tan tang tao te tei teng ti tian tiao tie ting tong tou tu tuan tui tun tuo wa wai wan wang wei wen weng wo wu xi xia xian xiang xiao xie xin xing xiong xiu xu xuan xue xun ya yan yang yao ye yi yin ying yo yong you yu yuan yue yun za zai zan zang zao ze zei zen zeng zha zhai zhan zhang zhao zhe zhei zhen zheng zhi zhong zhou zhu zhua zhuai zhuan zhuang zhui zhun zhuo zi zong zou zu zuan zui zun zuo a b c d e f g h i j k l m n o p q r s t u v w x y z 0 1 2 3 4 5 6 7 8 9 #'.split()

CRC_TABLE = []
for _i in range(256):
    _crc = _i
    for _ in range(8):
        _crc = (_crc >> 1) ^ (0x82f63b78 if _crc & 1 else 0)
    CRC_TABLE.append(_crc)


def u16(data, offset):
    return struct.unpack_from('<H', data, offset)[0]


def u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def varint(data, offset):
    value = 0
    for shift in range(0, 70, 7):
        if offset >= len(data):
            raise ValueError('Truncated varint')
        b = data[offset]
        offset += 1
        value |= (b & 127) << shift
        if b < 128:
            return (value, offset)
    raise ValueError('Overlong varint')


def snappy(data):
    expected, pos = varint(data, 0)
    out = bytearray()
    while pos < len(data):
        tag = data[pos]
        pos += 1
        typ = tag & 3
        if typ == 0:
            length = tag >> 2
            if length < 60:
                length += 1
            else:
                nbytes = length - 59
                length = int.from_bytes(data[pos:pos + nbytes], 'little') + 1
                pos += nbytes
            if not pos + length <= len(data):
                raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')
            out.extend(data[pos:pos + length])
            pos += length
        else:
            nbytes = {1: 1, 2: 2, 3: 4}[typ]
            if not pos + nbytes <= len(data):
                raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')
            distance = int.from_bytes(data[pos:pos + nbytes], 'little')
            pos += nbytes
            if typ == 1:
                distance |= (tag & 224) << 3
                length = 4 + (tag >> 2 & 7)
            else:
                length = 1 + (tag >> 2)
            if not 0 < distance <= len(out):
                raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')
            for _ in range(length):
                out.append(out[-distance])
        if not len(out) <= expected:
            raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')
    if not len(out) == expected:
        raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')
    return bytes(out)


def masked_crc32c(data):
    crc = 4294967295
    for b in data:
        crc = CRC_TABLE[(crc ^ b) & 255] ^ crc >> 8
    crc ^= 4294967295
    return (crc >> 15 | crc << 17) + 2726488792 & 4294967295


def block_entries(data):
    if not len(data) >= 4:
        raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')
    count = u32(data, len(data) - 4)
    end = len(data) - 4 - count * 4
    if not 0 <= end <= len(data) - 4:
        raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')
    restarts = [u32(data, end + i * 4) for i in range(count)]
    if not restarts == sorted(restarts):
        raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')
    pos = 0
    previous = b''
    while pos < end:
        start = pos
        shared, pos = varint(data, pos)
        unshared, pos = varint(data, pos)
        size, pos = varint(data, pos)
        if not (shared <= len(previous) and pos + unshared + size <= end):
            raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')
        if start in restarts:
            if not shared == 0:
                raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')
        key = previous[:shared] + data[pos:pos + unshared]
        pos += unshared
        value = data[pos:pos + size]
        pos += size
        previous = key
        yield (key, value, start)
    if not pos == end:
        raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')


def sst_records(path, stats):
    data = path.read_bytes()
    if not data[-8:] == bytes.fromhex('57fb808b247547db'):
        raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')

    def read_block(offset, length):
        if not offset + length + 5 <= len(data) - 48:
            raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')
        block = data[offset:offset + length]
        typ = data[offset + length]
        if not masked_crc32c(block + bytes([typ])) == u32(data, offset + length + 1):
            raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')
        stats['verified_blocks'] += 1
        stats['compression_' + str(typ)] += 1
        if typ == 0:
            return block
        if typ == 1:
            return snappy(block)
        raise ValueError('Unsupported SST compression ' + str(typ))
    footer = data[-48:]
    meta_offset, pos = varint(footer, 0)
    meta_size, pos = varint(footer, pos)
    index_offset, pos = varint(footer, pos)
    index_size, pos = varint(footer, pos)
    read_block(meta_offset, meta_size)
    index = read_block(index_offset, index_size)
    for _, handle, _ in block_entries(index):
        offset, pos = varint(handle, 0)
        size, pos = varint(handle, pos)
        if not pos == len(handle):
            raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')
        block = read_block(offset, size)
        for key, value, entry_offset in block_entries(block):
            if not len(key) >= 8:
                raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')
            packed = int.from_bytes(key[-8:], 'little')
            seq, typ = (packed >> 8, packed & 255)
            if not typ in (0, 1):
                raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')
            yield (key[:-8], value, seq, typ, offset, entry_offset)


def family(key):
    return key.split(b'!', 2)[1].decode('ascii', errors='replace') if key.startswith(b'!') else '(other)'


def log_records(data):
    """读取 LevelDB 物理日志，校验校验和并重组分片。"""
    pos = 0
    fragments = None
    while pos < len(data):
        remaining = 32768 - pos % 32768
        if remaining < 7:
            if not not any(data[pos:pos + remaining]):
                raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')
            pos += remaining
            continue
        if not pos + 7 <= len(data):
            raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')
        crc, size, typ = struct.unpack_from('<IHB', data, pos)
        if crc == size == typ == 0:
            if not not any(data[pos:min(len(data), pos + remaining)]):
                raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')
            pos += remaining
            continue
        if not (size + 7 <= remaining and pos + size + 7 <= len(data)):
            raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')
        payload = data[pos + 7:pos + 7 + size]
        if not masked_crc32c(bytes([typ]) + payload) == crc:
            raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')
        pos += 7 + size
        if typ == 1:
            if not fragments is None:
                raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')
            yield payload
        elif typ == 2:
            if not fragments is None:
                raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')
            fragments = bytearray(payload)
        elif typ in (3, 4):
            if not fragments is not None:
                raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')
            fragments.extend(payload)
            if typ == 4:
                yield bytes(fragments)
                fragments = None
        else:
            raise ValueError('Unknown log record type')
    if not fragments is None:
        raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')


def manifest_files(path):
    active = {}
    edits = 0
    fields = {}
    for record in log_records(path.read_bytes()):
        edits += 1
        pos = 0
        removed, added = ([], [])
        while pos < len(record):
            tag, pos = varint(record, pos)
            if tag == 1:
                size, pos = varint(record, pos)
                fields['comparator'] = record[pos:pos + size].decode('ascii')
                pos += size
            elif tag in (2, 3, 4, 9):
                fields[{2: 'log_number', 3: 'next_file_number', 4: 'last_sequence', 9: 'previous_log_number'}[tag]], pos = varint(record, pos)
            elif tag == 5:
                level, pos = varint(record, pos)
                size, pos = varint(record, pos)
                pos += size
            elif tag == 6:
                level, pos = varint(record, pos)
                number, pos = varint(record, pos)
                removed.append((level, number))
            elif tag == 7:
                level, pos = varint(record, pos)
                number, pos = varint(record, pos)
                file_size, pos = varint(record, pos)
                bounds = []
                for _ in range(2):
                    size, pos = varint(record, pos)
                    bounds.append(record[pos:pos + size].hex())
                    pos += size
                added.append(((level, number), dict(level=level, number=number, bytes=file_size, smallest_key_hex=bounds[0], largest_key_hex=bounds[1])))
            else:
                raise ValueError('Unsupported manifest tag: ' + str(tag))
            if not pos <= len(record):
                raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')
        for key in removed:
            active.pop(key, None)
        active.update(added)
    return (list(active.values()), dict(edits=edits, **fields))


def wal_operations(path):
    for record_number, data in enumerate(log_records(path.read_bytes())):
        if not len(data) >= 12:
            raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')
        sequence, count = struct.unpack_from('<QI', data, 0)
        pos = 12
        for i in range(count):
            if not pos < len(data):
                raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')
            typ = data[pos]
            pos += 1
            if not typ in (0, 1):
                raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')
            size, pos = varint(data, pos)
            if not pos + size <= len(data):
                raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')
            key = data[pos:pos + size]
            pos += size
            value = b''
            if typ == 1:
                size, pos = varint(data, pos)
                if not pos + size <= len(data):
                    raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')
                value = data[pos:pos + size]
                pos += size
            yield (key, value, sequence + i, typ, -1, record_number)
        if not pos == len(data):
            raise FormatError('数据校验失败：文件可能不完整、已损坏或版本不受支持。')


WORD_COLUMNS = ['word', 'pinyin', 'source_weight_raw_max', 'source_records']
META_COLUMNS = ['sequence', 'file', 'block_offset', 'entry_offset', 'value_hex', 'key_hex']


def file_hash(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def stable_read(path):
    data = path.read_bytes()
    require(hashlib.sha256(data).hexdigest() == file_hash(path),
            '读取期间词库发生了变化。请退出对应输入法后重试。')
    return data


class Exporter:
    def __init__(self, output, details=True, progress=None):
        self.output = Path(output)
        self.details = details
        self.progress = progress or (lambda message: None)
        self.files = []

    def tsv(self, name, rows, columns):
        count = 0
        with (self.output / name).open('w', encoding='utf-8-sig', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=columns, delimiter='\t', extrasaction='ignore')
            writer.writeheader()
            for row in rows:
                writer.writerow(row)
                count += 1
        self.files.append({'name': name, 'rows': count})

    def compact(self, ime, rows):
        grouped = {}
        for row in rows:
            key = row['word'], row['pinyin']
            if key not in grouped:
                grouped[key] = dict(word=key[0], pinyin=key[1], source_weight_raw_max='', source_records=0)
            item = grouped[key]
            item['source_records'] += 1
            if ime == 'sogou':
                item['source_weight_raw_max'] = max(item['source_weight_raw_max'] or 0, row['frequency_raw'])
        self.tsv(ime + '_migration.tsv', (grouped[key] for key in sorted(grouped)), WORD_COLUMNS)
        return len(grouped)

    def microsoft(self, path):
        self.progress('正在解析微软拼音词库…')
        data = stable_read(path)
        require(len(data) >= 0x2400 and data[:12] == bytes.fromhex('55aa88810200600055aa55aa'),
                '请选择微软拼音 ChsPinyinUDL.dat 自学习词库；暂不支持自定义短语导出或输入历史文件。')
        count = u32(data, 12)
        require(0x2400 + count * 60 <= len(data), '微软词库不完整：记录数超出文件长度。')
        rows = []
        for i in range(count):
            offset = 0x2400 + i * 60
            record = data[offset:offset + 60]
            size = record[10]
            require(0 < size <= 12, f'微软词库第 {i + 1} 条字长异常。')
            word = record[12:12 + size * 2].decode('utf-16le')
            indices = struct.unpack_from('<' + 'H' * size, record, 12 + size * 2)
            require(all(p < len(MS_PINYIN) for p in indices), f'微软词库第 {i + 1} 条读音编号不受支持。')
            rows.append(dict(word=word, pinyin=' '.join(MS_PINYIN[p] for p in indices),
                             attribute=hex(record[11]),
                             kind={0x5a: 'learned', 0xaa: 'user_defined'}.get(record[11], 'unknown'),
                             raw_u32_0=u32(record, 0), raw_prefix_hex=record[4:10].hex(),
                             pinyin_indices=','.join(map(str, indices)), offset=offset,
                             record_hex=record.hex()))
        pairs = self.compact('microsoft', rows)
        if self.details:
            self.tsv('microsoft_words.tsv', rows,
                     ['word', 'pinyin', 'attribute', 'kind', 'raw_u32_0', 'raw_prefix_hex', 'pinyin_indices', 'offset', 'record_hex'])
        return dict(parsed_records=count, word_pinyin_pairs=pairs,
                    unique_words=len({r['word'] for r in rows}), sha256=hashlib.sha256(data).hexdigest())

    def sogou(self, path):
        self.progress('正在解析搜狗拼音词库…')
        data = stable_read(path)
        require(len(data) >= 84 and data[:4] == b'SGPU',
                '请选择搜狗 sgim_gd_usr.bin（SGPU 格式）；暂不支持旧版加密 BIN、SCEL 或其他辅助词库。')
        index, capacity, count, start, total, used = struct.unpack_from('<6I', data, 0x38)
        require(index >= 84 and index + capacity <= start and start + total <= len(data)
                and count * 4 <= capacity and used <= total, '搜狗词库索引或数据区不完整。')
        rows = []
        for i in range(count):
            relative = u32(data, index + 4 * i)
            pos = start + relative
            require(relative < used and pos + 11 <= start + used, f'搜狗词库第 {i + 1} 条偏移异常。')
            py_bytes = u16(data, pos + 9)
            require(py_bytes % 2 == 0 and 0 < py_bytes <= 512, f'搜狗词库第 {i + 1} 条拼音长度异常。')
            py_start = pos + 11
            require(py_start + py_bytes + 4 <= start + used, '搜狗词库拼音数据被截断。')
            indices = struct.unpack_from('<' + 'H' * (py_bytes // 2), data, py_start)
            require(all(p < len(SOGOU_PINYIN) for p in indices), '搜狗词库包含不受支持的拼音编号。')
            payload_size = u16(data, py_start + py_bytes)
            word_bytes = u16(data, py_start + py_bytes + 2)
            word_start = py_start + py_bytes + 4
            require(word_bytes % 2 == 0 and 0 < word_bytes <= 512
                    and word_start + word_bytes + 2 <= start + used, '搜狗词库词语数据被截断或长度异常。')
            word = data[word_start:word_start + word_bytes].decode('utf-16le')
            tail = word_start + word_bytes
            secondary_bytes = u16(data, tail)
            require(secondary_bytes % 2 == 0 and secondary_bytes <= 512
                    and payload_size == word_bytes + secondary_bytes + 4
                    and tail + 2 + secondary_bytes <= start + used, '搜狗词库附加数据长度异常。')
            secondary = struct.unpack_from('<' + 'H' * (secondary_bytes // 2), data, tail + 2)
            rows.append(dict(word=word, pinyin=' '.join(SOGOU_PINYIN[p] for p in indices),
                             frequency_raw=u16(data, pos), raw_u32_2=u32(data, pos + 2),
                             flags_hex=data[pos+6:pos+9].hex(), index_number=i, offset=pos,
                             pinyin_indices=','.join(map(str, indices)),
                             secondary_indices=','.join(map(str, secondary)), secondary_matches=indices == secondary))
        pairs = self.compact('sogou', rows)
        if self.details:
            self.tsv('sogou_personal_words.tsv', rows,
                     ['word', 'pinyin', 'frequency_raw', 'raw_u32_2', 'flags_hex', 'index_number', 'offset',
                      'pinyin_indices', 'secondary_indices', 'secondary_matches'])
        return dict(parsed_records=count, word_pinyin_pairs=pairs,
                    unique_words=len({r['word'] for r in rows}), sha256=hashlib.sha256(data).hexdigest())

    def wechat(self, directory, recovery=False):
        self.progress('正在读取微信词库清单…')
        warnings = []
        manifest_info = None
        current = None
        if (directory / 'CURRENT').exists():
            current = (directory / 'CURRENT').read_text(encoding='ascii').strip()
            require(bool(re.fullmatch(r'MANIFEST-\d+', current)), '微信 CURRENT 内容异常。')
            manifest_path = directory / current
            if manifest_path.exists():
                active, manifest_info = manifest_files(manifest_path)
                require(manifest_info.get('comparator') == 'leveldb.BytewiseComparator',
                        '此微信词库使用了不受支持的排序方式。')
                manifest_info['sha256'] = file_hash(manifest_path)
                manifest_info['active_files'] = active
        if manifest_info is None:
            require(recovery, '微信词库缺少 CURRENT 或其指向的 MANIFEST。请选择完整文件夹；也可勾选“不完整副本恢复”。')
            warnings.append('缺少有效清单：扫描了现有数据文件，结果可能包含旧词条，无法确认当前有效状态。')
            paths = sorted([*directory.glob('*.ldb'), *directory.glob('*.sst')])
        else:
            paths = []
            for item in sorted(active, key=lambda f: f['number']):
                stem = '%06d' % item['number']
                path = directory / (stem + '.ldb')
                if not path.exists():
                    path = directory / (stem + '.sst')
                if not path.exists():
                    require(recovery, f'微信词库缺少清单引用的数据文件 {stem}.ldb。请复制完整目录。')
                    warnings.append(f'缺少数据文件 {stem}.ldb，恢复结果不完整。')
                    continue
                require(path.stat().st_size == item['bytes'], f'微信数据文件 {path.name} 大小与清单不一致。')
                paths.append(path)
        require(bool(paths) or manifest_info is not None, '所选微信文件夹中没有可解析的数据表。')
        latest = {}
        stats = collections.Counter()
        physical = collections.Counter()

        def merge(path, iterator):
            for key, value, seq, typ, offset, entry_offset in iterator:
                physical[family(key)] += 1
                old = latest.get(key)
                if old is None or seq > old[1]:
                    latest[key] = (value, seq, typ, path.name, offset, entry_offset)
                elif seq == old[1]:
                    require(value == old[0] and typ == old[2], '微信数据包含相同序列号的冲突记录。')

        for i, path in enumerate(paths):
            self.progress(f'正在校验微信词库 {i + 1}/{len(paths)}：{path.name}')
            merge(path, sst_records(path, stats))
        if manifest_info is not None:
            number = manifest_info.get('log_number', 0)
            previous = manifest_info.get('previous_log_number', 0)
            for required in {n for n in (number, previous) if n}:
                name = '%06d.log' % required
                if not (directory / name).exists():
                    require(recovery, f'微信词库缺少日志 {name}。请退出输入法后复制完整目录，或选择不完整副本恢复。')
                    warnings.append(f'缺少日志 {name}：可能遗漏最后尚未写入数据表的记录。')
            logs = [p for p in directory.glob('*.log') if p.stem.isdigit()
                    and (int(p.stem) >= number or int(p.stem) == previous)]
        else:
            logs = [p for p in directory.glob('*.log') if p.stem.isdigit()]
        for path in sorted(logs):
            self.progress('正在校验微信日志：' + path.name)
            merge(path, wal_operations(path))
        rows, prefix, bigrams = [], [], []
        deleted = 0
        for key, (value, seq, typ, name, offset, entry_offset) in sorted(latest.items()):
            if typ == 0:
                deleted += 1
                continue
            common = dict(sequence=seq, file=name, block_offset=offset, entry_offset=entry_offset,
                          value_hex=value.hex(), key_hex=key.hex())
            if key.startswith(b'!u_d_v_p!'):
                parts = key[9:].split(b'\x01')
                require(len(parts) == 2, '微信个人词索引格式不受支持。')
                rows.append(dict(word=parts[1].decode('utf-8'), pinyin=parts[0].decode('ascii').replace(',', ' '), **common))
            elif key.startswith(b'!user_prefix2!'):
                word = key[len(b'!user_prefix2!'):].split(b'\x7f', 1)[-1].decode('utf-8')
                prefix.append(dict(word=word, **common))
            elif key.startswith(b'!user_bigram2!'):
                parts = key[len(b'!user_bigram2!'):].split(b'\x7f')
                require(len(parts) in (2, 3), '微信搭配索引格式不受支持。')
                text = [part.decode('utf-8') for part in parts]
                bigrams.append(dict(previous_word=text[0], previous_pinyin=text[1] if len(text) == 3 else '',
                                    next_word=text[-1], **common))
        pairs = self.compact('wechat', rows)
        self.tsv('wechat_prefix_compact.tsv', ({'word': w} for w in sorted({r['word'] for r in prefix})), ['word'])
        self.tsv('wechat_bigrams_compact.tsv', bigrams, ['previous_word', 'previous_pinyin', 'next_word'])
        if self.details:
            self.tsv('wechat_personal_words.tsv', rows, ['word', 'pinyin', *META_COLUMNS])
            self.tsv('wechat_prefix_words.tsv', prefix, ['word', *META_COLUMNS])
            self.tsv('wechat_bigrams.tsv', bigrams, ['previous_word', 'previous_pinyin', 'next_word', *META_COLUMNS])
        return dict(parsed_records=len(rows), word_pinyin_pairs=pairs, unique_words=len({r['word'] for r in rows}),
                    prefix_records=len(prefix), unique_prefix_words=len({r['word'] for r in prefix}),
                    bigram_records=len(bigrams), unique_context_word_pairs=len({(r['previous_word'], r['next_word']) for r in bigrams}),
                    verified_blocks=stats['verified_blocks'], checks=dict(stats), deleted_keys_excluded=deleted,
                    current_manifest=current, manifest=manifest_info, warnings=warnings,
                    maximum_observed_sequence=max((v[1] for v in latest.values()), default=0))


def resolve_wechat_dir(path):
    def looks_like_db(p):
        return p.is_dir() and ((p / 'CURRENT').exists() or any(p.glob('*.ldb')) or any(p.glob('*.sst')))
    require(path.is_dir(), '微信输入应为词库文件夹。')
    if looks_like_db(path):
        return path
    matches = [p for p in path.iterdir() if looks_like_db(p)]
    require(len(matches) == 1, '请选择包含 CURRENT、MANIFEST 和 .ldb 文件的微信账号词库文件夹。')
    return matches[0]


def snapshot_wechat(source, destination, progress):
    destination.mkdir()
    files = sorted(p for p in source.iterdir() if p.is_file() and
                   (p.name == 'CURRENT' or re.fullmatch(r'MANIFEST-\d+|\d+\.(?:ldb|sst|log)', p.name)))
    require(bool(files), '微信目录中没有词库文件。')
    initial_names = {p.name for p in files}
    hashes = {}
    for p in files:
        progress('正在复制微信词库快照：' + p.name)
        before = file_hash(p)
        shutil.copyfile(p, destination / p.name)
        require(before == file_hash(destination / p.name) == file_hash(p),
                '微信词库在复制期间发生变化。请完全退出微信输入法后重试。')
        hashes[p.name] = before
    current_files = {p.name for p in source.iterdir() if p.is_file() and
                     (p.name == 'CURRENT' or re.fullmatch(r'MANIFEST-\d+|\d+\.(?:ldb|sst|log)', p.name))}
    require(current_files == initial_names and all(file_hash(source / n) == digest for n, digest in hashes.items()),
            '微信词库在复制期间发生变化。请完全退出微信输入法后重试。')
    return hashes


def cleanup_owned(path, parent):
    path = Path(path)
    require(not path.is_symlink() and path.resolve().parent == Path(parent).resolve(), '临时目录位置异常，已停止清理。')
    if path.exists():
        shutil.rmtree(path)


def publish_directory(stage, final):
    # Windows 索引或防护程序可能在关闭文件后短暂持有目录句柄。
    for attempt in range(6):
        try:
            stage.rename(final)
            return
        except PermissionError as error:
            if getattr(error, 'winerror', None) not in (5, 32, 33) or attempt == 5:
                raise
            time.sleep(0.05 * 2 ** attempt)


def convert_tsv_files(paths, destination):
    """按命名列合并主词表，保留读音；源权重和上下文索引不写入珊瑚词库。"""
    words = set()
    for path in paths:
        path = Path(path)
        require(path.stat().st_size <= 64 * 1024 * 1024, '单个 TSV 超过 64 MB，请拆分后导入。')
        with path.open(encoding='utf-8-sig', newline='') as stream:
            reader = csv.DictReader(stream, delimiter='\t')
            fields = reader.fieldnames or []
            require(fields.count('word') == 1 and fields.count('pinyin') <= 1,
                    '请选择包含 word 列的词表；上下文搭配表不能转换为普通词库。')
            for row in reader:
                require(None not in row and all(v is not None for v in row.values()), 'TSV 列数与表头不一致。')
                word, pinyin = row['word'].strip(), row.get('pinyin', '').strip()
                require(word and not any(c in word + pinyin for c in '\r\n\t'), '词表含空词语或跨行字段，请检查后重试。')
                words.add((word, pinyin))
    with Path(destination).open('w', encoding='utf-8-sig', newline='') as stream:
        writer = csv.writer(stream, delimiter='\t')
        writer.writerow(['word', 'pinyin'])
        writer.writerows(sorted(words))
    return len(words)


def send_coral_import(receiver, expected_pid, sender, path):
    """同步交付文件位置；珊瑚只打开预览，由用户最后确认写入。"""
    require(sys.platform == 'win32', '此入口仅支持 Windows 珊瑚输入法。')
    import ctypes
    from ctypes import wintypes
    user = ctypes.WinDLL('user32', use_last_error=True)
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.GetWindowThreadProcessId.restype = wintypes.DWORD
    actual = wintypes.DWORD()
    require(user.GetWindowThreadProcessId(receiver, ctypes.byref(actual)) and actual.value == expected_pid,
            '珊瑚设置窗口已关闭。请重新打开“导入词库”，选择导出目录中的 coral_import.tsv。')

    class CopyData(ctypes.Structure):
        _fields_ = [('dwData', ctypes.c_size_t), ('cbData', wintypes.DWORD), ('lpData', ctypes.c_void_p)]
    payload = ctypes.create_unicode_buffer(str(Path(path).resolve()))
    data = CopyData(0x43494D31, ctypes.sizeof(payload), ctypes.cast(payload, ctypes.c_void_p))
    user.SendMessageTimeoutW.argtypes = [wintypes.HWND, wintypes.UINT, ctypes.c_size_t, ctypes.c_ssize_t,
                                       wintypes.UINT, wintypes.UINT, ctypes.POINTER(ctypes.c_size_t)]
    user.SendMessageTimeoutW.restype = ctypes.c_ssize_t
    result = ctypes.c_size_t()
    require(user.SendMessageTimeoutW(receiver, 0x004A, sender, ctypes.addressof(data), 2, 3000,
                                     ctypes.byref(result)) and result.value,
            '未能交回珊瑚设置窗口。可在“导入词库”中手动选择 coral_import.tsv。')


def export_dictionaries(inputs, output, details=True, recovery=False, progress=None):
    """窗口和命令行共用入口；全部成功后才提交新的结果目录。"""
    progress = progress or (lambda message: None)
    require(bool(inputs), '请至少选择一种输入法的词库。')
    require(set(inputs) <= {'microsoft', 'wechat', 'sogou'}, '不支持的输入法类型。')
    paths = {name: Path(value).expanduser().resolve() for name, value in inputs.items() if str(value).strip()}
    require(bool(paths), '请至少选择一种输入法的词库。')
    for name in ('microsoft', 'sogou'):
        if name in paths:
            require(paths[name].is_file(), ('微软' if name == 'microsoft' else '搜狗') + '词库文件不存在。')
    if 'wechat' in paths:
        paths['wechat'] = resolve_wechat_dir(paths['wechat'])
    root = Path(output).expanduser().resolve()
    if 'wechat' in paths:
        require(root != paths['wechat'] and paths['wechat'] not in root.parents,
                '输出文件夹不能放在微信原始词库目录内，请选择其他位置。')
    root.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix='.ime-export-', dir=root))
    try:
        exporter = Exporter(stage, details, progress)
        report = dict(tool_version='1.0', created_at=datetime.now().astimezone().isoformat(),
                      inputs={k: str(v) for k, v in paths.items()}, details=details, results={})
        for name in ('microsoft', 'wechat', 'sogou'):
            if name not in paths:
                continue
            if name == 'wechat':
                snapshot = stage / '.wechat-input'
                try:
                    hashes = snapshot_wechat(paths[name], snapshot, progress)
                    result = exporter.wechat(snapshot, recovery)
                    result['source_sha256'] = hashes
                finally:
                    cleanup_owned(snapshot, stage)
            else:
                result = getattr(exporter, name)(paths[name])
            report['results'][name] = result
        primary = [stage / f'{name}_migration.tsv' for name in report['results']]
        report['coral_word_pinyin_pairs'] = convert_tsv_files(primary, stage / 'coral_import.tsv')
        exporter.files.append({'name': 'coral_import.tsv', 'rows': report['coral_word_pinyin_pairs']})
        report['files'] = exporter.files
        (stage / 'export_report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
        final = root / ('词库导出-' + datetime.now().strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:6])
        publish_directory(stage, final)
        report['output_directory'] = str(final)
        progress('导出完成。')
        return report
    finally:
        cleanup_owned(stage, root)


def friendly_error(error):
    if isinstance(error, FormatError):
        return str(error)
    if isinstance(error, PermissionError):
        return '无法读取词库或写入输出位置。请退出对应输入法，并选择有权限的文件夹。'
    if isinstance(error, (UnicodeError, struct.error, IndexError)):
        return '词库数据不完整或版本不受支持。请检查所选文件，并重新复制完整词库。'
    if isinstance(error, FileNotFoundError):
        return '文件已移动或不存在，请重新选择。'
    return f'导出失败：{error}'


class DictionaryApp:
    def __init__(self, root, coral_window=0, coral_pid=0, default_output=None):
        import tkinter as tk
        from tkinter import ttk
        self.tk, self.ttk, self.root = tk, ttk, root
        self.events = queue.Queue()
        self.running = False
        self.last_output = None
        self.coral_window, self.coral_pid = coral_window, coral_pid
        self.controls = []
        root.title('词库导出工具')
        root.geometry(f'860x{min(850, root.winfo_screenheight() - 100)}')
        root.minsize(720, 580)
        root.configure(background='#f4f6fa')
        style = ttk.Style(root)
        if 'clam' in style.theme_names():
            style.theme_use('clam')
        style.configure('.', font=('Microsoft YaHei UI', 10))
        style.configure('TFrame', background='#f4f6fa')
        style.configure('TLabel', background='#f4f6fa', foreground='#243247')
        style.configure('Title.TLabel', font=('Microsoft YaHei UI', 20, 'bold'))
        style.configure('Hint.TLabel', foreground='#627186', font=('Microsoft YaHei UI', 9))
        style.configure('TButton', padding=(14, 8))
        style.configure('Accent.TButton', background='#2864d7', foreground='white', padding=(20, 10))
        style.map('Accent.TButton', background=[('active', '#1e51b2'), ('disabled', '#98aac7')])
        style.configure('TCheckbutton', background='#f4f6fa')
        shell = ttk.Frame(root)
        shell.pack(fill='both', expand=True)
        canvas = tk.Canvas(shell, background='#f4f6fa', highlightthickness=0)
        scrollbar = ttk.Scrollbar(shell, orient='vertical', command=canvas.yview)
        scrollbar.pack(side='right', fill='y')
        canvas.pack(side='left', fill='both', expand=True)
        canvas.configure(yscrollcommand=scrollbar.set)
        outer = ttk.Frame(canvas, padding=26)
        body = canvas.create_window((0, 0), window=outer, anchor='nw')
        outer.bind('<Configure>', lambda event: canvas.configure(scrollregion=canvas.bbox('all')))
        canvas.bind('<Configure>', lambda event: canvas.itemconfigure(body, width=event.width))
        outer.columnconfigure(1, weight=1)
        ttk.Label(outer, text='把个人词库带出来', style='Title.TLabel').grid(row=0, column=0, columnspan=3, sticky='w')
        ttk.Label(outer, text='选择一种或多种输入法的词库，导出为通用 TSV 文件。', style='Hint.TLabel').grid(row=1, column=0, columnspan=3, sticky='w', pady=(5, 22))
        self.inputs = {}
        labels = [('microsoft', '微软拼音', 'ChsPinyinUDL.dat · 自学习词库文件'),
                  ('sogou', '搜狗拼音', 'sgim_gd_usr.bin · SGPU 格式文件'),
                  ('wechat', '微信输入法', '选择包含 CURRENT、MANIFEST 和 .ldb 的完整文件夹')]
        row = 2
        for name, label, hint in labels:
            var = tk.StringVar()
            self.inputs[name] = var
            ttk.Label(outer, text=label).grid(row=row, column=0, sticky='w', padx=(0, 16))
            entry = ttk.Entry(outer, textvariable=var)
            entry.grid(row=row, column=1, sticky='ew', padx=(0, 10))
            button = ttk.Button(outer, text='选择文件夹' if name == 'wechat' else '选择文件', command=lambda n=name: self.browse(n))
            button.grid(row=row, column=2, sticky='ew')
            self.controls.extend([entry, button])
            ttk.Label(outer, text=hint, style='Hint.TLabel').grid(row=row+1, column=1, columnspan=2, sticky='w', pady=(4, 14))
            row += 2
        ttk.Separator(outer).grid(row=row, column=0, columnspan=3, sticky='ew', pady=(0, 18))
        row += 1
        self.output = tk.StringVar(value=str(default_output or Path(__file__).resolve().parent / '词库导出'))
        ttk.Label(outer, text='保存到').grid(row=row, column=0, sticky='w')
        entry = ttk.Entry(outer, textvariable=self.output)
        entry.grid(row=row, column=1, sticky='ew', padx=(0, 10))
        button = ttk.Button(outer, text='选择位置', command=self.browse_output)
        button.grid(row=row, column=2, sticky='ew')
        self.controls.extend([entry, button])
        row += 1
        ttk.Label(outer, text='每次新建结果文件夹，保留已有导出。微信输入法请先完全退出。', style='Hint.TLabel').grid(row=row, column=1, columnspan=2, sticky='w', pady=(5, 14))
        row += 1
        self.details = tk.BooleanVar(value=True)
        self.recovery = tk.BooleanVar(value=False)
        for text, variable in [('同时导出详细记录（原始字段与来源位置）', self.details),
                               ('允许从不完整的微信副本恢复（结果可能包含旧记录或遗漏词条）', self.recovery)]:
            checkbox = ttk.Checkbutton(outer, text=text, variable=variable)
            checkbox.grid(row=row, column=0, columnspan=3, sticky='w', pady=3)
            self.controls.append(checkbox)
            row += 1
        actions = ttk.Frame(outer)
        actions.grid(row=row, column=0, columnspan=3, sticky='ew', pady=(18, 12))
        self.start_button = ttk.Button(actions, text='开始导出', style='Accent.TButton', command=self.start)
        self.start_button.pack(side='left')
        self.controls.append(self.start_button)
        self.open_button = ttk.Button(actions, text='打开结果文件夹', command=self.open_output, state='disabled')
        self.open_button.pack(side='left', padx=12)
        self.import_button = None
        if coral_window:
            self.import_button = ttk.Button(actions, text='在珊瑚中预览导入', command=self.import_coral, state='disabled')
            self.import_button.pack(side='left')
        row += 1
        self.progressbar = ttk.Progressbar(outer, mode='indeterminate')
        self.progressbar.grid(row=row, column=0, columnspan=3, sticky='ew')
        row += 1
        self.status = tk.StringVar(value='准备就绪 · 所有解析均在本机完成')
        ttk.Label(outer, textvariable=self.status, wraplength=720).grid(row=row, column=0, columnspan=3, sticky='w', pady=(10, 6))
        row += 1
        self.result_box = tk.Text(outer, height=5, wrap='word', relief='flat', background='#ffffff',
                                  foreground='#243247', font=('Microsoft YaHei UI', 9), padx=12, pady=8)
        self.result_box.grid(row=row, column=0, columnspan=3, sticky='nsew')
        self.result_box.configure(state='disabled')
        outer.rowconfigure(row, weight=1)
        root.protocol('WM_DELETE_WINDOW', self.close)
        root.after(100, self.poll)

    def browse(self, name):
        from tkinter import filedialog
        if name == 'wechat':
            value = filedialog.askdirectory(parent=self.root, title='选择微信账号词库文件夹')
        else:
            value = filedialog.askopenfilename(parent=self.root, title='选择词库文件',
                                               filetypes=[('词库文件', '*.dat *.bin'), ('所有文件', '*.*')])
        if value:
            self.inputs[name].set(value)

    def browse_output(self):
        from tkinter import filedialog
        value = filedialog.askdirectory(parent=self.root, title='选择导出位置')
        if value:
            self.output.set(value)

    def show_result(self, text):
        self.result_box.configure(state='normal')
        self.result_box.delete('1.0', 'end')
        self.result_box.insert('1.0', text)
        self.result_box.configure(state='disabled')

    def start(self):
        from tkinter import messagebox
        if self.running:
            return
        inputs = {name: variable.get().strip() for name, variable in self.inputs.items() if variable.get().strip()}
        output = self.output.get().strip()
        if not inputs or not output:
            messagebox.showinfo('请选择文件', '请至少选择一份词库，并填写保存位置。', parent=self.root)
            return
        details, recovery = self.details.get(), self.recovery.get()
        self.running = True
        self.last_output = None
        self.open_button.configure(state='disabled')
        if self.import_button:
            self.import_button.configure(state='disabled')
        for control in self.controls:
            control.configure(state='disabled')
        self.show_result('')
        self.status.set('准备读取词库…')
        self.progressbar.start(12)

        def work():
            try:
                result = export_dictionaries(inputs, output, details, recovery,
                                             lambda message: self.events.put(('progress', message)))
                self.events.put(('done', result))
            except Exception as error:
                self.events.put(('error', friendly_error(error)))
        threading.Thread(target=work, daemon=True).start()

    def poll(self):
        try:
            while True:
                event, value = self.events.get_nowait()
                if event == 'progress':
                    self.status.set(value)
                else:
                    self.running = False
                    self.progressbar.stop()
                    for control in self.controls:
                        control.configure(state='normal')
                    if event == 'done':
                        self.last_output = value['output_directory']
                        self.open_button.configure(state='normal')
                        if self.import_button:
                            self.import_button.configure(state='normal')
                        self.status.set(f'导出完成 · {len(value["files"])} 个 TSV 文件')
                        labels = {'microsoft': '微软拼音', 'wechat': '微信输入法', 'sogou': '搜狗拼音'}
                        lines = []
                        for name, result in value['results'].items():
                            lines.append(f'{labels[name]}：{result["word_pinyin_pairs"]:,} 条去重词语＋拼音')
                            lines.extend('提示：' + warning for warning in result.get('warnings', []))
                        lines.append('保存位置：' + self.last_output)
                        self.show_result('\n'.join(lines))
                    else:
                        self.status.set('未完成导出，请按提示调整后重试。')
                        self.show_result(value)
        except queue.Empty:
            pass
        self.root.after(100, self.poll)

    def open_output(self):
        if self.last_output and Path(self.last_output).is_dir():
            if sys.platform == 'win32':
                os.startfile(self.last_output)
            else:
                import webbrowser
                webbrowser.open(Path(self.last_output).as_uri())

    def import_coral(self):
        from tkinter import messagebox
        if not self.last_output or self.running:
            return
        try:
            import ctypes
            from ctypes import wintypes
            user = ctypes.WinDLL('user32', use_last_error=True)
            user.GetAncestor.argtypes = [wintypes.HWND, wintypes.UINT]
            user.GetAncestor.restype = wintypes.HWND
            sender = user.GetAncestor(self.root.winfo_id(), 2)
            send_coral_import(self.coral_window, self.coral_pid, sender, Path(self.last_output) / 'coral_import.tsv')
            self.root.after(100, self.root.destroy)
        except Exception as error:
            messagebox.showinfo('导入珊瑚', friendly_error(error), parent=self.root)

    def close(self):
        if self.running:
            from tkinter import messagebox
            messagebox.showinfo('正在导出', '请等待当前导出完成后再关闭窗口。', parent=self.root)
        else:
            self.root.destroy()


def main(argv=None):
    parser = argparse.ArgumentParser(description='微软 / 搜狗 / 微信词库导出工具。不带参数打开窗口。')
    parser.add_argument('--microsoft', type=Path, help='ChsPinyinUDL.dat 文件')
    parser.add_argument('--sogou', type=Path, help='sgim_gd_usr.bin 文件（SGPU）')
    parser.add_argument('--wechat', type=Path, help='微信完整词库文件夹')
    parser.add_argument('--output', type=Path, help='导出位置；每次新建结果文件夹')
    parser.add_argument('--compact-only', action='store_true', help='只导出简表')
    parser.add_argument('--recover-wechat', action='store_true', help='允许微信清单或日志缺失；结果会标注限制')
    parser.add_argument('--coral-window', type=int, default=0, help=argparse.SUPPRESS)
    parser.add_argument('--coral-pid', type=int, default=0, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    inputs = {name: getattr(args, name) for name in ('microsoft', 'sogou', 'wechat') if getattr(args, name)}
    if inputs:
        try:
            result = export_dictionaries(inputs, args.output or Path(__file__).resolve().parent / '词库导出',
                                         not args.compact_only, args.recover_wechat, print)
            print('保存位置：' + result['output_directory'])
            return 0
        except Exception as error:
            print(friendly_error(error), file=sys.stderr)
            return 1
    if any((args.output, args.compact_only, args.recover_wechat)) and not args.coral_window:
        parser.error('使用命令行选项时，请至少提供 --microsoft、--sogou 或 --wechat。')
    try:
        import tkinter as tk
        root = tk.Tk()
    except ImportError:
        print('当前 Python 未安装 Tkinter。Windows 请在 Python 安装器中启用 Tcl/Tk，或使用 --help 查看命令行导出方式。', file=sys.stderr)
        return 1
    DictionaryApp(root, args.coral_window, args.coral_pid, args.output)
    root.mainloop()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
