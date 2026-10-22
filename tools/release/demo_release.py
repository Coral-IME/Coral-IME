"""校验测试发行版本，并记录可下载安装包的源码、构建来源与摘要。"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil


def app_version(value):
    if not re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)', value):
        raise argparse.ArgumentTypeError('Version must contain four canonical numeric components')
    if any(int(part) > 65535 for part in value.split('.')):
        raise argparse.ArgumentTypeError('Version components must be <= 65535')
    return value


def digest(path):
    checksum = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            checksum.update(chunk)
    return checksum.hexdigest()


def bundle(package_dir, output_dir, version, source_sha, repository, run_url,
           edition='demo', data_lock=None, test_summary=None):
    app_version(version)
    if not re.fullmatch(r'[0-9a-f]{40}', source_sha):
        raise ValueError('A full source commit SHA is required')
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repository):
        raise ValueError('Invalid GitHub repository')
    if not re.fullmatch(r'https://github\.com/' + re.escape(repository) + r'/actions/runs/[0-9]+', run_url):
        raise ValueError('The build URL must belong to this GitHub repository')
    manifest_path = package_dir / 'package-manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    if edition not in ('demo', 'official'):
        raise ValueError('Unknown release edition')
    expected_edition = 'demo' if edition == 'demo' else 'standard'
    if (manifest.get('complete') is not True or manifest.get('installer_compiled') is not True
            or manifest.get('version') != version or manifest.get('sign_mode') != 'None'
            or manifest.get('edition') != expected_edition):
        raise ValueError('Expected a complete, unsigned package with the requested version')
    lock = None
    summary = None
    if edition == 'official':
        if data_lock is None or test_summary is None:
            raise ValueError('Official release requires locked data and formal-data test results')
        from official_data import load_lock
        lock = load_lock(data_lock)
        runtime = manifest.get('runtime', {})
        if (runtime.get('engine_version') != source_sha
                or runtime.get('data_version') != lock['data_version']
                or runtime.get('model_mode') != lock['model_mode']
                or runtime.get('installer_bins_match') is not True
                or manifest.get('manual_launch') is not True):
            raise ValueError('Package runtime differs from the requested official data and source')
        summary = json.loads(Path(test_summary).read_text(encoding='utf-8'))
        expected_tests = {'sequence_test', 'lexicon_import_engine_test', 'uvmode_engine_test', 'config_gate_test'}
        tests = summary.get('tests', [])
        if (summary.get('format_version') != 1 or summary.get('passed') is not True
                or not re.fullmatch(r'[0-9a-f]{64}', summary.get('runtime_manifest_sha256', ''))
                or summary['runtime_manifest_sha256'] != manifest.get('runtime_manifest_sha256')
                or len(tests) != 4 or {item.get('name') for item in tests} != expected_tests
                or any(item.get('passed') is not True or item.get('exit_code') != 0 for item in tests)):
            raise ValueError('Formal-data checks must all pass before release')
    installer = package_dir / ('coralpinyin-window-x64-' + version + '.exe')
    installer_hash = digest(installer)
    if manifest.get('installer_sha256') != installer_hash:
        raise ValueError('Installer SHA-256 differs from the package manifest')

    # 新建输出目录，防止重跑时把旧安装包或无关文件混入下载产物。
    output_dir.mkdir(parents=True, exist_ok=False)
    prefix = 'coralpinyin-demo' if edition == 'demo' else 'coralpinyin-official-test'
    installer_name = prefix + '-windows-x64-' + version + '.exe'
    shutil.copyfile(installer, output_dir / installer_name)
    shutil.copyfile(manifest_path, output_dir / 'package-manifest.json')
    record = {
        'format_version': 1,
        'edition': 'demo' if edition == 'demo' else 'official-data-test',
        'version': version,
        'repository': repository,
        'source_sha': source_sha,
        'build_url': run_url,
        'data': ('self-authored public demo fixtures; not the official dictionary or model'
                 if edition == 'demo' else lock),
        'signed': False,
        'automatic_updates': False,
        'installation_verified': False,
        'installer': {'name': installer_name, 'sha256': installer_hash},
    }
    if summary is not None:
        record['formal_data_tests'] = summary
    (output_dir / 'BUILD_INFO.json').write_text(
        json.dumps(record, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    if edition == 'demo':
        notes = '''本版本仅用于验证自动构建和安装包分发，不代表正式输入质量。

- 使用仓库自制的小型演示词库及模型，未包含正式数据。
- 包内包含 x64 程序及 x86 文本服务组件，自动更新在编译时关闭。
- 安装完成后，请从开始菜单打开输入法；安装器明确标为演示版。
- 安装包未签名；Windows 可能显示未知发布者提示。
- 自动编译与测试通过不代表已完成实际安装、升级、卸载或真实应用输入验收。
- 演示包沿用产品安装位置和标识，请在独立虚拟机验收，避免覆盖日常使用版本。

'''
    else:
        notes = f'''本版本接入正式词库，供虚拟机安装和输入验收，仍是测试发行。

- 使用固定数据版本 `{lock['data_version']}`；词库、英文、U 模式和转换资源来自独立私有数据仓库。
- 两个既有正式模型明确复用，未重新训练；数据继续按各自原有许可管理。
- 使用本次源码生成资源，已核对安装时还原的词库和语言模型与构建结果一致。
- 正式数据的按键序列、词库导入、U/V 模式和配置同步四项自动检查均已通过。
- 包内包含 x64 程序及 x86 文本服务组件，自动更新在编译时关闭。
- 安装完成后，请从开始菜单打开珊瑚输入法。
- 安装包未签名；实际安装、升级、卸载及真实应用输入仍需验收。
- 沿用产品安装位置和标识，请在独立虚拟机验收。

'''
    notes += f'''程序版本：{version}
对应源码：https://github.com/{repository}/commit/{source_sha}
构建与测试记录：{run_url}

下载 `{installer_name}`；GitHub 自动附带的 Source code 文件是源码，不是安装包。
校验值见 SHA256SUMS.txt，构建来源见 BUILD_INFO.json。
'''
    (output_dir / 'RELEASE_NOTES.md').write_text(notes, encoding='utf-8')
    sums = ''.join(digest(path) + '  ' + path.name + '\n'
                   for path in sorted(output_dir.iterdir()))
    (output_dir / 'SHA256SUMS.txt').write_text(sums, encoding='utf-8')
    print('Release files: ' + str(output_dir))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    validate = commands.add_parser('validate-version')
    validate.add_argument('version', type=app_version)
    prepare = commands.add_parser('bundle')
    prepare.add_argument('--package-dir', type=Path, required=True)
    prepare.add_argument('--output-dir', type=Path, required=True)
    prepare.add_argument('--version', type=app_version, required=True)
    prepare.add_argument('--source-sha', required=True)
    prepare.add_argument('--repository', required=True)
    prepare.add_argument('--run-url', required=True)
    prepare.add_argument('--edition', choices=('demo', 'official'), default='demo')
    prepare.add_argument('--data-lock', type=Path)
    prepare.add_argument('--test-summary', type=Path)
    args = parser.parse_args()
    try:
        if args.command == 'bundle':
            bundle(args.package_dir, args.output_dir, args.version, args.source_sha,
                   args.repository, args.run_url, args.edition, args.data_lock, args.test_summary)
        else:
            print('Valid demo version: ' + args.version)
    except (ValueError, OSError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
