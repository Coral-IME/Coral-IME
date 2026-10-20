"""Compile lexicon resources through an explicit native executable."""
import sys
sys.dont_write_bytecode = True
from pathlib import Path
import argparse
import subprocess

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ('resource-compiler', 'source-dir', 'index', 'bigram', 'output-dir'):
        parser.add_argument('--' + flag, type=Path, required=True)
    args = parser.parse_args()
    from paths import output_path
    compiler = args.resource_compiler.resolve()
    if not compiler.is_file():
        parser.error('compiled resource tool is missing')
    source = args.source_dir.resolve()
    root = args.output_dir.resolve()
    protected = [source, args.index, args.bigram, compiler]
    for relative in ('data/generallexicon/primary.marisa', 'data/generallexicon/primary.dat',
                     'data/generallexicon/mainlexicon.coral', 'data/generallexicon/languagemodel.coral',
                     'data/generallexicon/english.marisa', 'data/generallexicon/english.dat',
                     'data/bushou.marisa', 'data/bushou.dat', 'data/stroke.marisa', 'data/stroke.dat'):
        output_path(root / relative, protected)
    return subprocess.run([str(compiler), '--source-dir', str(source), '--index', str(args.index.resolve()),
        '--bigram', str(args.bigram.resolve()), '--output-dir', str(root)], check=False).returncode

if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print('Lexicon generation failed: ' + str(error), file=sys.stderr)
        raise SystemExit(1)
