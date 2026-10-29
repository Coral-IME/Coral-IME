"""Stage a Windows package from explicit compiled binaries and a paired runtime."""
from pathlib import Path
import argparse,configparser,hashlib,json,os,re,shutil,struct,subprocess,sys,time
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'data'))
from paths import CODE_ROOT,digest,output_path,require_files
from runtime_manifest import inventory,verify,verify_runtime,character_model_info
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'checks'))
from verify_engine_linkage import imported_libraries
from product_identity import load_identity
from surface_model_format import HEADER,TOKEN,ARC,PHRASE
from compact_tables import pack_tables

IDENTITY=load_identity()
OWNED=tuple(IDENTITY[key] for key in ('settings_exe','update_exe','diagnostic_exe','service_exe','session_host_exe','renderer_exe','tsf_x64_dll'))
GENERATORS=('coral_lexicon_builder.exe','coral_bundle_decoder.exe')
BUILD_TOOLS=('coral_resource_compiler.exe',)

def stamp_package_version(path,version):
    # 运行模板使用 0.0.0.0；发行副本必须写入真实版本，供关于页和更新检查一致读取。
    with Path(path).open('r',encoding='utf-8',newline='') as stream:lines=stream.readlines()
    section='';changed=0;output=[]
    for line in lines:
        header=re.match(r'\s*\[([^\]]+)\]',line)
        if header:section=header[1].lower()
        if section=='app' and re.match(r'\s*Version\s*=',line,re.IGNORECASE):
            line=re.sub(r'^(\s*Version\s*=\s*)[^\r\n]*',lambda match:match[1]+version,line,flags=re.IGNORECASE)
            changed+=1
        output.append(line)
    if changed!=1:raise ValueError('Package configuration requires one app/Version')
    with Path(path).open('w',encoding='utf-8',newline='') as stream:stream.writelines(output)

LEGAL_DOCUMENTS=('用户协议.md','隐私政策.md','许可说明.md')

def stage_legal_documents(package):
    # 与官网使用同一份正文，安装前可读，安装后保留离线副本。
    legal_dir=Path(package)/'legal';legal_dir.mkdir()
    for name in LEGAL_DOCUMENTS:
        shutil.copyfile(CODE_ROOT/'docs/legal'/name,legal_dir/name)
    notice=("珊瑚输入法 · 安装前须知\n"
            "提供者：个人开发者（公开署名：Coral-IME）\n"
            "联系邮箱：coralime@126.com\n\n"
            "继续安装表示接受以下用户协议；隐私政策供阅读。\n"
            "这不概括授权上传，不代替可选云翻译或反馈附件的主动选择。\n\n")
    for name in ('用户协议.md','隐私政策.md'):
        notice+=(CODE_ROOT/'docs/legal'/name).read_text(encoding='utf-8-sig')+'\n\n'
    (legal_dir/'安装前须知.txt').write_bytes(notice.replace('\r\n','\n').replace('\n','\r\n').encode('utf-16'))

def stage_migration_tool(package):
    # 工具只携带公开脚本，运行时由用户选择本机词库。
    tools=Path(package)/'tools';tools.mkdir()
    shutil.copyfile(CODE_ROOT/'tools/data/ime_dictionary_tool.py',tools/'ime_dictionary_tool.py')

def machine(path):
    with Path(path).open('rb') as stream:
        if stream.read(2)!=b'MZ':raise ValueError('Not a PE binary: '+str(path))
        stream.seek(0x3c);offset=struct.unpack('<I',stream.read(4))[0];stream.seek(offset)
        if stream.read(4)!=b'PE\0\0':raise ValueError('Invalid PE header')
        return struct.unpack('<H',stream.read(2))[0]

def package_character_model(runtime,provenance,required=False):
    path=Path(runtime)/'data/generallexicon/character_model.coral'
    expected=provenance.get('character_model')
    if not path.exists() and expected is None and not required:return None
    if not path.is_file() or not expected:
        raise ValueError('Required character model is missing or has no build provenance')
    actual=character_model_info(path)
    if actual!=expected:raise ValueError('Character model differs from build provenance')
    return actual

