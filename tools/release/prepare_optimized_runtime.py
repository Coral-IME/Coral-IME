"""编译独立词对源并冻结打包输入；保留既有预编译模型兼容入口。"""
import argparse
import configparser
import datetime
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path, PurePosixPath

sys.dont_write_bytecode = True


def digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def info(path):
    return {'size': path.stat().st_size, 'sha256': digest(path)}


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.tmp')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    temporary.replace(path)


def checked_version(value):
    if not re.fullmatch(r'\d+\.\d+\.\d+\.\d+', value) or any(int(part) > 65535 for part in value.split('.')):
        raise ValueError('Version must be a.b.c.d with each part <= 65535')
    return value


def load_tools(project):
    sys.path.insert(0, str(project / 'tools/data'))
    sys.path.insert(0, str(project / 'tools/release'))
    import runtime_manifest
    import package_windows
    import character_model_format
    return runtime_manifest, package_windows, character_model_format


def resource_inventory(source):
    result = {}
    for folder in ('data', 'config'):
        if not (source / folder).is_dir():
            raise ValueError('Missing resource folder: ' + str(source / folder))
        for path in sorted((source / folder).rglob('*')):
            if path.is_symlink() or getattr(path, 'is_junction', lambda: False)():
                raise ValueError('Resource links are not supported: ' + str(path))
            if path.is_file():
                result[path.relative_to(source).as_posix()] = info(path)
    return result

def validate_ranking_profile(profile, tables, character, ranker):
    expected = {'candidate_ranker_sha256': ranker['sha256'],
                'character_model_sha256': character['sha256'],
                'dictionary_sha256': tables['files']['mainlexicon.coral']['sha256'],
                'pair_bonus_sha256': tables['files']['languagemodel.coral']['sha256']}
    if profile.get('format_version') != 1 or any(profile.get(k) != v for k, v in expected.items()):
        raise ValueError('Ranking profile does not match selected models and dictionary')
    if (profile.get('character_weight') != .5 or profile.get('search_paths') != 32
            or not isinstance(profile.get('neural_paths'), int) or not 2 <= profile['neural_paths'] <= 32
            or not isinstance(profile.get('rerank_threshold'), int) or not 0 <= profile['rerank_threshold'] <= 64):
        raise ValueError('Unsupported ranking profile parameters')


