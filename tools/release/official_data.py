"""为独立私有数据仓库生成并核验固定版本的正式发行输入。"""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'data'))
from character_model_format import inspect_model
from runtime_manifest import ENGINE_FILES
from candidate_ranker_format import inspect_model as ranker_info


SOURCE_FILES = ('coral_lexicon.csv', 'coral_languagemodel.csv', 'eng_lexicon.csv',
                'pinyin_frequency.csv', 'umode_radical.csv', 'umode_stroke.csv', 'dw.coral')
MODEL_FILES = ('char_lm.dat', 'correction_pinyin_model.dat')
CHARACTER_MODEL = 'models/character_model.coral'
CANDIDATE_RANKER = 'models/candidate_ranker.coral'
RANKING_PROFILE = 'models/ranking-profile.json'
REQUIRED = ({'source/' + name for name in SOURCE_FILES}
            | {'models/' + name for name in MODEL_FILES}
            | {'source/opencc/' + name for name in ('s2t.json', 'symbol_word.coral', 'emoji_word.coral')})
MAX_TOTAL_BYTES = 1024 * 1024 * 1024
PAIR_MODE = 'independent-word-pair-bonus'
OPTIMIZED_REQUIRED = ({'source/coral_lexicon.csv', 'source/pair-bonuses.tsv', CHARACTER_MODEL,
                       'runtime/data/generallexicon/mainlexicon.coral', 'runtime/data/dw.coral'}
                      | {'runtime/' + name for name in ENGINE_FILES.values()}
                      | {'runtime/data/opencc/' + name for name in ('s2t.json', 'symbol_word.coral', 'emoji_word.coral')})


def digest(path):
    checksum = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            checksum.update(chunk)
    return checksum.hexdigest()


def safe_name(name, optimized=False):
    path = PurePosixPath(name)
    if (not name or '\\' in name or ':' in name or path.is_absolute()
            or path.as_posix() != name or any(part in ('.', '..') for part in path.parts)):
        raise ValueError('Invalid input archive path')
    if name in (OPTIMIZED_REQUIRED if optimized else REQUIRED) or name == CHARACTER_MODEL or (optimized and name in (CANDIDATE_RANKER, RANKING_PROFILE)):
        return
    prefix = ('runtime', 'data', 'opencc') if optimized else ('source', 'opencc')
    if (len(path.parts) == len(prefix) + 1 and path.parts[:-1] == prefix
            and path.suffix in ('.json', '.coral', '.ocd2', '.txt')):
        return
    raise ValueError('Unexpected file in official inputs: ' + name)


def validate_manifest(manifest):
    optimized = manifest.get('model_mode') == PAIR_MODE
    if (manifest.get('format_version') != (2 if optimized else 1)
            or manifest.get('model_mode') not in ('explicit-reuse', PAIR_MODE)
            or not re.fullmatch(r'[A-Za-z0-9_.-]{1,80}', manifest.get('data_version', ''))):
        raise ValueError('Unsupported official input manifest')
    files = manifest.get('files', {})
    required = OPTIMIZED_REQUIRED if optimized else REQUIRED
    if not isinstance(files, dict) or not required.issubset(files) or len(files) > 128:
        raise ValueError('Required dictionary or model inputs are missing')
    total = 0
    for name, entry in files.items():
        safe_name(name, optimized)
        if (not isinstance(entry.get('size'), int) or entry['size'] < 0
                or not re.fullmatch(r'[0-9a-f]{64}', entry.get('sha256', ''))):
            raise ValueError('Invalid file checksum or size')
        total += entry['size']
    if total > MAX_TOTAL_BYTES:
        raise ValueError('Official inputs exceed the supported size')
    character = manifest.get('character_model')
    if CHARACTER_MODEL in files:
        validate_character_record(character)
        if {key: character[key] for key in ('size', 'sha256')} != files[CHARACTER_MODEL]:
            raise ValueError('Character model manifest differs from file inventory')
    elif character is not None:
        raise ValueError('Character model is declared but missing')
    if optimized:
        validate_tables_record(manifest.get('precompiled_tables'))
        for name, entry in manifest['precompiled_tables']['files'].items():
            if entry != files['runtime/data/generallexicon/' + name]:
                raise ValueError('Precompiled tables differ from file inventory')
    validate_ranking_records(manifest, files)
    return files


