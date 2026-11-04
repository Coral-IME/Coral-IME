"""用独立人工树验证推理、阈值与损坏资源回退，不需要训练依赖。"""
import copy
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/data'))
from candidate_ranker_format import FEATURE_NAMES,export_model,inspect_model
PROBE=sys.argv.pop(1)

def fixture():
    def split(feature,threshold,left,right,missing):
        return dict(split_feature=feature,threshold=threshold,decision_type='<=',missing_type=missing,
                    default_left=True,left_child=dict(leaf_value=left),right_child=dict(leaf_value=right))
    return dict(num_class=1,num_tree_per_iteration=1,objective='lambdarank',average_output=False,feature_names=FEATURE_NAMES,
                tree_info=[dict(tree_structure=split(0,.5,.4,-.6,'NaN')),dict(tree_structure=split(1,0,-.1,.2,'None'))])

class CandidateRankerTests(unittest.TestCase):
    def test_numeric_and_corrupt_resources(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);path=root/'ranker.coral';export_model(fixture(),path);data=path.read_bytes()
            self.assertEqual(inspect_model(path)['trees'],2)
            rows=[]
            for a,b in [(0,0),(.5,0),(.5000000001,0),(0,1),(math.nan,0),(0,math.nan),(math.inf,0)]:
                rows.append(' '.join(map(str,[a,b]+[0]*30)))
            payload='\n'.join(rows)+'\n'
            result=subprocess.run([PROBE,str(path)],input=payload,text=True,capture_output=True)
            self.assertEqual(result.returncode,0,result.stderr)
            output=result.stdout.splitlines()
            for actual,expected in zip(output[:6],[.3,.3,-.7,.6,.3,.3]):self.assertAlmostEqual(float(actual),expected,places=14)
            self.assertEqual(output[6],'error')
            malformed=[data[:40],data+b'x']
            for offset,value in [(8,struct.pack('<I',2)),(28,struct.pack('<I',2)),(40,b'x'),
                                 (80+16+4,struct.pack('<i',0)),(80+16+12,struct.pack('<I',4))]:
                changed=bytearray(data);changed[offset:offset+len(value)]=value
                if offset>=80:changed[40:72]=hashlib.sha256(changed[80:]).digest()
                malformed.append(bytes(changed))
            for index,blob in enumerate(malformed):
                bad=root/f'bad-{index}.coral';bad.write_bytes(blob)
                with self.assertRaises(ValueError):inspect_model(bad)
                rejected=subprocess.run([PROBE,str(bad)],capture_output=True,text=True)
                self.assertNotEqual(rejected.returncode,0,index)
                retained=subprocess.run([PROBE,str(path),str(bad)],input=payload,text=True,capture_output=True)
                self.assertEqual(retained.returncode,0,retained.stderr)
                self.assertEqual(retained.stdout,result.stdout)

    def test_feature_math_and_trace_escaping(self):
        result=subprocess.run([PROBE,'--features'],capture_output=True,text=True,encoding='utf-8')
        self.assertEqual(result.returncode,0,result.stderr)
        a,b=map(json.loads,result.stdout.splitlines());f=a['features']
        self.assertEqual(a['pinyin'],'a\tb');self.assertEqual(a['context'],'前\n文')
        self.assertEqual(len(f),32);self.assertEqual(a['feature_version'],1)
        for index,value in {0:-1,1:-9,2:0,5:-4,6:-1,8:1,9:-1,10:0,12:1,14:4,15:8,16:2,17:.5,18:3,25:2,28:2,29:0,30:3,31:1}.items():
            self.assertAlmostEqual(f[index],value,places=12,msg=str(index))
        self.assertAlmostEqual(f[26],.1,places=12)
        self.assertEqual(b['features'][8],0)

    def test_export_rejects_incompatible_models(self):
        with tempfile.TemporaryDirectory() as folder:
            for kind in ['category','schema','nonfinite','zero_missing']:
                m=copy.deepcopy(fixture())
                if kind=='category':m['tree_info'][0]['tree_structure']['decision_type']='=='
                if kind=='schema':m['feature_names']=list(reversed(FEATURE_NAMES))
                if kind=='nonfinite':m['tree_info'][0]['tree_structure']['threshold']=math.inf
                if kind=='zero_missing':m['tree_info'][0]['tree_structure']['missing_type']='Zero'
                with self.assertRaises(ValueError):export_model(m,Path(folder)/'bad.coral')

    def test_training_dataset_boundaries(self):
        try:import numpy as np
        except ImportError:self.skipTest('Optional training dependency numpy is unavailable')
        from train_candidate_ranker import load_dataset
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'data.npz'
            x=np.zeros((4,32));y=np.array([1,0,0,1]);groups=np.array([2,2])
            np.savez(path,features=x,labels=y,groups=groups)
            self.assertEqual(load_dataset(path)[2].tolist(),[2,2])
            invalid=[(x,y,[2.5,1.5]),(x,y,[[2,2]]),(x,y.reshape(4,1),groups),
                     (x,[0,0,0,1],groups),(x,[1,2,0,1],groups),(x,y,[4,2]),
                     (np.full((4,32),np.nan),y,groups),(x[:0],y[:0],[])]
            for features,labels,lengths in invalid:
                np.savez(path,features=features,labels=labels,groups=lengths)
                with self.assertRaises(ValueError):load_dataset(path)

if __name__=='__main__':unittest.main()
