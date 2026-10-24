"""训练一个随机初始化的小网络，以真实权重检查训练、导出和原生推理。"""
import argparse
import json
from pathlib import Path
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/data'))
import train_character_model as training


def main():
    training.torch.set_num_threads(2)
    with tempfile.TemporaryDirectory(prefix='coral-character-training-') as temporary:
        root = Path(temporary)
        train = root / 'train.jsonl'
        validation = root / 'validation.jsonl'
        train.write_text('\n'.join(json.dumps({'doc_id': f'train-{i}',
                         'text': '今天可以写了这样的组合是加不完的' * (i + 1)}, ensure_ascii=False)
                         for i in range(8)), encoding='utf-8')
        validation.write_text(json.dumps({'doc_id': 'validation', 'text': '今天这样的组合可以写了新'},
                                        ensure_ascii=False), encoding='utf-8')
        args = argparse.Namespace(train=str(train), validation=str(validation), output=str(root/'trained'),
            layers=2, width=32, heads=4, feedforward=64, positions=128, seed=7919,
            steps=60, batch_size=4, vocab_size=100, max_chars=10000,
            eval_every=20, learning_rate=.003, device='cpu')
        training.train(args)
        manifest = json.loads((root/'trained/manifest.json').read_text(encoding='utf-8'))
        assert manifest['best_validation_loss'] < manifest['initial_validation_loss'] - .1
        model, record = training.load_checkpoint(root/'trained/best.pt')
        assert ord('新') not in record['vocabulary'], 'validation text leaked into vocabulary'
        assert record['provenance']['initialization'] == 'random'
        try:
            training.export_model(model, [0x110000, 0x110001] + [ord('今')] * (len(record['vocabulary']) - 2), root/'invalid.coral')
        except ValueError:
            assert not (root/'invalid.coral').exists()
        else:
            raise AssertionError('duplicate vocabulary exported')
        try:
            training.export_model(model, record['vocabulary'], root/'trained/character_model.coral')
        except FileExistsError:
            pass
        else:
            raise AssertionError('existing model overwritten')
        cases = root/'cases.jsonl'
        examples = [{'context': context, 'text': text} for context in ['', '今天', '今天𠀀可以']
                    for text in ['可以写了', '这样的组合是', '加不完的', '新', '']]
        cases.write_text('\n'.join(json.dumps(item, ensure_ascii=False) for item in examples), encoding='utf-8')
        training.verify(argparse.Namespace(checkpoint=str(root/'trained/best.pt'),
            model=str(root/'trained/character_model.coral'), probe=sys.argv[1], cases=str(cases), tolerance=.005))
        training.export_model(model, record['vocabulary'], root/'int8.coral', 'int8')
        training.verify(argparse.Namespace(checkpoint=str(root/'trained/best.pt'),
            model=str(root/'int8.coral'), probe=sys.argv[1], cases=str(cases), tolerance=.2))
        args.output = str(root/'overlap')
        args.validation = args.train
        try:
            training.train(args)
        except ValueError as error:
            assert 'overlap' in str(error)
        else:
            raise AssertionError('train/validation overlap accepted')
    print('random training, held-out vocabulary, native parity and overlap rejection passed')


if __name__ == '__main__':
    main()
