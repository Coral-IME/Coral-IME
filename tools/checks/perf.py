"""Measure a selected isolated engine executable; write results outside sources."""
from pathlib import Path
import argparse,json,math,os,statistics,subprocess,sys,time,uuid
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'data'))
from paths import output_path

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable',type=Path,required=True);parser.add_argument('--runtime-dir',type=Path,required=True)
    parser.add_argument('--output-dir',type=Path,required=True);parser.add_argument('--runs',type=int,default=10)
    parser.add_argument('--arg',action='append',help='Child argument; @USER_DIR@ is replaced. Default: @USER_DIR@ for sequence_test')
    parser.add_argument('--previous',type=Path,help='Optional prior result JSON')
    args=parser.parse_args()
    try:
        if args.runs<1:raise ValueError('--runs must be positive')
        if not args.executable.is_file() or not (args.runtime_dir/'config/coralpinyinConfig.ini').is_file():raise ValueError('Executable/runtime missing')
        root=output_path(args.output_dir,[args.executable,args.runtime_dir],directory=True);times=[]
        for index in range(args.runs):
            token=uuid.uuid4().hex;user=root/'users'/token;user.mkdir(parents=True)
            env=os.environ.copy();env.update(CORAL_LANGENGINE_DATA_DIR=str(args.runtime_dir.resolve()),CORAL_USER_DATA_DIR=str(user),CORAL_TEST_NAMESPACE=token)
            command=[str(args.executable.resolve()),*[str(user) if v=='@USER_DIR@' else v for v in args.arg or ['@USER_DIR@']]]
            start=time.perf_counter()
            with (root/(str(index)+'.log')).open('wb') as stream:subprocess.run(command,check=True,env=env,stdout=stream,stderr=subprocess.STDOUT)
            times.append((time.perf_counter()-start)*1000)
        times.sort();result={'runs':args.runs,'measurement':'child process end-to-end, including engine init and teardown strategy',
            'min_ms':round(times[0],2),'median_ms':round(statistics.median(times),2),'p95_ms':round(times[math.ceil(.95*len(times))-1],2)}
        if args.previous:
            previous=json.loads(args.previous.read_text(encoding='utf-8'))
            result['median_change_percent']=round((result['median_ms']/previous['median_ms']-1)*100,2)
        (root/'perf.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result))
    except (ValueError,OSError,subprocess.CalledProcessError) as error:parser.exit(1,str(error)+'\n')

if __name__=='__main__':main()
