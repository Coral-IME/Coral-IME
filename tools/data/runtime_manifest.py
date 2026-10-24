"""Explicit checksums; no implicit Git metadata or author paths."""
import sys
sys.dont_write_bytecode=True
from pathlib import Path
import configparser
import hashlib
import struct
from character_model_format import inspect_model
from paths import digest

ENGINE_FILES={'BushouTrie':'data/bushou.marisa','BushouData':'data/bushou.dat','StrokeTrie':'data/stroke.marisa','StrokeData':'data/stroke.dat',
 'BigramGBin':'data/generallexicon/languagemodel.coral','EnglishData':'data/generallexicon/english.dat','EnglishMarisa':'data/generallexicon/english.marisa',
 'PrimaryData':'data/generallexicon/primary.dat','PrimaryMarisa':'data/generallexicon/primary.marisa',
 'CorrectionData':'data/generallexicon/correction_pinyin_model.dat','CharLMData':'data/generallexicon/char_lm.dat'}

def character_model_info(path):
    # 两种精度共用布局校验；网络数学约束仍由原生加载器验证。
    path=Path(path)
    inspect_model(path)
    size=path.stat().st_size
    return {'sha256':digest(path),'size':size}

def inventory(root,exclude=()):
    root=Path(root).resolve();rows={}
    for path in sorted(root.rglob('*')):
        if path.is_symlink():raise ValueError('Symlinks are not supported: '+str(path))
        if path.is_file() and path.relative_to(root).as_posix() not in exclude:
            rows[path.relative_to(root).as_posix()]={'sha256':digest(path),'size':path.stat().st_size}
    return rows

def verify(root,manifest):
    if manifest.get('format_version')!=2:raise ValueError('Expected manifest format_version 2')
    current=inventory(root,manifest.get('excluded_files',[]));expected=manifest['files']
    changed=[p for p in sorted(set(current)|set(expected)) if current.get(p)!=expected.get(p)]
    if changed:raise ValueError('Manifest mismatch: '+', '.join(changed))
    return len(current)

def write_runtime(root,version):
    root=Path(root);cfg=configparser.ConfigParser(interpolation=None);cfg.optionxform=str
    cfg.read(root/'config/coralpinyinConfig.ini',encoding='utf-8-sig');cfg['app']['Version']=version
    cfg['LexiconFiles']={key:digest(root/relative,'md5') for key,relative in ENGINE_FILES.items()}
    with (root/'config/coralpinyinConfig.ini').open('w',encoding='utf-8') as stream:cfg.write(stream)

def verify_runtime(root):
    root=Path(root);cfg=configparser.ConfigParser(interpolation=None)
    cfg.read(root/'config/coralpinyinConfig.ini',encoding='utf-8-sig')
    for key,relative in ENGINE_FILES.items():
        if cfg.get('LexiconFiles',key,fallback='').lower()!=digest(root/relative,'md5'):raise ValueError('Runtime config checksum mismatch: '+relative)
    if not (root/'data/generallexicon/mainlexicon.coral').is_file():raise ValueError('Missing mainlexicon.coral')
    return len(ENGINE_FILES)
