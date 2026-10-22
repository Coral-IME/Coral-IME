"""校验演示版版本，并记录可下载安装包的源码、构建来源与摘要。"""

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


def bundle(package_dir, output_dir, version, source_sha, repository, run_url):
    app_version(version)
    if not re.fullmatch(r'[0-9a-f]{40}', source_sha):
        raise ValueError('A full source commit SHA is required')
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repository):
        raise ValueError('Invalid GitHub repository')
    if not re.fullmatch(r'https://github\.com/' + re.escape(repository) + r'/actions/runs/[0-9]+', run_url):
        raise ValueError('The build URL must belong to this GitHub repository')
    manifest_path = package_dir / 'package-manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    if (manifest.get('complete') is not True or manifest.get('installer_compiled') is not True
            or manifest.get('version') != version or manifest.get('sign_mode') != 'None'):
        raise ValueError('Expected a complete, unsigned package with the requested version')
    installer = package_dir / ('coralpinyin-window-x64-' + version + '.exe')
    installer_hash = digest(installer)
    if manifest.get('installer_sha256') != installer_hash:
        raise ValueError('Installer SHA-256 differs from the package manifest')

    # 新建输出目录，防止重跑时把旧安装包或无关文件混入下载产物。
    output_dir.mkdir(parents=True, exist_ok=False)
    installer_name = 'coralpinyin-demo-windows-x64-' + version + '.exe'
    shutil.copyfile(installer, output_dir / installer_name)
    shutil.copyfile(manifest_path, output_dir / 'package-manifest.json')
    record = {
        'format_version': 1,
        'edition': 'demo',
        'version': version,
        'repository': repository,
        'source_sha': source_sha,
        'build_url': run_url,
        'data': 'self-authored public demo fixtures; not the official dictionary or model',
        'signed': False,
        'automatic_updates': False,
        'installation_verified': False,
        'installer': {'name': installer_name, 'sha256': installer_hash},
    }
    (output_dir / 'BUILD_INFO.json').write_text(
        json.dumps(record, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    notes = f'''本版本仅用于验证自动构建和安装包分发，不代表正式输入质量。

- 使用仓库自制的小型演示词库及模型，未包含正式数据。
- 包内包含 x64 程序及 x86 文本服务组件，自动更新在编译时关闭。
- 安装包未签名；Windows 可能显示未知发布者提示。
- 自动编译与测试通过不代表已完成实际安装、升级、卸载或真实应用输入验收。
- 演示包沿用产品安装位置和标识，请在独立虚拟机验收，避免覆盖日常使用版本。

程序版本：{version}
对应源码：https://github.com/{repository}/commit/{source_sha}
构建与测试记录：{run_url}

下载 `{installer_name}`；GitHub 自动附带的 Source code 文件是源码，不是安装包。
校验值见 SHA256SUMS.txt，构建来源见 BUILD_INFO.json。
'''
    (output_dir / 'RELEASE_NOTES.md').write_text(notes, encoding='utf-8')
    sums = ''.join(digest(path) + '  ' + path.name + '\n'
                   for path in sorted(output_dir.iterdir()))
    (output_dir / 'SHA256SUMS.txt').write_text(sums, encoding='utf-8')
    print('Demo release files: ' + str(output_dir))


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
    args = parser.parse_args()
    try:
        if args.command == 'bundle':
            bundle(args.package_dir, args.output_dir, args.version, args.source_sha,
                   args.repository, args.run_url)
        else:
            print('Valid demo version: ' + args.version)
    except (ValueError, OSError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
