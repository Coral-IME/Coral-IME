"""Build isolated, self-authored variants and require one normal process exit."""
from pathlib import Path
import hashlib
import os
import re
import shutil
import subprocess
import sys
import uuid


def set_config(path, key, value):
    text = path.read_bytes().decode('utf-8')
    text, count = re.subn(r'(?m)^' + re.escape(key) + r'\s*=\s*[^\r\n]*', key + ' = ' + value, text)
    if count != 1:
        raise ValueError(f'Expected one fixture key: {key}')
    path.write_bytes(text.encode('utf-8'))


def main():
    executable, runtime, root = (Path(value).resolve() for value in sys.argv[1:])
    token = uuid.uuid4().hex
    scope = root / token
    first, second, invalid = (scope / name for name in ('first', 'second', 'invalid'))
    for destination in (first, second, invalid):
        shutil.copytree(runtime, destination)
    primary = second / 'data/generallexicon/primary.dat'
    original = primary.read_bytes()
    modified = original.replace('你好'.encode(), '您好'.encode())
    if original == modified:
        raise ValueError('Greeting missing from self-authored demo data')
    primary.write_bytes(modified)
    set_config(second / 'config/coralpinyinConfig.ini', 'PrimaryData', hashlib.md5(modified).hexdigest())
    # 运行时查询使用编译后的表。在不改变记录宽度、偏移或词典
    # 格式的前提下保持人造变体的一致性。
    table = second / 'data/generallexicon/mainlexicon.coral'
    original_table = table.read_bytes()
    modified_table = original_table.replace('你好'.encode(), '您好'.encode())
    if original_table == modified_table:
        raise ValueError('Greeting missing from compiled demo table')
    table.write_bytes(modified_table)
    set_config(second / 'config/coralpinyinConfig.ini', 'isUMode', 'false')
    conversion = second / 'data/opencc/demo_s2t.txt'
    original_conversion = conversion.read_bytes()
    modified_conversion = original_conversion.replace('國'.encode(), '囯'.encode())
    if original_conversion == modified_conversion:
        raise ValueError('Conversion missing from self-authored demo data')
    conversion.write_bytes(modified_conversion)
    set_config(invalid / 'config/coralpinyinConfig.ini', 'CorrectionData', 'incorrect-checksum')
    user = scope / 'users'
    user.mkdir()
    env = os.environ.copy()
    env.update(CORAL_TEST_NAMESPACE=token, CORAL_USER_DATA_DIR=str(user))
    return subprocess.run([str(executable), str(first), str(second), str(invalid), str(user)],
                          env=env, timeout=30).returncode


if __name__ == '__main__':
    raise SystemExit(main())