def validate_ranking_records(record, files=None):
    ranker, profile = record.get('candidate_ranker'), record.get('ranking_profile')
    if ranker is None and profile is None:
        if files and (CANDIDATE_RANKER in files or RANKING_PROFILE in files):
            raise ValueError('Undeclared ranking inputs')
        return
    if (record.get('model_mode') != PAIR_MODE or not isinstance(ranker, dict)
            or ranker.get('path') != CANDIDATE_RANKER or ranker.get('format') != 'CRNK0001'
            or ranker.get('feature_version') != 1 or ranker.get('features') != 32
            or not isinstance(profile, dict) or profile.get('path') != RANKING_PROFILE):
        raise ValueError('Invalid ranking release records')
    for name, entry in ((CANDIDATE_RANKER, ranker), (RANKING_PROFILE, profile)):
        if (type(entry.get('size')) is not int or not 0 < entry['size'] <= 2 * 1024 * 1024
                or not re.fullmatch(r'[0-9a-f]{64}', entry.get('sha256', ''))
                or (files is not None and files.get(name) != {k: entry[k] for k in ('size', 'sha256')})):
            raise ValueError('Ranking inputs differ from file inventory')
    from prepare_optimized_runtime import validate_ranking_profile
    validate_ranking_profile(profile.get('parameters', {}), record['precompiled_tables'],
                             record['character_model'], ranker)


def ranking_records(directory):
    directory = Path(directory)
    ranker = {'path': CANDIDATE_RANKER, **ranker_info(directory / CANDIDATE_RANKER)}
    path = directory / RANKING_PROFILE
    profile = {'path': RANKING_PROFILE, 'size': path.stat().st_size, 'sha256': digest(path),
               'parameters': json.loads(path.read_text(encoding='utf-8'))}
    return {'candidate_ranker': ranker, 'ranking_profile': profile}


def validate_tables_record(record):
    if (not isinstance(record, dict) or record.get('format') != 'CBGN0001'
            or set(record.get('files', {})) != {'mainlexicon.coral', 'languagemodel.coral'}):
        raise ValueError('Expected locked independent pair tables')
    for entry in record['files'].values():
        if (type(entry.get('size')) is not int or not 0 < entry['size'] <= MAX_TOTAL_BYTES
                or not re.fullmatch(r'[0-9a-f]{64}', entry.get('sha256', ''))):
            raise ValueError('Invalid precompiled table checksum or size')


def validate_character_record(record):
    if (not isinstance(record, dict) or record.get('path') != CHARACTER_MODEL
            or type(record.get('size')) is not int or not 128 <= record['size'] <= 256 * 1024 * 1024
            or not re.fullmatch(r'[0-9a-f]{64}', record.get('sha256', ''))
            or record.get('precision') not in ('float32', 'int8')):
        raise ValueError('Invalid character model release record')


def character_record(path):
    precision = 'int8' if inspect_model(path)[0] == 2 else 'float32'
    return {'path': CHARACTER_MODEL, 'size': Path(path).stat().st_size,
            'sha256': digest(path), 'precision': precision}


def load_lock(path):
    lock = json.loads(Path(path).read_text(encoding='utf-8'))
    optimized = lock.get('model_mode') == PAIR_MODE
    if (lock.get('format_version') != (2 if optimized else 1)
            or not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', lock.get('repository', ''))
            or not re.fullmatch(r'[0-9a-f]{40}', lock.get('revision', ''))
            or not re.fullmatch(r'[A-Za-z0-9_.-]{1,80}', lock.get('data_version', ''))
            or lock.get('archive') != 'official-inputs.zip'
            or lock.get('model_mode') not in ('explicit-reuse', PAIR_MODE)
            or any(not re.fullmatch(r'[0-9a-f]{64}', lock.get(key, ''))
                   for key in ('archive_sha256', 'manifest_sha256'))):
        raise ValueError('Invalid official data lock')
    if 'character_model' in lock:
        validate_character_record(lock['character_model'])
    if optimized:
        validate_tables_record(lock.get('precompiled_tables'))
    validate_ranking_records(lock)
    return lock


