"""Finalize only the self-authored demo runtime; uses Python standard library."""
from pathlib import Path
import argparse
import configparser
import hashlib
import json
import shutil

def digest(path,algorithm='sha256'):
    h=hashlib.new(algorithm)
    with path.open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):h.update(block)
    return h.hexdigest()

def main():
    parser=argparse.ArgumentParser();parser.add_argument('runtime',type=Path);parser.add_argument('config',type=Path);parser.add_argument('fixtures',type=Path)
    args=parser.parse_args();root=args.runtime.resolve()
    shutil.copytree(args.config,root/'config',dirs_exist_ok=True)
    shutil.copytree(args.config/'doublepinyin',root/'data/doublepinyin',dirs_exist_ok=True)
    ini=root/'config/coralpinyinConfig.ini';cfg=configparser.ConfigParser(interpolation=None);cfg.optionxform=str
    cfg.read(ini,encoding='utf-8-sig');cfg['app']['Version']='demo-v1';cfg['app']['DataKind']='self-authored-demo'
    cfg['settingsconfig']['isAutoCheck']='false'
    files={'BushouTrie':'data/bushou.marisa','BushouData':'data/bushou.dat','StrokeTrie':'data/stroke.marisa','StrokeData':'data/stroke.dat',
      'BigramGBin':'data/generallexicon/languagemodel.coral','EnglishData':'data/generallexicon/english.dat','EnglishMarisa':'data/generallexicon/english.marisa',
      'PrimaryData':'data/generallexicon/primary.dat','PrimaryMarisa':'data/generallexicon/primary.marisa',
      'CorrectionData':'data/generallexicon/correction_pinyin_model.dat','CharLMData':'data/generallexicon/char_lm.dat'}
    cfg['LexiconFiles']={key:digest(root/path,'md5') for key,path in files.items()}
    for path in ('data/generallexicon/mainlexicon.coral','data/opencc/s2t.json','data/dw.coral'):
        if not (root/path).is_file():raise RuntimeError('Incomplete demo runtime: '+path)
    with ini.open('w',encoding='utf-8') as out:cfg.write(out)
    manifest={'format_version':1,'data_kind':'self-authored-demo','license':'MPL-2.0','data_version':'demo-v1',
       'quality':'Toy vocabulary and synthetic uniform correction counts; not a production model.',
       'inputs':{p.relative_to(args.fixtures).as_posix():digest(p) for p in sorted(args.fixtures.rglob('*')) if p.is_file()},
       'files':{p.relative_to(root).as_posix():{'sha256':digest(p),'bytes':p.stat().st_size} for folder in ('data','config') for p in sorted((root/folder).rglob('*')) if p.is_file()}}
    (root/'demo-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (root/'demo.stamp').write_text('demo-v1\n',encoding='utf-8')
    print('Demo manifest finalized:',len(manifest['files']),'runtime files')

if __name__=='__main__':main()
