"""为独立私有数据仓库生成并核验固定版本的正式发行输入。"""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import zipfile


SOURCE_FILES = ('coral_lexicon.csv', 'coral_languagemodel.csv', 'eng_lexicon.csv',
                'pinyin_frequency.csv', 'umode_radical.csv', 'umode_stroke.csv', 'dw.coral')
MODEL_FILES = ('char_lm.dat', 'correction_pinyin_model.dat')
REQUIRED = ({'source/' + name for name in SOURCE_FILES}
            | {'models/' + name for name in MODEL_FILES}
            | {'source/opencc/' + name for name in ('s2t.json', 'symbol_word.coral', 'emoji_word.coral')})
MAX_TOTAL_BYTES = 1024 * 1024 * 1024


def digest(path):
    checksum = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            checksum.update(chunk)
    return checksum.hexdigest()


def safe_name(name):
    path = PurePosixPath(name)
    if (not name or '\\' in name or ':' in name or path.is_absolute()
            or path.as_posix() != name or any(part in ('.', '..') for part in path.parts)):
        raise ValueError('Invalid input archive path')
    if name in REQUIRED:
        return
    if (len(path.parts) == 3 and path.parts[:2] == ('source', 'opencc')
            and path.suffix in ('.json', '.coral', '.ocd2', '.txt')):
        return
    raise ValueError('Unexpected file in official inputs: ' + name)


def validate_manifest(manifest):
    if (manifest.get('format_version') != 1 or manifest.get('model_mode') != 'explicit-reuse'
            or not re.fullmatch(r'[A-Za-z0-9_.-]{1,80}', manifest.get('data_version', ''))):
        raise ValueError('Unsupported official input manifest')
    files = manifest.get('files', {})
    if not isinstance(files, dict) or not REQUIRED.issubset(files) or len(files) > 128:
        raise ValueError('Required dictionary or model inputs are missing')
    total = 0
    for name, entry in files.items():
        safe_name(name)
        if (not isinstance(entry.get('size'), int) or entry['size'] < 0
                or not re.fullmatch(r'[0-9a-f]{64}', entry.get('sha256', ''))):
            raise ValueError('Invalid file checksum or size')
        total += entry['size']
    if total > MAX_TOTAL_BYTES:
        raise ValueError('Official inputs exceed the supported size')
    return files


def load_lock(path):
    lock = json.loads(Path(path).read_text(encoding='utf-8'))
    if (lock.get('format_version') != 1
            or not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', lock.get('repository', ''))
            or not re.fullmatch(r'[0-9a-f]{40}', lock.get('revision', ''))
            or not re.fullmatch(r'[A-Za-z0-9_.-]{1,80}', lock.get('data_version', ''))
            or lock.get('archive') != 'official-inputs.zip'
            or lock.get('model_mode') != 'explicit-reuse'
            or any(not re.fullmatch(r'[0-9a-f]{64}', lock.get(key, ''))
                   for key in ('archive_sha256', 'manifest_sha256'))):
        raise ValueError('Invalid official data lock')
    return lock


def pack(input_dir, output_dir, data_version):
    input_dir = Path(input_dir).resolve()
    output_dir = Path(output_dir).resolve()
    if output_dir.is_relative_to(input_dir) or input_dir.is_relative_to(output_dir):
        raise ValueError('Input and output directories must not overlap')
    files = {}
    for path in sorted(input_dir.rglob('*')):
        if path.is_symlink():
            raise ValueError('Symlink inputs are not supported')
        if path.is_file():
            name = path.relative_to(input_dir).as_posix()
            safe_name(name)
            files[name] = {'size': path.stat().st_size, 'sha256': digest(path)}
    manifest = {'format_version': 1, 'data_version': data_version,
                'model_mode': 'explicit-reuse', 'files': files}
    validate_manifest(manifest)
    output_dir.mkdir(parents=True, exist_ok=False)
    archive_path = output_dir / 'official-inputs.zip'
    with zipfile.ZipFile(archive_path, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name in files:
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (stat.S_IFREG | 0o644) << 16
            with (input_dir / name).open('rb') as source, archive.open(info, 'w') as target:
                shutil.copyfileobj(source, target, 1024 * 1024)
    manifest_path = output_dir / 'inputs-manifest.json'
    manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    print(json.dumps({'data_version': data_version, 'files': len(files),
                      'archive_sha256': digest(archive_path),
                      'manifest_sha256': digest(manifest_path)}, ensure_ascii=False))


def unpack(repository_dir, lock_path, output_dir):
    repository_dir = Path(repository_dir).resolve()
    output_dir = Path(output_dir).resolve()
    lock = load_lock(lock_path)
    manifest_path = repository_dir / 'inputs-manifest.json'
    archive_path = repository_dir / lock['archive']
    if digest(manifest_path) != lock['manifest_sha256'] or digest(archive_path) != lock['archive_sha256']:
        raise ValueError('Private data repository differs from the locked checksums')
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    files = validate_manifest(manifest)
    if manifest['data_version'] != lock['data_version']:
        raise ValueError('Official data version differs from its lock')
    if output_dir.is_relative_to(repository_dir) or repository_dir.is_relative_to(output_dir):
        raise ValueError('Repository and output directories must not overlap')
    with zipfile.ZipFile(archive_path) as archive:
        infos = archive.infolist()
        names = [info.filename for info in infos]
        if len(names) != len(set(name.casefold() for name in names)) or set(names) != set(files):
            raise ValueError('Archive file inventory differs from its manifest')
        for info in infos:
            safe_name(info.filename)
            if (info.is_dir() or stat.S_ISLNK(info.external_attr >> 16)
                    or info.flag_bits & 1 or info.file_size != files[info.filename]['size']):
                raise ValueError('Invalid archive entry')
        output_dir.mkdir(parents=True, exist_ok=False)
        for info in infos:
            target = output_dir / info.filename
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(info) as source, target.open('xb') as output:
                shutil.copyfileobj(source, output, 1024 * 1024)
            if digest(target) != files[info.filename]['sha256']:
                raise ValueError('Extracted input checksum mismatch')
    print('Official input checksums verified: ' + str(len(files)) + ' files, ' + lock['data_version'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    create = commands.add_parser('pack')
    create.add_argument('--input-dir', type=Path, required=True)
    create.add_argument('--output-dir', type=Path, required=True)
    create.add_argument('--data-version', required=True)
    extract = commands.add_parser('unpack')
    extract.add_argument('--repository-dir', type=Path, required=True)
    extract.add_argument('--lock', type=Path, required=True)
    extract.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == 'pack':
            pack(args.input_dir, args.output_dir, args.data_version)
        else:
            unpack(args.repository_dir, args.lock, args.output_dir)
    except (ValueError, OSError, zipfile.BadZipFile) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