def pack(input_dir, output_dir, data_version, model_mode='explicit-reuse'):
    input_dir = Path(input_dir).resolve()
    output_dir = Path(output_dir).resolve()
    if output_dir.is_relative_to(input_dir) or input_dir.is_relative_to(output_dir):
        raise ValueError('Input and output directories must not overlap')
    files = {}
    optimized = model_mode == PAIR_MODE
    for path in sorted(input_dir.rglob('*')):
        if path.is_symlink():
            raise ValueError('Symlink inputs are not supported')
        if path.is_file():
            name = path.relative_to(input_dir).as_posix()
            safe_name(name, optimized)
            files[name] = {'size': path.stat().st_size, 'sha256': digest(path)}
    manifest = {'format_version': 2 if optimized else 1, 'data_version': data_version,
                'model_mode': model_mode, 'files': files}
    if (input_dir / CHARACTER_MODEL).is_file():
        manifest['character_model'] = character_record(input_dir / CHARACTER_MODEL)
    if optimized:
        from package_windows import precompiled_tables_info
        from build_pair_bonus import compile_source
        directory = input_dir / 'runtime/data/generallexicon'
        manifest['precompiled_tables'] = precompiled_tables_info(directory)
        if compile_source(input_dir / 'source/pair-bonuses.tsv') != (directory / 'languagemodel.coral').read_bytes():
            raise ValueError('Pair source differs from the selected compiled model')
    if (input_dir / CANDIDATE_RANKER).exists() or (input_dir / RANKING_PROFILE).exists():
        manifest.update(ranking_records(input_dir))
    validate_manifest(manifest)
    output_dir.mkdir(parents=True, exist_ok=False)
    archive_path = output_dir / 'official-inputs.zip'
    compression = zipfile.ZIP_LZMA if optimized else zipfile.ZIP_DEFLATED
    with zipfile.ZipFile(archive_path, 'w', compression=compression) as archive:
        for name in files:
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = compression
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
    if manifest.get('character_model') != lock.get('character_model'):
        raise ValueError('Character model differs from its lock')
    if manifest.get('model_mode') != lock['model_mode'] or manifest.get('precompiled_tables') != lock.get('precompiled_tables'):
        raise ValueError('Precompiled tables differ from their lock')
    if any(manifest.get(k) != lock.get(k) for k in ('candidate_ranker', 'ranking_profile')):
        raise ValueError('Ranking inputs differ from their lock')
    if output_dir.is_relative_to(repository_dir) or repository_dir.is_relative_to(output_dir):
        raise ValueError('Repository and output directories must not overlap')
    with zipfile.ZipFile(archive_path) as archive:
        infos = archive.infolist()
        names = [info.filename for info in infos]
        if len(names) != len(set(name.casefold() for name in names)) or set(names) != set(files):
            raise ValueError('Archive file inventory differs from its manifest')
        for info in infos:
            safe_name(info.filename, lock['model_mode'] == PAIR_MODE)
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
    if manifest.get('character_model') and character_record(output_dir / CHARACTER_MODEL) != manifest['character_model']:
        raise ValueError('Extracted character model metadata mismatch')
    if lock.get('candidate_ranker') and ranking_records(output_dir) != {k: lock[k] for k in ('candidate_ranker', 'ranking_profile')}:
        raise ValueError('Extracted ranking model metadata mismatch')
    if lock['model_mode'] == PAIR_MODE:
        shutil.copyfile(manifest_path, output_dir / 'inputs-manifest.json')
    print('Official input checksums verified: ' + str(len(files)) + ' files, ' + lock['data_version'])


