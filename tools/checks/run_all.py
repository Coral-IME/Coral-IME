"""Build/test an explicit build tree; private data checks are opt-in."""
from pathlib import Path
import argparse,json,os,re,shutil,subprocess,sys,uuid

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,required=True)
    parser.add_argument('--configuration',action='append',choices=['Debug','Release'])
    parser.add_argument('--no-build',action='store_true')
    parser.add_argument('--runtime-dir',type=Path);parser.add_argument('--manifest',type=Path)
    parser.add_argument('--dumpbin',type=Path);parser.add_argument('--tsf-exports-baseline',type=Path)
    args=parser.parse_args()
    try:
        build=args.build_dir.resolve()
        if not (build/'CMakeCache.txt').is_file():raise ValueError('Configure the selected build tree first')
        env=os.environ.copy();env.pop('CORAL_LANGENGINE_DATA_DIR',None)
        for config in args.configuration or ['Debug','Release']:
            env['CORAL_TEST_NAMESPACE']=uuid.uuid4().hex
            user=build/'test-users/checks'/env['CORAL_TEST_NAMESPACE'];user.mkdir(parents=True)
            env['CORAL_USER_DATA_DIR']=str(user)
            if not args.no_build:subprocess.run(['cmake','--build',str(build),'--config',config,'--parallel','1','--','/nr:false'],check=True,env=env)
            listing=subprocess.run(['ctest','--test-dir',str(build),'-C',config,'--show-only=json-v1'],check=True,capture_output=True,env=env)
            count=len(json.loads(listing.stdout)['tests'])
            if count==0:raise ValueError('No tests registered; refusing to report success')
            subprocess.run(['ctest','--test-dir',str(build),'-C',config,'--output-on-failure'],check=True,env=env)
            print(config+': '+str(count)+' tests passed',flush=True)
        if bool(args.runtime_dir)!=bool(args.manifest):raise ValueError('--runtime-dir and --manifest must be supplied together')
        if args.manifest:
            subprocess.run([sys.executable,Path(__file__).resolve().parents[1]/'data/generate_manifest.py','--source-dir',args.runtime_dir,'--manifest',args.manifest,'--verify'],check=True)
        if bool(args.dumpbin)!=bool(args.tsf_exports_baseline):raise ValueError('--dumpbin and --tsf-exports-baseline must be supplied together')
        if args.dumpbin:
            result=subprocess.run([args.dumpbin,'/EXPORTS',build/'coralpinyin/Release/coraltsf64.dll'],capture_output=True,check=True)
            names=lambda text:sorted(m.group(1) for m in re.finditer(r'^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)',text,re.M))
            if names(result.stdout.decode('utf-8',errors='replace'))!=names(args.tsf_exports_baseline.read_text(encoding='utf-8',errors='replace')):raise ValueError('Exports differ from the supplied baseline')
        print('ALL REQUESTED CHECKS PASSED')
    except (ValueError,OSError,subprocess.CalledProcessError) as error:parser.exit(1,str(error)+'\n')

if __name__=='__main__':main()
