"""将自有 LightGBM 数值排序树导出为原生只读模型，不包含第三方权重。"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

FEATURE_VERSION = 1
FEATURE_NAMES = [
    'base_score', 'base_total', 'base_rank', 'base_delta_first', 'base_delta_second',
    'neural_total', 'neural_per_character', 'neural_per_letter', 'neural_rank', 'neural_delta_best',
    'neural_delta_reference', 'legacy_score', 'legacy_rank', 'legacy_delta_best', 'characters',
    'pinyin_letters', 'word_count', 'single_word_fraction', 'longest_word', 'mean_word_length',
    'pair_bonus_total', 'pair_hit_ratio', 'pair_word_coverage', 'approximate_path', 'neural_missing',
    'neural_available_count', 'base_best_gap', 'neural_best_gap', 'candidate_count',
    'base_neural_agree', 'context_characters', 'pair_model_available',
]
HEADER = struct.Struct('<8sIIQIIII32sII')
NODE = struct.Struct('<iiiIdd')

def inspect_model(path):
    path = Path(path)
    if not HEADER.size <= path.stat().st_size <= 2 * 1024 * 1024:
        raise ValueError('Invalid candidate ranker size')
    data = path.read_bytes()
    magic, version, header, size, features, schema, trees, count, checksum, flags, reserved = HEADER.unpack_from(data)
    if (magic != b'CRNK0001' or version != 1 or header != HEADER.size or size != len(data)
            or features != len(FEATURE_NAMES) or schema != FEATURE_VERSION or flags or reserved):
        raise ValueError('Incompatible candidate ranker header')
    if not 0 < trees <= 512 or not 0 < count <= 65536 or header + trees * 8 + count * NODE.size != size:
        raise ValueError('Invalid candidate ranker sections')
    if hashlib.sha256(data[header:]).digest() != checksum:
        raise ValueError('Candidate ranker checksum mismatch')
    nodes = list(NODE.iter_unpack(data[header + trees * 8:]))
    for feature, left, right, options, threshold, value in nodes:
        if not -1 <= feature < features or options > 3 or not math.isfinite(threshold) or not math.isfinite(value):
            raise ValueError('Invalid candidate ranker node')
        if feature == -1:
            if (left, right, options, threshold) != (-1, -1, 0, 0):raise ValueError('Invalid candidate ranker leaf')
        elif value != 0:raise ValueError('Invalid candidate ranker split value')
    next_node = 0
    for index in range(trees):
        start, length = struct.unpack_from('<II', data, header + index * 8)
        if start != next_node or not length or start + length > count:
            raise ValueError('Invalid candidate ranker tree bounds')
        seen = set(); pending = [(0, 0)]
        while pending:
            node, depth = pending.pop()
            if not 0 <= node < length or node in seen or depth > 64:
                raise ValueError('Invalid candidate ranker tree graph')
            seen.add(node)
            feature, left, right, *_ = nodes[start + node]
            if feature != -1:pending.extend([(left, depth + 1), (right, depth + 1)])
        if len(seen) != length:raise ValueError('Unreachable candidate ranker nodes')
        next_node += length
    if next_node != count:raise ValueError('Unreferenced candidate ranker nodes')
    return dict(format='CRNK0001', feature_version=schema, features=features, trees=trees,
                nodes=count, size=size, sha256=hashlib.sha256(data).hexdigest())

def export_model(model, path):
    if model.get('num_class', 1) != 1 or model.get('num_tree_per_iteration', 1) != 1 or model.get('average_output', False):
        raise ValueError('Only single-output additive ranking trees are supported')
    if model.get('objective', '').split()[0] != 'lambdarank':
        raise ValueError('Expected a LambdaRank model')
    if model.get('feature_names') != FEATURE_NAMES:
        raise ValueError('Feature names or order differ from feature version 1')
    nodes, trees = [], []
    for tree in model['tree_info']:
        local = []
        def visit(item, depth=0):
            if depth > 64:
                raise ValueError('Tree exceeds depth limit')
            index = len(local);local.append(None)
            if 'leaf_value' in item:
                if item.get('leaf_features') or item.get('leaf_coeff'):
                    raise ValueError('Linear leaves are not supported')
                node = (-1, -1, -1, 0, 0., float(item['leaf_value']))
            else:
                if item.get('decision_type') != '<=' or item.get('missing_type') not in ['None', 'NaN']:
                    raise ValueError('Unsupported split or missing-value mode')
                feature = int(item['split_feature'])
                if not 0 <= feature < len(FEATURE_NAMES):
                    raise ValueError('Feature index outside schema')
                left = visit(item['left_child'], depth+1);right = visit(item['right_child'], depth+1)
                flags = int(bool(item['default_left'])) | (2 if item['missing_type']=='NaN' else 0)
                node = (feature, left, right, flags, float(item['threshold']), 0.)
            if not all(math.isfinite(v) for v in node[4:]):
                raise ValueError('Non-finite tree value')
            local[index] = node
            return index
        visit(tree['tree_structure'])
        trees.append((len(nodes), len(local)));nodes.extend(local)
    if not 0 < len(trees) <= 512 or not 0 < len(nodes) <= 65536:
        raise ValueError('Model exceeds tree or node budget')
    payload = b''.join(struct.pack('<II', *tree) for tree in trees) + b''.join(NODE.pack(*node) for node in nodes)
    size = HEADER.size + len(payload)
    if size > 2*1024*1024:
        raise ValueError('Model exceeds 2 MiB budget')
    data = HEADER.pack(b'CRNK0001', 1, 80, size, len(FEATURE_NAMES), FEATURE_VERSION,
                       len(trees), len(nodes), hashlib.sha256(payload).digest(), 0, 0) + payload
    Path(path).write_bytes(data)
    return dict(bytes=len(data), trees=len(trees), nodes=len(nodes), sha256=hashlib.sha256(data).hexdigest())

def main():
    parser=argparse.ArgumentParser();parser.add_argument('json_model',type=Path);parser.add_argument('output',type=Path)
    args=parser.parse_args();print(json.dumps(export_model(json.loads(args.json_model.read_text(encoding='utf-8')),args.output)))

if __name__=='__main__':main()