def precompiled_tables_info(directory):
    # 显式模型绑定词库ID；安装端必须原样使用，不能再从旧CSV重建。
    directory=require_files(directory,('mainlexicon.coral','languagemodel.coral'))
    model=directory/'languagemodel.coral'
    with model.open('rb') as stream:
        raw=stream.read(HEADER.size)
        if len(raw)!=HEADER.size:raise ValueError('Truncated precompiled surface model')
        h=HEADER.unpack(raw)
        if (h[0]!=b'CSLM0001' or h[1]!=1 or h[2]!=HEADER.size or h[3]!=model.stat().st_size
            or h[3]!=HEADER.size+h[4]*TOKEN.size+h[5]*ARC.size+h[6]*PHRASE.size+h[7]*4
            or h[9]!=0 or h[12]!=bytes(16)):
            raise ValueError('Invalid precompiled surface model layout')
        body=hashlib.sha256()
        for chunk in iter(lambda:stream.read(1048576),b''):body.update(chunk)
    if h[10].hex()!=digest(directory/'mainlexicon.coral'):
        raise ValueError('Precompiled model is bound to a different dictionary')
    if h[11]!=body.digest():raise ValueError('Precompiled model body checksum mismatch')
    return {'format':'CSLM0001','dictionary_entries_binding':'sha256','states':h[4],'explicit_arcs':h[5],
        'files':{name:{'sha256':digest(directory/name),'size':(directory/name).stat().st_size}
                 for name in ('mainlexicon.coral','languagemodel.coral')}}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,required=True,help='Configured x64 build tree, Release outputs')
    parser.add_argument('--x86-tsf',type=Path,required=True);parser.add_argument('--runtime-dir',type=Path,required=True)
    parser.add_argument('--output-dir',type=Path,required=True);parser.add_argument('--version',required=True)
    parser.add_argument('--makensis',type=Path,help='Optional explicit compiler; omitted means staging only')
    parser.add_argument('--sign-mode',choices=['None','LocalTest','Store','Artifact'],default='None')
    parser.add_argument('--require-character-model',action='store_true',help='Reject packages missing recorded CCHLM001 weights')
    parser.add_argument('--precompiled-tables-dir',type=Path,help='Explicit paired mainlexicon.coral and CSLM0001 model; deploy without CSV rebuilding')
    parser.add_argument('--precompiled-tables-storage',choices=['compact','direct'],default='compact',help='Compact lossless installer reconstruction (default), or direct tables for diagnostics')
    parser.add_argument('--compact-tables-bundle',type=Path,help='Reuse an explicit CSTPACK1 bundle; native restoration and exact pairing are still verified')
    parser.add_argument('--demo',action='store_true',help='Mark the demo installer; launch manually from the Start menu')
    parser.add_argument('--manual-launch',action='store_true',help='Launch from the Start menu; use standard NSIS without ShellExecAsUser')
    args=parser.parse_args()
    try:
        if not re.fullmatch(r'\d+\.\d+\.\d+\.\d+',args.version) or any(int(x)>65535 for x in args.version.split('.')):raise ValueError('Version must have four components, each <= 65535')
        if args.compact_tables_bundle and (not args.precompiled_tables_dir or args.precompiled_tables_storage!='compact'):
            raise ValueError('--compact-tables-bundle requires compact precompiled tables')
        binaries=require_files(args.build_dir/'coralpinyin/Release',OWNED+GENERATORS+BUILD_TOOLS)
        runtime=require_files(args.runtime_dir,('runtime-manifest.json','build-record.json','config/coralpinyinConfig.ini')+
            (() if args.precompiled_tables_dir else ('dictionary_bundle.dictpack.lzma2',)))
        manifest=json.loads((runtime/'runtime-manifest.json').read_text(encoding='utf-8'))
        verify(runtime,manifest);verify_runtime(runtime)
        provenance=json.loads((runtime/'build-record.json').read_text(encoding='utf-8'))
        if provenance.get('format_version')!=3:
            raise ValueError('Runtime build record requires version 3; rebuild with the current data tools')
        if not provenance.get('complete') or not provenance.get('installer_bins_match'):
            raise ValueError('Runtime is incomplete or installer reconstruction has not been verified')
        tables_info=None
        if args.precompiled_tables_dir:
            tables_info=precompiled_tables_info(args.precompiled_tables_dir)
            probe=binaries/'coral_lm_probe.exe'
            if not probe.is_file():raise ValueError('Missing native surface-model probe')
            result=subprocess.run([str(probe),str(args.precompiled_tables_dir/'languagemodel.coral'),str(args.precompiled_tables_dir/'mainlexicon.coral')],input=b'',capture_output=True)
            if result.returncode:raise ValueError('Precompiled model native loading failed: '+result.stderr.decode('utf-8',errors='replace'))
            tables_info.update({'native_load_verified':True,'native_probe_sha256':digest(probe)})
            if args.precompiled_tables_storage=='compact':
                restore=require_files(binaries,('coral_table_restore.exe',))/'coral_table_restore.exe'
                if machine(restore)!=0x8664:raise ValueError('Expected x64 table restore tool')
                if {'langengine.dll','coral_engine_core.dll'}.intersection(imported_libraries(restore)):
                    raise ValueError('Table restore tool depends on a removed engine DLL')
        else:
            for field,name in [('resource_compiler_sha256','coral_resource_compiler.exe'),('coral_lexicon_builder_sha256','coral_lexicon_builder.exe'),('coral_bundle_decoder_sha256','coral_bundle_decoder.exe')]:
                if provenance.get(field)!=digest(binaries/name):raise ValueError('Runtime/compiler pairing differs: '+name)
        if machine(args.x86_tsf)!=0x14c:raise ValueError('--x86-tsf must be a Win32 DLL')
        for name in OWNED+GENERATORS+BUILD_TOOLS:
            if machine(binaries/name)!=0x8664:raise ValueError('Expected x64 binary: '+name)
            if {'langengine.dll','coral_engine_core.dll'}.intersection(imported_libraries(binaries/name)):
                raise ValueError('Binary still depends on a removed engine DLL: '+name)
        character_info=package_character_model(runtime,provenance,args.require_character_model)
        if args.makensis and not args.makensis.is_file():raise ValueError('NSIS compiler missing')
        root=output_path(args.output_dir,[args.build_dir,runtime,args.x86_tsf]+([args.precompiled_tables_dir] if tables_info else [])+
            ([args.compact_tables_bundle] if args.compact_tables_bundle else []),directory=True)
        package=root/'package/x64';package.mkdir(parents=True)
        for name in OWNED:shutil.copyfile(binaries/name,package/name)
        stage_migration_tool(package)
        shutil.copyfile(args.x86_tsf,package/IDENTITY['tsf_x86_dll'])
        res=package/'res';res.mkdir()
        for name in ('pinyin.ico','zh.ico','en.ico','A.ico'):
            component='coralpinyin' if name=='pinyin.ico' else 'coraltsf'
            path=CODE_ROOT/'src/platform/windows'/component/'res'/name
            if not path.is_file():raise ValueError('Missing public icon: '+str(path))
            shutil.copyfile(path,res/name)
        shutil.copyfile(CODE_ROOT/'resources/icons/coral_logo.ico',res/'coral_logo.ico')
        shutil.copytree(CODE_ROOT/'src/platform/windows/renderer/skins',res/'skins')
        (package/'config').mkdir();shutil.copyfile(runtime/'config/coralpinyinConfig.ini',package/'config/coralpinyinConfig.ini')
        stamp_package_version(package/'config/coralpinyinConfig.ini',args.version)
        data=package/'data';general=data/'generallexicon';general.mkdir(parents=True)
        for name in ('bushou.marisa','bushou.dat','stroke.marisa','stroke.dat','dw.coral'):shutil.copyfile(runtime/'data'/name,data/name)
        for name in ('english.dat','english.marisa','correction_pinyin_model.dat','char_lm.dat'):shutil.copyfile(runtime/'data/generallexicon'/name,general/name)
        if character_info:
            shutil.copyfile(runtime/'data/generallexicon/character_model.coral',general/'character_model.coral')
            if character_model_info(general/'character_model.coral')!=character_info:
                raise ValueError('Character model changed during package collection')
        if tables_info:
            tables_info['storage']=args.precompiled_tables_storage
            if args.precompiled_tables_storage=='compact':
                bundle=general/'surface_tables.cstpack.xz'
                if args.compact_tables_bundle:shutil.copyfile(args.compact_tables_bundle,bundle)
                else:pack_tables(args.precompiled_tables_dir,bundle)
                helper=general/'coral_table_restore.exe';shutil.copyfile(restore,helper)
                restored=root/'restore-check';started=time.perf_counter()
                with (root/'restore-check.log').open('wb') as log:
                    subprocess.run([str(helper),str(bundle),str(restored)],stdout=log,stderr=subprocess.STDOUT,check=True)
                for name in ('mainlexicon.coral','languagemodel.coral'):
                    if digest(restored/name)!=tables_info['files'][name]['sha256']:
                        raise ValueError('Installer reconstruction differs: '+name)
                native=subprocess.run([str(probe),str(restored/'languagemodel.coral'),str(restored/'mainlexicon.coral')],input=b'',capture_output=True)
                if native.returncode:raise ValueError('Restored model native loading failed')
                tables_info.update({'compact_bundle':{'sha256':digest(bundle),'size':bundle.stat().st_size},
                    'restore_tool_sha256':digest(helper),'installer_restoration_verified':True,'restore_seconds':time.perf_counter()-started})
            else:
                for name in ('mainlexicon.coral','languagemodel.coral'):
                    shutil.copyfile(args.precompiled_tables_dir/name,general/name)
                    if digest(general/name)!=tables_info['files'][name]['sha256']:raise ValueError('Precompiled tables changed during copying')
            config=package/'config/coralpinyinConfig.ini';cfg=configparser.ConfigParser(interpolation=None);cfg.optionxform=str
            cfg.read(config,encoding='utf-8-sig');cfg.set('LexiconFiles','BigramGBin',digest(args.precompiled_tables_dir/'languagemodel.coral','md5'))
            with config.open('w',encoding='utf-8') as stream:cfg.write(stream)
        else:
            shutil.copyfile(runtime/'dictionary_bundle.dictpack.lzma2',general/'dictionary_bundle.dictpack.lzma2')
            for name in GENERATORS:shutil.copyfile(binaries/name,general/name)
        shutil.copytree(runtime/'data/opencc',data/'opencc');shutil.copytree(runtime/'data/doublepinyin',data/'doublepinyin')
        # 源码与二进制发行保留相同的组件条款。
        notice_inventory=json.loads((CODE_ROOT/'third_party/manifest.json').read_text(encoding='utf-8'))
        for name in ('LICENSE','LICENSING.md','THIRD_PARTY_NOTICES.md'):
            shutil.copyfile(CODE_ROOT/name,package/name)
        shutil.copyfile(CODE_ROOT/'third_party/manifest.json',package/'THIRD_PARTY_MANIFEST.json')
        stage_legal_documents(package)
        license_dir=package/'licenses';license_dir.mkdir()
        copied_licenses=[]
        for component in notice_inventory['components']:
            for entry in component['license_files']:
                source=CODE_ROOT/entry['path']
                if digest(source)!=entry['sha256']:raise ValueError('Third-party license inventory is stale: '+entry['path'])
                dest=license_dir/(component['id']+'-'+source.name)
                shutil.copyfile(source,dest);copied_licenses.append(dest.relative_to(package).as_posix())
        sign=CODE_ROOT/'packaging/windows/sign_windows.ps1'
        owned=[*(package/name for name in OWNED),'coraltsf32.dll']
        owned=[package/IDENTITY['tsf_x86_dll'] if path=='coraltsf32.dll' else path for path in owned]+([] if tables_info else [general/name for name in GENERATORS])
        if tables_info and args.precompiled_tables_storage=='compact':owned.append(general/'coral_table_restore.exe')
        if args.sign_mode!='None':
            for path in owned:subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(sign),'-Mode',args.sign_mode,'-FilePath',str(path)],check=True)
        installer=None
        if args.makensis:
            installer=root/('coralpinyin-window-x64-'+args.version+'.exe')
            command=[str(args.makensis.resolve()),'/NOCD','/INPUTCHARSET','UTF8','-DARCH=x64','-DDBG=0','-DPRODUCT_VERSION='+args.version,
                '-DPACKAGE_DIR='+str(package),'-DOUTPUT_FILE='+str(installer)]
            if character_info:command+=['-DCORAL_PACKAGE_CHARACTER_MODEL=1']
            if tables_info:command+=['-DCORAL_PACKAGE_PRECOMPILED_TABLES=1']
            if tables_info and args.precompiled_tables_storage=='compact':command+=['-DCORAL_PACKAGE_COMPACT_TABLES=1']
            if args.demo:command+=['-DCORAL_DEMO_PACKAGE=1']
            if args.demo or args.manual_launch:command+=['-DCORAL_MANUAL_LAUNCH=1']
            if args.sign_mode!='None':command+=['-DCORAL_SIGN_SCRIPT='+str(sign),'-DCORAL_SIGN_MODE='+args.sign_mode]
            command+=[str(CODE_ROOT/'packaging/windows/coralpinyin_lite.nsi')]
            with (root/'nsis.log').open('wb') as stream:subprocess.run(command,cwd=root,stdout=stream,stderr=subprocess.STDOUT,check=True)
            if args.sign_mode!='None':subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(sign),'-Mode',args.sign_mode,'-FilePath',str(installer)],check=True)
        record={'format_version':1,'complete':True,'version':args.version,'sign_mode':args.sign_mode,'installer_compiled':bool(installer),
            'edition':'demo' if args.demo else 'standard',
            'character_model':character_info,
            'precompiled_tables':tables_info,
            'manual_launch':args.demo or args.manual_launch,
            'runtime':{key:provenance.get(key) for key in ('data_version','engine_version','model_mode','installer_bins_match')},
            'runtime_manifest_sha256':digest(runtime/'runtime-manifest.json'),'x86_tsf_sha256':digest(args.x86_tsf),
            'files':inventory(package),'license_files':copied_licenses,
            'third_party_manifest_sha256':digest(CODE_ROOT/'third_party/manifest.json'),
            'installer_sha256':digest(installer) if installer else None}
        (root/'package-manifest.json').write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print('Package staged: '+str(package))
        if installer:print('Installer compiled (not executed): '+str(installer))
    except (ValueError,OSError,subprocess.CalledProcessError) as error:parser.exit(1,str(error)+'\n')

if __name__=='__main__':main()