def check(args, project):
    runtime_manifest, packaging, character_format = load_tools(project)
    source = args.source_runtime.resolve(strict=True)
    character = args.character_model.resolve(strict=True)
    version = checked_version(args.version)
    work = (args.work_root or args.selection_file.parent).resolve()
    outputs = [work / 'build/win-x64-release', work / 'build/win-x86-release',
               work / 'runtime', work / 'packages' / version]
    if any(source == path or source.is_relative_to(path) or path.is_relative_to(source) or
           character == path or character.is_relative_to(path) for path in outputs):
        raise ValueError('Model inputs must be outside folders cleaned by this package run')
    tables = None
    pair_source_record = None
    compiled_model = None
    if args.pair_bonus_source:
        from build_pair_bonus import compile_source, inspect_pairs
        pair_source = args.pair_bonus_source.resolve(strict=True)
        if any(pair_source == path or pair_source.is_relative_to(path) for path in outputs):
            raise ValueError('Pair source must be outside cleaned outputs')
        before = info(pair_source)
        encoded = compile_source(pair_source)
        inspect_pairs(encoded)
        if info(pair_source) != before:
            raise ValueError('Pair source changed during compilation')
        frozen = work / 'model-inputs'
        frozen.mkdir(parents=True, exist_ok=True)
        compiled_model = frozen / ('pair-bonus-' + hashlib.sha256(encoded).hexdigest() + '.coral')
        if compiled_model.exists():
            if compiled_model.read_bytes() != encoded:
                raise ValueError('Frozen compiled model was modified')
        else:
            temporary = compiled_model.with_suffix('.partial')
            with temporary.open('xb') as stream:
                stream.write(encoded)
            temporary.rename(compiled_model)
        pair_source_record = {'path':str(pair_source), **before}
        tables = packaging.precompiled_tables_info(source / 'data/generallexicon', compiled_model)
    else:
        tables = packaging.precompiled_tables_info(source / 'data/generallexicon')
    character_info = runtime_manifest.character_model_info(character)
    ranker = getattr(args, 'candidate_ranker', None)
    profile_path = getattr(args, 'ranking_profile', None)
    ranker_record = profile_record = None
    if profile_path and not ranker:
        raise ValueError('Ranking profile requires a candidate ranker')
    if ranker:
        ranker = ranker.resolve(strict=True)
        if any(ranker == path or ranker.is_relative_to(path) for path in outputs):
            raise ValueError('Candidate ranker must be outside cleaned outputs')
        ranker_record = runtime_manifest.candidate_ranker_info(ranker)
        if profile_path:
            profile_path = profile_path.resolve(strict=True)
            if any(profile_path == path or profile_path.is_relative_to(path) for path in outputs):
                raise ValueError('Ranking profile must be outside cleaned outputs')
            profile = json.loads(profile_path.read_text(encoding='utf-8'))
            validate_ranking_profile(profile, tables, character_info, ranker_record)
            profile_record = {'path': str(profile_path), **info(profile_path), 'parameters': profile}
            config = configparser.ConfigParser(interpolation=None)
            config.read(source / 'config/coralpinyinConfig.ini', encoding='utf-8-sig')
            if not config.getboolean('settingsconfig', 'smartCandidates', fallback=False):
                raise ValueError('Ranking profile requires smart candidates enabled in the package defaults')
    files = resource_inventory(source)
    if not ranker and 'data/generallexicon/candidate_ranker.coral' in files:
        raise ValueError('Source contains an unselected candidate ranker; select it explicitly')
    # 词库与模型自身的绑定是权威；固定目录更新后无需手改旧清单的哈希。
    for name in ('mainlexicon.coral', 'languagemodel.coral'):
        if name != 'languagemodel.coral' or compiled_model is None:
            if files['data/generallexicon/' + name] != tables['files'][name]:
                raise ValueError('Resource changed during preflight: ' + name)
    record = {
        'format_version': 1, 'version': version, 'project_root': str(project),
        'source_runtime': str(source), 'files': files, 'tables': tables,
        'pair_bonus_source': pair_source_record,
        'compiled_model': str(compiled_model) if compiled_model else None,
        'character_model_source': str(character), 'character_model_info': character_info,
        'character_model_precision': character_format.inspect_model(character)[0],
        'candidate_ranker_source': str(ranker) if ranker else None,
        'candidate_ranker': ranker_record, 'ranking_profile': profile_record,
        'data_version': 'active-' + tables['files']['mainlexicon.coral']['sha256'][:12] + '-' +
                        tables['files']['languagemodel.coral']['sha256'][:12],
        'checked_at': datetime.datetime.now().astimezone().isoformat(),
    }
    write_json(args.selection_file, record)
    print('Optimized source:', source)
    print('Dictionary SHA256:', tables['files']['mainlexicon.coral']['sha256'])
    print('Language model SHA256:', tables['files']['languagemodel.coral']['sha256'])
    print('States:', tables['states'], 'explicit bigrams:', tables['explicit_arcs'])
    print('Character model:', character)
    print('Preflight passed; no build/output folders were removed.')


