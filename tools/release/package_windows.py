"""Stage a Windows package from explicit compiled binaries and a paired runtime."""
from pathlib import Path
import argparse,json,os,re,shutil,struct,subprocess,sys
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'data'))
from paths import CODE_ROOT,digest,output_path,require_files
from runtime_manifest import inventory,verify,verify_runtime
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'checks'))
from verify_engine_linkage import imported_libraries
from product_identity import load_identity

IDENTITY=load_identity()
OWNED=tuple(IDENTITY[key] for key in ('settings_exe','update_exe','diagnostic_exe','service_exe','session_host_exe','renderer_exe','tsf_x64_dll'))
GENERATORS=('coral_lexicon_builder.exe','coral_bundle_decoder.exe')
BUILD_TOOLS=('coral_resource_compiler.exe',)

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

def machine(path):
    with Path(path).open('rb') as stream:
        if stream.read(2)!=b'MZ':raise ValueError('Not a PE binary: '+str(path))
        stream.seek(0x3c);offset=struct.unpack('<I',stream.read(4))[0];stream.seek(offset)
        if stream.read(4)!=b'PE\0\0':raise ValueError('Invalid PE header')
        return struct.unpack('<H',stream.read(2))[0]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,required=True,help='Configured x64 build tree, Release outputs')
    parser.add_argument('--x86-tsf',type=Path,required=True);parser.add_argument('--runtime-dir',type=Path,required=True)
    parser.add_argument('--output-dir',type=Path,required=True);parser.add_argument('--version',required=True)
    parser.add_argument('--makensis',type=Path,help='Optional explicit compiler; omitted means staging only')
    parser.add_argument('--sign-mode',choices=['None','LocalTest','Store','Artifact'],default='None')
    args=parser.parse_args()
    try:
        if not re.fullmatch(r'\d+\.\d+\.\d+\.\d+',args.version) or any(int(x)>65535 for x in args.version.split('.')):raise ValueError('Version must have four components, each <= 65535')
        binaries=require_files(args.build_dir/'coralpinyin/Release',OWNED+GENERATORS+BUILD_TOOLS)
        runtime=require_files(args.runtime_dir,('runtime-manifest.json','build-record.json','dictionary_bundle.dictpack.lzma2','config/coralpinyinConfig.ini'))
        manifest=json.loads((runtime/'runtime-manifest.json').read_text(encoding='utf-8'))
        verify(runtime,manifest);verify_runtime(runtime)
        provenance=json.loads((runtime/'build-record.json').read_text(encoding='utf-8'))
        if provenance.get('format_version')!=3:
            raise ValueError('Runtime build record requires version 3; rebuild with the current data tools')
        if not provenance.get('complete') or not provenance.get('installer_bins_match'):
            raise ValueError('Runtime is incomplete or installer reconstruction has not been verified')
        for field,name in [('resource_compiler_sha256','coral_resource_compiler.exe'),('coral_lexicon_builder_sha256','coral_lexicon_builder.exe'),('coral_bundle_decoder_sha256','coral_bundle_decoder.exe')]:
            if provenance.get(field)!=digest(binaries/name):raise ValueError('Runtime/compiler pairing differs: '+name)
        if machine(args.x86_tsf)!=0x14c:raise ValueError('--x86-tsf must be a Win32 DLL')
        for name in OWNED+GENERATORS+BUILD_TOOLS:
            if machine(binaries/name)!=0x8664:raise ValueError('Expected x64 binary: '+name)
            if {'langengine.dll','coral_engine_core.dll'}.intersection(imported_libraries(binaries/name)):
                raise ValueError('Binary still depends on a removed engine DLL: '+name)
        if args.makensis and not args.makensis.is_file():raise ValueError('NSIS compiler missing')
        root=output_path(args.output_dir,[args.build_dir,runtime,args.x86_tsf],directory=True)
        package=root/'package/x64';package.mkdir(parents=True)
        for name in OWNED:shutil.copyfile(binaries/name,package/name)
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
        data=package/'data';general=data/'generallexicon';general.mkdir(parents=True)
        for name in ('bushou.marisa','bushou.dat','stroke.marisa','stroke.dat','dw.coral'):shutil.copyfile(runtime/'data'/name,data/name)
        for name in ('english.dat','english.marisa','correction_pinyin_model.dat','char_lm.dat'):shutil.copyfile(runtime/'data/generallexicon'/name,general/name)
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
        owned=[package/IDENTITY['tsf_x86_dll'] if path=='coraltsf32.dll' else path for path in owned]+[general/name for name in GENERATORS]
        if args.sign_mode!='None':
            for path in owned:subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(sign),'-Mode',args.sign_mode,'-FilePath',str(path)],check=True)
        installer=None
        if args.makensis:
            installer=root/('coralpinyin-window-x64-'+args.version+'.exe')
            command=[str(args.makensis.resolve()),'/NOCD','/INPUTCHARSET','UTF8','-DARCH=x64','-DDBG=0','-DPRODUCT_VERSION='+args.version,
                '-DPACKAGE_DIR='+str(package),'-DOUTPUT_FILE='+str(installer)]
            if args.sign_mode!='None':command+=['-DCORAL_SIGN_SCRIPT='+str(sign),'-DCORAL_SIGN_MODE='+args.sign_mode]
            command+=[str(CODE_ROOT/'packaging/windows/coralpinyin_lite.nsi')]
            with (root/'nsis.log').open('wb') as stream:subprocess.run(command,cwd=root,stdout=stream,stderr=subprocess.STDOUT,check=True)
            if args.sign_mode!='None':subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(sign),'-Mode',args.sign_mode,'-FilePath',str(installer)],check=True)
        record={'format_version':1,'complete':True,'version':args.version,'sign_mode':args.sign_mode,'installer_compiled':bool(installer),
            'runtime_manifest_sha256':digest(runtime/'runtime-manifest.json'),'x86_tsf_sha256':digest(args.x86_tsf),
            'files':inventory(package),'license_files':copied_licenses,
            'third_party_manifest_sha256':digest(CODE_ROOT/'third_party/manifest.json'),
            'installer_sha256':digest(installer) if installer else None}
        (root/'package-manifest.json').write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print('Package staged: '+str(package))
        if installer:print('Installer compiled (not executed): '+str(installer))
    except (ValueError,OSError,subprocess.CalledProcessError) as error:parser.exit(1,str(error)+'\n')

if __name__=='__main__':main()
