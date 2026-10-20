"""Generate or verify a manifest for an explicitly selected data directory."""
import sys
sys.dont_write_bytecode=True
from pathlib import Path
import argparse,json
from paths import output_path
from runtime_manifest import inventory,verify

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir',type=Path,required=True);parser.add_argument('--manifest',type=Path,required=True)
    parser.add_argument('--verify',action='store_true');parser.add_argument('--data-version',default='unspecified')
    parser.add_argument('--engine-version',default='unspecified',help='Explicit compatibility identity, not inferred from Git')
    args=parser.parse_args()
    try:
        root=args.source_dir.resolve()
        if not root.is_dir():raise ValueError('Source directory does not exist')
        if args.verify:print('Manifest verified: '+str(verify(root,json.loads(args.manifest.read_text(encoding='utf-8'))))+' files')
        else:
            dest=output_path(args.manifest,[root])
            manifest={'format_version':2,'data_version':args.data_version,'engine_version':args.engine_version,'files':inventory(root)}
            dest.write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print('Manifest written: '+str(len(manifest['files']))+' files')
    except (ValueError,OSError) as error:parser.exit(1,str(error)+'\n')

if __name__=='__main__':main()