def stage_optimized(input_dir, lock_path, output_dir, config_dir, engine_version, version, native_probe, ranker_probe=None):
    """原样部署锁定的预编译词库和独立词对；配置仅来自公开模板。"""
    import subprocess
    from runtime_manifest import inventory, write_runtime, verify_runtime
    from package_windows import precompiled_tables_info
    from build_pair_bonus import compile_source
    lock = load_lock(lock_path)
    if lock['model_mode'] != PAIR_MODE:
        raise ValueError('Expected optimized release inputs')
    input_dir, output_dir = Path(input_dir).resolve(), Path(output_dir).resolve()
    if output_dir.is_relative_to(input_dir) or input_dir.is_relative_to(output_dir):
        raise ValueError('Input and output directories must not overlap')
    manifest_path = input_dir / 'inputs-manifest.json'
    if digest(manifest_path) != lock['manifest_sha256']:
        raise ValueError('Input manifest differs from its lock')
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    for name, entry in validate_manifest(manifest).items():
        path = input_dir / name
        if path.stat().st_size != entry['size'] or digest(path) != entry['sha256']:
            raise ValueError('Selected input differs from its manifest: ' + name)
    # 独立校验源与模型一致，再交给当前源码编译的原生读者。
    directory = input_dir / 'runtime/data/generallexicon'
    tables = precompiled_tables_info(directory)
    if tables != lock['precompiled_tables']:
        raise ValueError('Selected precompiled tables differ from their lock')
    if compile_source(input_dir / 'source/pair-bonuses.tsv') != (directory / 'languagemodel.coral').read_bytes():
        raise ValueError('Pair source differs from the selected compiled model')
    if character_record(input_dir / CHARACTER_MODEL) != lock['character_model']:
        raise ValueError('Character model differs from its lock')
    ranking = {}
    if lock.get('candidate_ranker'):
        ranking = ranking_records(input_dir)
        if ranking != {k: lock[k] for k in ('candidate_ranker', 'ranking_profile')} or not ranker_probe:
            raise ValueError('Ranking inputs differ from lock or native ranker probe is missing')
        import configparser
        config = configparser.ConfigParser(interpolation=None)
        config.read(Path(config_dir) / 'coralpinyinConfig.ini', encoding='utf-8-sig')
        if not config.getboolean('settingsconfig', 'smartCandidates', fallback=False):
            raise ValueError('Ranking release requires smart candidates enabled')
    output_dir.mkdir(parents=True, exist_ok=False)
    shutil.copytree(input_dir / 'runtime/data', output_dir / 'data')
    shutil.copytree(config_dir, output_dir / 'config')
    shutil.copytree(Path(config_dir) / 'doublepinyin', output_dir / 'data/doublepinyin')
    shutil.copyfile(input_dir / CHARACTER_MODEL, output_dir / 'data/generallexicon/character_model.coral')
    if ranking:
        shutil.copyfile(input_dir / CANDIDATE_RANKER, output_dir / 'data/generallexicon/candidate_ranker.coral')
        subprocess.run([str(Path(ranker_probe).resolve()), str(output_dir / 'data/generallexicon/candidate_ranker.coral')],
                       input=(' '.join(['0'] * 32) + '\n').encode(), check=True)
    general = output_dir / 'data/generallexicon'
    subprocess.run([str(Path(native_probe).resolve()), str(general / 'languagemodel.coral'),
                    str(general / 'mainlexicon.coral')], input=b'', check=True)
    write_runtime(output_dir, version)
    verify_runtime(output_dir)
    record = {'format_version': 3, 'complete': True, 'data_version': lock['data_version'],
              'engine_version': engine_version, 'model_mode': PAIR_MODE, 'installer_bins_match': True,
              'installer_bins_match_basis': 'locked precompiled copies; installer restoration checked during packaging',
              'tables': tables, 'character_model': {k: lock['character_model'][k] for k in ('size', 'sha256')},
              'pair_bonus_source': {'sha256': digest(input_dir / 'source/pair-bonuses.tsv'),
                                    'size': (input_dir / 'source/pair-bonuses.tsv').stat().st_size},
              'dictionary_source': {'sha256': digest(input_dir / 'source/coral_lexicon.csv'),
                                    'size': (input_dir / 'source/coral_lexicon.csv').stat().st_size}}
    if ranking:
        record['candidate_ranker'] = {k: v for k, v in ranking['candidate_ranker'].items() if k != 'path'}
        record['ranking_profile'] = ranking['ranking_profile']
    (output_dir / 'build-record.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    runtime_manifest = {'format_version': 2, 'data_version': lock['data_version'], 'engine_version': engine_version,
                        'excluded_files': ['runtime-manifest.json'],
                        'files': inventory(output_dir, exclude=('runtime-manifest.json',))}
    (output_dir / 'runtime-manifest.json').write_text(json.dumps(runtime_manifest, indent=2) + '\n', encoding='utf-8')
    print('Locked optimized runtime staged: ' + lock['data_version'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    create = commands.add_parser('pack')
    create.add_argument('--input-dir', type=Path, required=True)
    create.add_argument('--output-dir', type=Path, required=True)
    create.add_argument('--data-version', required=True)
    create.add_argument('--model-mode', choices=('explicit-reuse', PAIR_MODE), default='explicit-reuse')
    extract = commands.add_parser('unpack')
    extract.add_argument('--repository-dir', type=Path, required=True)
    extract.add_argument('--lock', type=Path, required=True)
    extract.add_argument('--output-dir', type=Path, required=True)
    stage = commands.add_parser('stage-optimized')
    stage.add_argument('--input-dir', type=Path, required=True)
    stage.add_argument('--lock', type=Path, required=True)
    stage.add_argument('--output-dir', type=Path, required=True)
    stage.add_argument('--config-dir', type=Path, required=True)
    stage.add_argument('--engine-version', required=True)
    stage.add_argument('--version', required=True)
    stage.add_argument('--native-probe', type=Path, required=True)
    stage.add_argument('--ranker-probe', type=Path)
    args = parser.parse_args()
    try:
        if args.command == 'pack':
            pack(args.input_dir, args.output_dir, args.data_version, args.model_mode)
        elif args.command == 'unpack':
            unpack(args.repository_dir, args.lock, args.output_dir)
        else:
            stage_optimized(args.input_dir, args.lock, args.output_dir, args.config_dir,
                            args.engine_version, args.version, args.native_probe, args.ranker_probe)
    except (ValueError, OSError, zipfile.BadZipFile) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
