"""Adapt the public hand-authored fixtures to the formal source CSV schemas."""
import sys
sys.dont_write_bytecode=True
from pathlib import Path
from collections import defaultdict
import argparse,csv,shutil
from paths import CODE_ROOT,output_path

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output-dir',type=Path,required=True)
    args=parser.parse_args()
    try:
        fixtures=CODE_ROOT/'samples/demo/fixtures';root=output_path(args.output_dir,[fixtures],directory=True)
        with (fixtures/'lexicon.csv').open(encoding='utf-8') as stream:rows=list(csv.DictReader(stream))
        frequencies=defaultdict(int);readings={r['word']:r['fullPinyin'] for r in rows}
        with (root/'coral_lexicon.csv').open('x',encoding='utf-8',newline='') as stream:
            writer=csv.writer(stream);writer.writerow(['pinyin','frequency','word','fullPinyin','identifier','isPolyphonic','in_m2'])
            for row in rows:
                writer.writerow([row['pinyin'],row['frequency'],row['word'],row['fullPinyin'],0,0,row['in_m2']])
                frequencies[row['fullPinyin']]+=int(row['frequency'])
        with (fixtures/'bigram.csv').open(encoding='utf-8') as stream:bigram=list(csv.DictReader(stream))
        with (root/'coral_languagemodel.csv').open('x',encoding='utf-8',newline='') as stream:
            writer=csv.writer(stream);writer.writerow(['prob','prev_word','next_word','prev_fullPinyin','next_fullPinyin'])
            for row in bigram:writer.writerow([row['log_probability'],row['prev_word'],row['next_word'],readings[row['prev_word']],readings[row['next_word']]])
        with (root/'pinyin_frequency.csv').open('x',encoding='utf-8',newline='') as stream:
            writer=csv.writer(stream);writer.writerow(['pinyinstr','frequency']);writer.writerows(sorted(frequencies.items()))
        for name in ('umode_radical.csv','umode_stroke.csv','dw.coral'):shutil.copyfile(fixtures/name,root/name)
        shutil.copyfile(fixtures/'eng_lexicon.csv',root/'eng_lexicon.csv');shutil.copytree(fixtures/'opencc',root/'opencc')
        print('Self-authored demo sources: '+str(root))
    except (ValueError,OSError) as error:parser.exit(1,str(error)+'\n')

if __name__=='__main__':main()
