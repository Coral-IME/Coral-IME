"""Build external data into a fresh runtime without changing inputs."""
import sys
sys.dont_write_bytecode=True
from pathlib import Path
import argparse,json,os,shutil,subprocess,sys,uuid
from paths import digest,output_path,require_files
from runtime_manifest import inventory,write_runtime,verify_runtime,character_model_info
from character_model_format import inspect_model,quantize_model

TOOLS=Path(__file__).resolve().parent
CONFIG=TOOLS.parents[1]/'resources/config'
INPUTS=('coral_lexicon.csv','coral_languagemodel.csv','umode_radical.csv','umode_stroke.csv','eng_lexicon.csv','pinyin_frequency.csv','dw.coral','opencc/s2t.json','opencc/symbol_word.coral','opencc/emoji_word.coral')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for flag in ('source-dir','output-dir','resource-compiler','lexicon-builder','bundle-decoder'):parser.add_argument('--'+flag,type=Path,required=True)
    parser.add_argument('--config-dir',type=Path,default=CONFIG)
    parser.add_argument('--model-dir',type=Path,help='Explicit reuse of two compiled models; no model retraining')
    parser.add_argument('--character-model',type=Path,help='Optional external CCHLM001 weights; copied with checksum provenance')
    parser.add_argument('--character-model-format',choices=('int8','preserve'),default='int8',help='Store character matrices as INT8 by default; preserve keeps original precision')
    parser.add_argument('--data-version',required=True);parser.add_argument('--engine-version',required=True)
    parser.add_argument('--source-manifest',type=Path,help='Prior source manifest (format 2) to verify before building')
    args=parser.parse_args()
    try:
        source=require_files(args.source_dir,INPUTS);config=require_files(args.config_dir,('coralpinyinConfig.ini',))
        for path in (args.resource_compiler,args.lexicon_builder,args.bundle_decoder):
            if not path.is_file():raise ValueError('Missing compiled tool: '+str(path))
        protected=[source,config,args.resource_compiler,args.lexicon_builder,args.bundle_decoder];models=None
        if args.model_dir:
            models=require_files(args.model_dir,('correction_pinyin_model.dat','char_lm.dat'));protected.append(models)
        character_info=None
        if args.character_model:
            character_info=character_model_info(args.character_model)
            protected.append(args.character_model)
        if args.source_manifest:
            from runtime_manifest import verify
            verify(source,json.loads(args.source_manifest.read_text(encoding='utf-8')))
        source_hashes=inventory(source);root=output_path(args.output_dir,protected,directory=True)
        intermediate=root/'intermediate';intermediate.mkdir();logs=root/'logs';logs.mkdir()
        env=os.environ.copy();env['PYTHONIOENCODING']='utf-8';env['CORAL_TEST_NAMESPACE']=uuid.uuid4().hex
        env['CORAL_USER_DATA_DIR']=str(root/'generator-user');env.pop('CORAL_LANGENGINE_DATA_DIR',None)
        steps=[]
        def run(name,command):
            print('Running '+name,flush=True)
            with (logs/(name+'.log')).open('wb') as stream:
                result=subprocess.run([str(v) for v in command],cwd=root,env=env,stdout=stream,stderr=subprocess.STDOUT)
            steps.append({'name':name,'command':[str(v) for v in command],'exit_code':result.returncode})
            (root/'build-record.json').write_text(json.dumps({'steps':steps,'complete':False},indent=2)+'\n',encoding='utf-8')
            if result.returncode:raise ValueError('Failed '+name+'; see '+str(logs/(name+'.log')))
        run('index',[sys.executable,TOOLS/'index_lexicon.py','--input',source/'coral_lexicon.csv','--output',intermediate/'index_coral_lexicon.csv','--english',source/'eng_lexicon.csv'])
        run('bigram',[sys.executable,TOOLS/'index_bigram.py','--input',source/'coral_languagemodel.csv','--index',intermediate/'index_coral_lexicon.csv','--output-dir',intermediate/'bigram','--no-emoji'])
        run('lexicons',[sys.executable,TOOLS/'generate_lexicons.py','--source-dir',source,'--index',intermediate/'index_coral_lexicon.csv','--bigram',intermediate/'bigram/converted_coral_languagemodel.csv','--output-dir',root,'--resource-compiler',args.resource_compiler.resolve()])
        general=root/'data/generallexicon'
        if character_info:
            character_target=general/'character_model.coral'
            source_character_info=character_info
            if args.character_model_format=='int8' and inspect_model(args.character_model)[0]==1:
                quantize_model(args.character_model,character_target)
            else:
                shutil.copyfile(args.character_model,character_target)
            character_info=character_model_info(character_target)
            if character_model_info(args.character_model)!=source_character_info:
                raise ValueError('Character model changed during copying')
        if models:
            for name in ('correction_pinyin_model.dat','char_lm.dat'):shutil.copyfile(models/name,general/name)
        else:
            run('char-model',[sys.executable,TOOLS/'build_char_lm.py','--input',source/'pinyin_frequency.csv','--lexicon',source/'coral_lexicon.csv','--output',general/'char_lm.dat'])
            run('mixed-model',[sys.executable,TOOLS/'build_mixed_pinyin_model.py','--input',source/'coral_lexicon.csv','--output',general/'correction_pinyin_model.dat'])
        # 使用与安装程序相同的权威 CSV 模式；避免从旧的
        # 中文键 marisa 输出推导引擎音节索引。
        run('lexicon-tables',[args.lexicon_builder.resolve(),'--csv',source/'coral_lexicon.csv',source/'coral_languagemodel.csv',general,source/'eng_lexicon.csv'])
        run('dictpack',[sys.executable,TOOLS/'encode_dictpack.py',source/'coral_lexicon.csv',intermediate/'bigram/bundle_coral_languagemodel.csv',root/'dictionary_bundle.dictpack.lzma2','--english',source/'eng_lexicon.csv'])
        decoded=intermediate/'install-decoded';decoded.mkdir()
        run('decode-check',[args.bundle_decoder.resolve(),root/'dictionary_bundle.dictpack.lzma2',decoded/'coral_lexicon.csv',decoded/'coral_languagemodel.csv'])
        run('install-tables-check',[args.lexicon_builder.resolve(),'--csv',decoded/'coral_lexicon.csv',decoded/'coral_languagemodel.csv',intermediate/'install-bins'])
        for name in ('mainlexicon.coral','languagemodel.coral'):
            if digest(general/name)!=digest(intermediate/'install-bins'/name):raise ValueError('Installer reconstruction differs: '+name)
        shutil.copytree(config,root/'config');shutil.copytree(config/'doublepinyin',root/'data/doublepinyin')
        shutil.copytree(source/'opencc',root/'data/opencc');shutil.copyfile(source/'dw.coral',root/'data/dw.coral')
        write_runtime(root,args.data_version);verify_runtime(root)
        if inventory(source)!=source_hashes:raise ValueError('Source changed during generation')
        record={'format_version':3,'complete':True,'data_version':args.data_version,'engine_version':args.engine_version,
            'source_files':source_hashes,'config_files':inventory(config),'resource_compiler_sha256':digest(args.resource_compiler),'coral_lexicon_builder_sha256':digest(args.lexicon_builder),
            'coral_bundle_decoder_sha256':digest(args.bundle_decoder),'installer_bins_match':True,
            'model_mode':'explicit-reuse' if models else 'rebuilt',
            'character_model':character_info,
            'character_model_source':source_character_info if character_info else None,
            'character_model_format':('int8' if inspect_model(character_target)[0]==2 else 'float32') if character_info else None,
            'reused_models':{n:{'sha256':digest(models/n),'size':(models/n).stat().st_size} for n in ('correction_pinyin_model.dat','char_lm.dat')} if models else None,
            'tool_files':inventory(TOOLS,exclude=tuple(p.relative_to(TOOLS).as_posix() for p in TOOLS.rglob('*.pyc'))),'steps':steps}
        (root/'build-record.json').write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        manifest={'format_version':2,'data_version':args.data_version,'engine_version':args.engine_version,
            'excluded_files':['runtime-manifest.json'],'files':inventory(root,exclude=('runtime-manifest.json',))}
        (root/'runtime-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print('Runtime completed: '+str(root))
    except (ValueError,OSError) as error:parser.exit(1,str(error)+'\n')

if __name__=='__main__':main()
