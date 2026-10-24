"""将已经校验的 GitHub 安装包同步到官网，仅使用独立的 CI 发布凭据。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import urllib.error
import urllib.request

from demo_release import app_version, digest

ORIGIN = 'https://coralime.cn'


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, target):
        raise ValueError('Deployment redirects are not allowed')


def verified_release(directory, version):
    app_version(version)
    directory = Path(directory)
    info = json.loads((directory / 'BUILD_INFO.json').read_text(encoding='utf-8'))
    manifest = json.loads((directory / 'package-manifest.json').read_text(encoding='utf-8'))
    name = info.get('installer', {}).get('name', '')
    if (not re.fullmatch(r'coralpinyin-official-test-windows-x64-' + re.escape(version) + r'\.exe', name)
            or info.get('version') != version or info.get('edition') != 'official-data-test'
            or info.get('automatic_updates') is not True or info.get('repository') != 'Coral-IME/Coral-IME'
            or not re.fullmatch(r'[0-9a-f]{40}', info.get('source_sha', ''))
            or manifest.get('version') != version or manifest.get('complete') is not True):
        raise ValueError('Expected a verified, update-enabled official installer')
    model = info.get('character_model', {})
    if (model.get('precision') != 'int8' or {key: model.get(key) for key in ('size', 'sha256')} != manifest.get('character_model')
            or not info.get('formal_data_tests', {}).get('passed')):
        raise ValueError('Expected the pinned INT8 model and passed formal-data checks')
    installer = directory / name
    actual = digest(installer)
    if actual != info['installer'].get('sha256') or actual != manifest.get('installer_sha256'):
        raise ValueError('Installer checksum does not match its build records')
    inventory = {}
    for line in (directory / 'SHA256SUMS.txt').read_text(encoding='utf-8').splitlines():
        match = re.fullmatch(r'([0-9a-f]{64})  ([A-Za-z0-9_.-]+)', line)
        if not match or match[2] in inventory:
            raise ValueError('Invalid release checksum inventory')
        inventory[match[2]] = match[1]
        if digest(directory / match[2]) != match[1]:
            raise ValueError('Release attachment checksum mismatch')
    if not {'BUILD_INFO.json', 'package-manifest.json', name}.issubset(inventory):
        raise ValueError('Release checksum inventory is incomplete')
    return installer, info


def deploy(directory, version, key, description):
    installer, info = verified_release(directory, version)
    if not key or len(key) < 32:
        raise ValueError('Website deployment credential is missing')
    if not description.strip() or len(description.encode('utf-8')) > 4096:
        raise ValueError('Release description is missing or too long')
    client = urllib.request.build_opener(NoRedirect())

    def request(path, data, method='POST', binary=False):
        headers = {'Authorization': 'Bearer ' + key, 'Content-Type': 'application/octet-stream' if binary else 'application/json'}
        if binary:
            headers['Content-Length'] = str(installer.stat().st_size)
        else:
            data = json.dumps(data, ensure_ascii=False).encode('utf-8')
        call = urllib.request.Request(ORIGIN + path, data=data, method=method, headers=headers)
        try:
            with client.open(call, timeout=300) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            raise ValueError('Website deployment failed (HTTP %s)' % error.code) from None

    prepared = request('/api/deploy/releases', {
        'version': version, 'description': description, 'source_sha': info['source_sha'],
        'sha256': info['installer']['sha256'], 'size': installer.stat().st_size,
        'release_url': 'https://github.com/Coral-IME/Coral-IME/releases/tag/v' + version})['release']
    release_id = prepared['id']
    if type(release_id) is not int or not 1 <= release_id <= 2147483647:
        raise ValueError('Invalid server release identity')
    if prepared['status'] != 'published':
        with installer.open('rb') as stream:
            request(f'/api/deploy/releases/{release_id}/upload', stream, 'PUT', binary=True)
        published = request(f'/api/deploy/releases/{release_id}/publish', {})['release']
    else:
        published = prepared
    if (published['status'] != 'published' or published['version'] != version
            or published['fileSha256'] != info['installer']['sha256'] or published['fileSize'] != installer.stat().st_size):
        raise ValueError('Published package does not match the GitHub release')
    with client.open(ORIGIN + '/api/v1/version/getDownloadUrl?Id=' + str(release_id), timeout=30) as response:
        public = json.load(response)
    if public['data']['url'] != published['download_url']:
        raise ValueError('Public download route is not synchronized')
    print(json.dumps({'version': version, 'release_id': release_id, 'website': ORIGIN, 'published': True}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--release-dir', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--description', required=True)
    args = parser.parse_args()
    try:
        deploy(args.release_dir, args.version, os.environ.get('CORAL_RELEASE_API_KEY', ''), args.description)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
