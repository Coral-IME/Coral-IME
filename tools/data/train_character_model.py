"""从自有 JSONL 语料随机初始化训练字符 GPT，并导出引擎可读取的模型。"""
import argparse
from collections import Counter
from dataclasses import asdict, dataclass
import hashlib
import json
import math
from pathlib import Path
import random
import struct
import subprocess
import time
import unicodedata
import tempfile

import torch
from torch import nn
from torch.nn import functional as functional


@dataclass
class ModelConfig:
    layers: int = 3
    width: int = 256
    heads: int = 4
    feedforward: int = 1024
    positions: int = 128
    epsilon: float = 1e-5

    def validate(self):
        if not (1 <= self.layers <= 8 and 1 <= self.width <= 512 and
                1 <= self.heads <= self.width and self.width % self.heads == 0 and
                1 <= self.feedforward <= 4096 and 2 <= self.positions <= 2048 and
                0 < self.epsilon <= .1):
            raise ValueError('architecture exceeds CCHLM001 limits')


class TransformerBlock(nn.Module):
    def __init__(self, config):
        super().__init__()
        self.heads = config.heads
        self.norm1 = nn.LayerNorm(config.width, eps=config.epsilon)
        self.qkv = nn.Linear(config.width, 3 * config.width)
        self.projection = nn.Linear(config.width, config.width)
        self.norm2 = nn.LayerNorm(config.width, eps=config.epsilon)
        self.expand = nn.Linear(config.width, config.feedforward)
        self.contract = nn.Linear(config.feedforward, config.width)

    def forward(self, value):
        batch, length, width = value.shape
        query, key, val = [part.reshape(batch, length, self.heads, width // self.heads).transpose(1, 2)
                           for part in self.qkv(self.norm1(value)).chunk(3, dim=-1)]
        attended = functional.scaled_dot_product_attention(query, key, val, dropout_p=0., is_causal=True)
        value = value + self.projection(attended.transpose(1, 2).contiguous().view(batch, length, width))
        return value + self.contract(functional.gelu(self.expand(self.norm2(value)), approximate='tanh'))


class CharacterGpt(nn.Module):
    def __init__(self, config, vocabulary_size):
        super().__init__()
        config.validate()
        if not 2 < vocabulary_size <= 32768:
            raise ValueError('vocabulary must contain 3..32768 tokens')
        self.config = config
        self.embedding = nn.Embedding(vocabulary_size, config.width)
        self.position = nn.Embedding(config.positions, config.width)
        self.blocks = nn.ModuleList([TransformerBlock(config) for _ in range(config.layers)])
        self.final_norm = nn.LayerNorm(config.width, eps=config.epsilon)
        self.apply(self.initialize)

    @staticmethod
    def initialize(module):
        if isinstance(module, (nn.Linear, nn.Embedding)):
            nn.init.normal_(module.weight, std=.02)
            if isinstance(module, nn.Linear):
                nn.init.zeros_(module.bias)

    def forward(self, tokens):
        positions = torch.arange(tokens.shape[1], device=tokens.device)
        value = self.embedding(tokens) + self.position(positions)
        for block in self.blocks:
            value = block(value)
        return functional.linear(self.final_norm(value), self.embedding.weight)


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def read_documents(path, maximum_chars):
    documents, identities, hashes = [], set(), set()
    total = 0
    with Path(path).open(encoding='utf-8-sig') as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            record = json.loads(line)
            text, identity = record['text'], record['doc_id']
            if not isinstance(text, str) or not isinstance(identity, str) or not identity:
                raise ValueError(f'{path}:{line_number}: invalid document')
            if not text or '\0' in text or any(0xd800 <= ord(char) <= 0xdfff for char in text):
                raise ValueError(f'{path}:{line_number}: invalid Unicode text')
            normalized = ''.join(unicodedata.normalize('NFKC', text).split())
            fingerprint = hashlib.sha256(normalized.encode()).hexdigest()
            if identity in identities or fingerprint in hashes:
                raise ValueError(f'{path}:{line_number}: duplicate document')
            identities.add(identity)
            hashes.add(fingerprint)
            documents.append(text)
            total += len(text)
            if total > maximum_chars:
                raise ValueError('corpus exceeds --max-chars; select a bounded training scope explicitly')
    if not documents:
        raise ValueError(f'{path}: empty corpus')
    return documents, identities, hashes


def make_examples(documents, vocabulary, positions):
    mapping = {chr(code): index for index, code in enumerate(vocabulary) if code <= 0x10ffff}
    examples = []
    known, total = 0, 0
    for document in documents:
        # 每个块独立以 BOS 开始；不把相邻文档拼接为一句，也不训练结束符。
        for start in range(0, len(document), positions):
            text = document[start:start + positions]
            ids = [mapping.get(char, 1) for char in text]
            total += len(ids)
            known += sum(index != 1 for index in ids)
            examples.append(([0] + ids[:-1], ids))
    return examples, known / total


def batch_examples(examples, indexes, device):
    selected = [examples[index] for index in indexes]
    length = max(len(pair[0]) for pair in selected)
    inputs = torch.ones((len(selected), length), dtype=torch.long)
    targets = torch.full_like(inputs, -100)
    for row, (source, target) in enumerate(selected):
        inputs[row, :len(source)] = torch.tensor(source)
        targets[row, :len(target)] = torch.tensor(target)
    return inputs.to(device), targets.to(device)


@torch.no_grad()
def evaluate(model, examples, batch_size, device):
    model.eval()
    loss_sum, tokens = 0., 0
    for start in range(0, len(examples), batch_size):
        source, target = batch_examples(examples, range(start, min(start + batch_size, len(examples))), device)
        loss_sum += functional.cross_entropy(model(source).flatten(0, 1), target.flatten(),
                                             ignore_index=-100, reduction='sum').item()
        tokens += (target != -100).sum().item()
    return loss_sum / tokens


def checkpoint(model, path, vocabulary, provenance, steps, validation_loss):
    # 只保存张量与基本类型，加载端始终使用 weights_only=True。
    torch.save({'config': asdict(model.config), 'vocabulary': vocabulary,
                'state_dict': {name: value.detach().cpu() for name, value in model.state_dict().items()},
                'provenance': provenance, 'steps': steps, 'validation_loss': validation_loss}, path)


def load_checkpoint(path):
    record = torch.load(path, map_location='cpu', weights_only=True)
    model = CharacterGpt(ModelConfig(**record['config']), len(record['vocabulary']))
    model.load_state_dict(record['state_dict'], strict=True)
    return model.eval(), record


def export_model(model, vocabulary, output, precision='fp32'):
    if precision not in ('fp32', 'int8'):
        raise ValueError('unsupported model precision')
    if precision == 'int8':
        from character_model_format import quantize_model
        with tempfile.TemporaryDirectory(prefix='coral-export-', dir=Path(output).parent) as directory:
            intermediate = Path(directory)/'float.coral'
            export_model(model, vocabulary, intermediate)
            quantize_model(intermediate, output)
        return
    config = model.config
    if (len(vocabulary) != model.embedding.num_embeddings or len(set(vocabulary)) != len(vocabulary) or
            vocabulary[:2] != [0x110000, 0x110001] or
            any(not isinstance(code, int) or not 1 <= code <= 0x10ffff or 0xd800 <= code <= 0xdfff
                for code in vocabulary[2:])):
        raise ValueError('invalid character vocabulary')
    expected_floats = (len(vocabulary) * config.width + config.positions * config.width + 2 * config.width +
                       config.layers * (4 * config.width**2 + 2 * config.width * config.feedforward +
                                        9 * config.width + config.feedforward))
    expected_bytes = 128 + 4 * len(vocabulary) + 4 * expected_floats
    if expected_bytes > 256 * 1024**2:
        raise ValueError('model exceeds the native file size limit')
    tensors = [model.embedding.weight, model.position.weight, model.final_norm.weight, model.final_norm.bias]
    for block in model.blocks:
        for layer in [block.norm1, block.qkv, block.projection, block.norm2, block.expand, block.contract]:
            tensors.extend([layer.weight, layer.bias])
    payload = struct.pack(f'<{len(vocabulary)}I', *vocabulary)
    for tensor in tensors:
        values = tensor.detach().cpu().float()
        if not torch.isfinite(values).all() or (values.abs() > 10000).any():
            raise ValueError('nonfinite or out-of-range weights')
        payload += values.numpy().astype('<f4', copy=False).tobytes()
    if len(payload) + 128 != expected_bytes:
        raise ValueError('tensor layout does not match CCHLM001')
    header = bytearray(128)
    struct.pack_into('<8sIIQ8IfI', header, 0, b'CCHLM001', 1, 128, 128 + len(payload),
                     config.layers, config.width, config.heads, config.feedforward,
                     len(vocabulary), config.positions, 0, 1, config.epsilon, 0)
    header[64:96] = hashlib.sha256(payload).digest()
    with Path(output).open('xb') as stream:
        stream.write(header)
        stream.write(payload)


@torch.no_grad()
def reference_score(model, vocabulary, context, text):
    mapping = {chr(code): index for index, code in enumerate(vocabulary) if code <= 0x10ffff}
    prefix = []
    for char in context[-32:]:
        if char not in mapping:
            prefix = []
        else:
            prefix.append(mapping[char])
    if not text or len(text) > 64 or any(char not in mapping for char in text):
        return float('nan')
    ids = [mapping[char] for char in text]
    tokens = [0] + prefix + ids[:-1]
    if len(tokens) > model.config.positions:
        return float('nan')
    logits = model(torch.tensor([tokens]))[0].log_softmax(-1)
    return sum(logits[len(prefix) + index, token].item() for index, token in enumerate(ids))


def verify(args):
    if not math.isfinite(args.tolerance) or args.tolerance <= 0:
        raise ValueError('tolerance must be positive and finite')
    model, record = load_checkpoint(args.checkpoint)
    cases = []
    with Path(args.cases).open(encoding='utf-8-sig') as stream:
        for line in stream:
            item = json.loads(line)
            context, text = item.get('context', ''), item['text']
            if any(char in context + text for char in '\t\r\n'):
                raise ValueError('probe cases cannot contain tab/newline')
            cases.append((context, text))
    if not cases:
        raise ValueError('empty parity cases')
    result = subprocess.run([str(Path(args.probe).resolve()), str(Path(args.model).resolve())],
                            input=''.join(f'{context}\t{text}\n' for context, text in cases),
                            encoding='utf-8', capture_output=True, check=True)
    actual = [float(line) for line in result.stdout.splitlines()]
    if len(actual) != len(cases):
        raise ValueError('probe returned wrong score count')
    errors = []
    finite_count = 0
    for (context, text), native in zip(cases, actual):
        expected = reference_score(model, record['vocabulary'], context, text)
        if math.isnan(expected) and math.isnan(native):
            continue
        if not math.isfinite(expected) or not math.isfinite(native):
            raise ValueError('native and training graph disagree on supported text')
        finite_count += 1
        errors.append(abs(native - expected))
        if abs(native - expected) > args.tolerance:
            raise ValueError(f'native score differs: {native} vs {expected}')
    if not finite_count:
        raise ValueError('parity requires supported candidate cases')
    print(json.dumps({'cases': len(cases), 'finite_cases': finite_count,
                      'maximum_absolute_error': max(errors), 'passed': True}))


def train(args):
    if (args.steps < 1 or args.batch_size < 1 or not 3 <= args.vocab_size <= 32768 or args.eval_every < 1 or
            not math.isfinite(args.learning_rate) or args.learning_rate <= 0 or args.max_chars < 1):
        raise ValueError('invalid training budget')
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=False)
    config = ModelConfig(args.layers, args.width, args.heads, args.feedforward, args.positions)
    config.validate()
    random.seed(args.seed)
    torch.manual_seed(args.seed)
    training, train_ids, train_hashes = read_documents(args.train, args.max_chars)
    validation, valid_ids, valid_hashes = read_documents(args.validation, args.max_chars)
    if train_ids & valid_ids or train_hashes & valid_hashes:
        raise ValueError('training and validation documents overlap')
    counts = Counter(char for text in training for char in text)
    vocabulary = [0x110000, 0x110001] + [ord(char) for char, _ in
                  sorted(counts.items(), key=lambda item: (-item[1], ord(item[0])))[:args.vocab_size - 2]]
    train_examples, train_coverage = make_examples(training, vocabulary, config.positions)
    valid_examples, valid_coverage = make_examples(validation, vocabulary, config.positions)
    provenance = {'initialization': 'random', 'teacher': None, 'seed': args.seed,
                  'train_sha256': digest(args.train), 'validation_sha256': digest(args.validation),
                  'train_documents': len(training), 'validation_documents': len(validation),
                  'train_chars': sum(map(len, training)), 'validation_chars': sum(map(len, validation)),
                  'train_coverage': train_coverage, 'validation_coverage': valid_coverage,
                  'torch_version': str(torch.__version__), 'script_sha256': digest(__file__),
                  'device': args.device, 'batch_size': args.batch_size, 'learning_rate': args.learning_rate}
    model = CharacterGpt(config, len(vocabulary)).to(args.device)
    provenance['parameters'] = sum(parameter.numel() for parameter in model.parameters())
    optimizer = torch.optim.AdamW(model.parameters(), lr=args.learning_rate, weight_decay=.01)
    rng = random.Random(args.seed)
    initial_loss = evaluate(model, valid_examples, args.batch_size, args.device)
    best = initial_loss
    checkpoint(model, output / 'best.pt', vocabulary, provenance, 0, best)
    started = time.monotonic()
    with (output / 'metrics.jsonl').open('x', encoding='utf-8') as metrics:
        metrics.write(json.dumps({'step': 0, 'validation_loss': best}) + '\n')
        for step in range(1, args.steps + 1):
            model.train()
            indexes = [rng.randrange(len(train_examples)) for _ in range(args.batch_size)]
            source, target = batch_examples(train_examples, indexes, args.device)
            logits = model(source)
            loss = functional.cross_entropy(logits.flatten(0, 1), target.flatten(), ignore_index=-100)
            if not torch.isfinite(loss):
                raise ValueError('nonfinite training loss')
            optimizer.zero_grad(set_to_none=True)
            loss.backward()
            nn.utils.clip_grad_norm_(model.parameters(), 1.)
            optimizer.step()
            if step % args.eval_every == 0 or step == args.steps:
                valid_loss = evaluate(model, valid_examples, args.batch_size, args.device)
                metric = {'step': step, 'train_loss': loss.item(), 'validation_loss': valid_loss,
                          'seconds': time.monotonic() - started}
                metrics.write(json.dumps(metric) + '\n')
                metrics.flush()
                print(json.dumps(metric), flush=True)
                if valid_loss < best:
                    best = valid_loss
                    checkpoint(model, output / 'best.pt', vocabulary, provenance, step, valid_loss)
    checkpoint(model, output / 'last.pt', vocabulary, provenance, args.steps, valid_loss)
    chosen, record = load_checkpoint(output / 'best.pt')
    export_model(chosen, vocabulary, output / 'character_model.coral')
    manifest = {'config': asdict(config), 'provenance': provenance, 'initial_validation_loss': initial_loss,
                'best_validation_loss': best, 'selected_step': record['steps'],
                'seconds': time.monotonic() - started, 'model_sha256': digest(output / 'character_model.coral')}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2, ensure_ascii=False), encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--threads', type=int, default=4)
    commands = parser.add_subparsers(dest='command', required=True)
    trainer = commands.add_parser('train')
    for name in ['train', 'validation', 'output']:
        trainer.add_argument('--' + name, required=True)
    for name, default in [('layers', 3), ('width', 256), ('heads', 4), ('feedforward', 1024),
                          ('positions', 128), ('vocab-size', 6000), ('steps', 10000),
                          ('batch-size', 32), ('eval-every', 500), ('seed', 20261023), ('max-chars', 10000000)]:
        trainer.add_argument('--' + name, type=int, default=default)
    trainer.add_argument('--learning-rate', type=float, default=3e-4)
    trainer.add_argument('--device', choices=['cpu', 'cuda'], default='cpu')
    exporter = commands.add_parser('export')
    exporter.add_argument('--checkpoint', required=True)
    exporter.add_argument('--output', required=True)
    exporter.add_argument('--precision', choices=['fp32', 'int8'], default='fp32')
    verifier = commands.add_parser('verify')
    for name in ['checkpoint', 'model', 'probe', 'cases']:
        verifier.add_argument('--' + name, required=True)
    verifier.add_argument('--tolerance', type=float, default=.005)
    args = parser.parse_args()
    if args.threads < 1:
        parser.error('--threads must be positive')
    torch.set_num_threads(args.threads)
    if args.command == 'train':
        train(args)
    elif args.command == 'export':
        model, record = load_checkpoint(args.checkpoint)
        export_model(model, record['vocabulary'], args.output, args.precision)
    else:
        verify(args)


if __name__ == '__main__':
    main()
