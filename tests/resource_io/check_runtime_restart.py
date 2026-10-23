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
    executable, runtime, root, builder = (Path(value).resolve() for value in sys.argv[1:])
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
    # 手写微型网络用于验证开关；不读取第三方权重或用户输入。
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'language_model'))
    from test_character_model import fixture
    character = scope / 'character.coral'
    character.write_bytes(fixture())
    env['CORAL_TEST_CHARACTER_MODEL'] = str(character)
    # 用户词目前只用于完整词查询；组句夹具需把同音词放到独立的人工主词典。
    neural = scope / 'neural'
    shutil.copytree(runtime, neural)
    lexicon = scope / 'lexicon.csv'
    lexicon.write_text("index,pinyin,frequency,word,fullPinyin,backprob,in_m2\n\n\n"
                       "0,zai,100000,在,zai,-10,1\n1,zai,90000,再,zai,-10,1\n", encoding='utf-8')
    bigram = scope / 'bigram.csv'
    bigram.write_text("prev_word,next_word,prob\n在,在,-0.1\n在,再,-0.2\n再,在,-0.1\n再,再,-0.2\n", encoding='utf-8')
    subprocess.run([str(builder), '--csv', str(lexicon), str(bigram), str(neural / 'data/generallexicon')],
                   env=env, check=True, timeout=10)
    set_config(neural / 'config/coralpinyinConfig.ini', 'BigramGBin',
               hashlib.md5((neural / 'data/generallexicon/languagemodel.coral').read_bytes()).hexdigest())
    set_config(neural / 'config/coralpinyinConfig.ini', 'Version', 'neural-switch-v1')
    env['CORAL_TEST_CHARACTER_RUNTIME'] = str(neural)
    return subprocess.run([str(executable), str(first), str(second), str(invalid), str(user)],
                          env=env, timeout=30).returncode


if __name__ == '__main__':
    raise SystemExit(main())