def stage(args, project):
    runtime_manifest, packaging, character_format = load_tools(project)
    selection = json.loads(args.selection_file.read_text(encoding='utf-8'))
    if Path(selection['project_root']).resolve() != project:
        raise ValueError('Selection belongs to another project')
    source = Path(selection['source_runtime']).resolve(strict=True)
    character = Path(selection['character_model_source']).resolve(strict=True)
    output = args.output_dir.resolve()
    if output == source or output.is_relative_to(source) or source.is_relative_to(output):
        raise ValueError('Input and output folders must not overlap')
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        raise ValueError('Output must be a new or empty folder: ' + str(output))
    if resource_inventory(source) != selection['files'] or info(character) != selection['character_model_info']:
        raise ValueError('Inputs changed since preflight; run package.bat again')
    pair_source = selection.get('pair_bonus_source')
    compiled_model = Path(selection['compiled_model']) if selection.get('compiled_model') else None
    ranker = Path(selection['candidate_ranker_source']) if selection.get('candidate_ranker_source') else None
    if ranker and runtime_manifest.candidate_ranker_info(ranker) != selection['candidate_ranker']:
        raise ValueError('Candidate ranker changed since preflight')
    profile = selection.get('ranking_profile')
    if profile and info(Path(profile['path'])) != {key: profile[key] for key in ('size', 'sha256')}:
        raise ValueError('Ranking profile changed since preflight')
    if pair_source and info(Path(pair_source['path'])) != {k:pair_source[k] for k in ('size','sha256')}:
        raise ValueError('Pair source changed since preflight; run package.bat again')
    if compiled_model and info(compiled_model) != selection['tables']['files']['languagemodel.coral']:
        raise ValueError('Frozen compiled model changed since preflight')
    output.mkdir(parents=True, exist_ok=True)
    for name, expected in selection['files'].items():
        relative = PurePosixPath(name)
        if relative.is_absolute() or '..' in relative.parts or relative.parts[0] not in ('data', 'config'):
            raise ValueError('Invalid selected resource path')
        target = output.joinpath(*relative.parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        selected = compiled_model if name == 'data/generallexicon/languagemodel.coral' and compiled_model else source.joinpath(*relative.parts)
        shutil.copyfile(selected, target)
        wanted = selection['tables']['files']['languagemodel.coral'] if selected == compiled_model else expected
        if info(target) != wanted:
            raise ValueError('Copy verification failed: ' + name)
    character_target = output / 'data/generallexicon/character_model.coral'
    if compiled_model and not (output / 'data/generallexicon/languagemodel.coral').exists():
        shutil.copyfile(compiled_model, output / 'data/generallexicon/languagemodel.coral')
    character_target.unlink(missing_ok=True)
    if selection['character_model_precision'] == 1:
        character_format.quantize_model(character, character_target)
    else:
        shutil.copyfile(character, character_target)
    character_info = runtime_manifest.character_model_info(character_target)
    ranker_info = None
    ranker_target = output / 'data/generallexicon/candidate_ranker.coral'
    if ranker:
        shutil.copyfile(ranker, ranker_target)
        ranker_info = runtime_manifest.candidate_ranker_info(ranker_target)
        if ranker_info != selection['candidate_ranker']:
            raise ValueError('Candidate ranker changed during copying')
    elif ranker_target.exists():
        raise ValueError('Source contains an unselected candidate ranker')
    tables = packaging.precompiled_tables_info(output / 'data/generallexicon')
    if tables != selection['tables']:
        raise ValueError('Staged tables differ from selected tables')
    if profile:
        validate_ranking_profile(profile['parameters'], tables, character_info, ranker_info)
    probe = args.native_probe.resolve(strict=True)
    subprocess.run([str(probe), str(output / 'data/generallexicon/languagemodel.coral'),
                    str(output / 'data/generallexicon/mainlexicon.coral')], input=b'', check=True)
    if ranker:
        ranker_probe = getattr(args, 'ranker_probe', None)
        if not ranker_probe:raise ValueError('Candidate ranker requires a native ranker probe')
        subprocess.run([str(ranker_probe.resolve(strict=True)), str(ranker_target)], input=b'', check=True)
    runtime_manifest.write_runtime(output, selection['version'])
    runtime_manifest.verify_runtime(output)
    record = {
        'format_version': 3, 'complete': True, 'installer_bins_match': True,
        'installer_bins_match_basis': 'exact precompiled table copies; compact restoration is verified by package_windows',
        'data_version': selection['data_version'], 'engine_version': selection['version'],
        'model_mode': 'independent-word-pair-bonus' if tables['format'] == 'CBGN0001' else 'paired-precompiled-surface-model',
        'deployment_mode': 'lossless-compact-installer-restoration',
        'native_load_verified': True, 'native_probe_sha256': digest(probe),
        'tables': tables, 'character_model': character_info,
        'candidate_ranker': ranker_info, 'ranking_profile': profile,
        'pair_bonus_source': pair_source,
        'character_model_source': selection['character_model_info'],
        'source_runtime': str(source), 'selection_sha256': digest(args.selection_file),
    }
    write_json(output / 'build-record.json', record)
    manifest = {'format_version': 2, 'data_version': selection['data_version'],
                'engine_version': selection['version'], 'excluded_files': ['runtime-manifest.json'],
                'files': runtime_manifest.inventory(output, exclude=('runtime-manifest.json',))}
    write_json(output / 'runtime-manifest.json', manifest)
    runtime_manifest.verify(output, manifest)
    print('Optimized runtime ready:', output)


def verify_package(args, project):
    selection = json.loads(args.selection_file.read_text(encoding='utf-8'))
    package = json.loads(args.package_manifest.read_text(encoding='utf-8'))
    tables = package.get('precompiled_tables') or {}
    if not package.get('complete') or tables.get('files') != selection['tables']['files']:
        raise ValueError('Package did not use the selected dictionary/model')
    if tables.get('storage') != 'compact' or not tables.get('installer_restoration_verified') or not tables.get('native_load_verified'):
        raise ValueError('Package lacks verified compact restoration')
    if 'data/generallexicon/dictionary_bundle.dictpack.lzma2' in package['files']:
        raise ValueError('Old CSV rebuild payload leaked into optimized package')
    if package.get('version') != selection['version']:
        raise ValueError('Package version does not match selection')
    if package.get('pair_bonus_source') != selection.get('pair_bonus_source'):
        raise ValueError('Package pair source provenance differs from selection')
    if package.get('candidate_ranker') != selection.get('candidate_ranker'):
        raise ValueError('Package candidate ranker differs from selection')
    if package.get('ranking_profile') != selection.get('ranking_profile'):
        raise ValueError('Package ranking profile differs from selection')
    if selection.get('candidate_ranker'):
        expected = {key: selection['candidate_ranker'][key] for key in ('size', 'sha256')}
        if package['files'].get('data/generallexicon/candidate_ranker.coral') != expected:
            raise ValueError('Package is missing the selected candidate ranker payload')
    print('Package verified: optimized paired resources and exact compact restoration.')


def clean(args, project):
    root = args.work_root.absolute()
    if root.resolve() != root:
        raise ValueError('Work folder must not be a link or junction')
    checked_version(args.version)
    targets = [root / 'build/win-x64-release', root / 'build/win-x86-release',
               root / 'runtime', root / 'packages' / args.version]
    # 先核对全部绝对路径，再删除明确属于本次构建的四个目录。
    for path in targets:
        if path.resolve() != path or not path.is_relative_to(root) or path == root:
            raise ValueError('Unsafe output path: ' + str(path))
        if path.exists() and not path.is_dir():
            raise ValueError('Expected output directory: ' + str(path))
    for path in targets:
        if path.exists():
            shutil.rmtree(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('check', 'stage', 'verify-package', 'clean'))
    parser.add_argument('--project-root', type=Path, required=True)
    parser.add_argument('--source-runtime', type=Path)
    parser.add_argument('--character-model', type=Path)
    parser.add_argument('--candidate-ranker', type=Path)
    parser.add_argument('--ranking-profile', type=Path)
    parser.add_argument('--ranker-probe', type=Path)
    parser.add_argument('--pair-bonus-source', type=Path, help='Compile editable prev_word/next_word/bonus TSV before freezing inputs')
    parser.add_argument('--version')
    parser.add_argument('--selection-file', type=Path)
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--native-probe', type=Path)
    parser.add_argument('--package-manifest', type=Path)
    parser.add_argument('--work-root', type=Path)
    args = parser.parse_args()
    try:
        project = args.project_root.resolve(strict=True)
        globals()[args.action.replace('-', '_')](args, project)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
