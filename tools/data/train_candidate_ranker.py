"""通过 LightGBM 官方 C API 训练 LambdaRank；原生产品只使用导出的数值树。"""
import argparse
import ctypes as C
import importlib.util
import json
from pathlib import Path
import time
import numpy as np
from candidate_ranker_format import FEATURE_NAMES, export_model

class NativeLightGBM:
    def __init__(self):
        spec=importlib.util.find_spec('lightgbm')
        if not spec or not spec.submodule_search_locations:
            raise RuntimeError('Install the pinned LightGBM training wheel first')
        root=Path(next(iter(spec.submodule_search_locations)))
        path=next((directory/name for directory in [root/'bin',root/'lib',root]
            for name in ['lib_lightgbm.dll','lib_lightgbm.so','lib_lightgbm.dylib'] if (directory/name).is_file()),None)
        if path is None:raise RuntimeError('LightGBM native library missing')
        self.path=path;self.lib=C.CDLL(str(path))
        handle=C.c_void_p;integer=C.c_int;pointer=C.POINTER
        signatures={
            'LGBM_DatasetCreateFromMat':[handle,integer,C.c_int32,C.c_int32,integer,C.c_char_p,handle,pointer(handle)],
            'LGBM_DatasetSetField':[handle,C.c_char_p,handle,integer,integer],
            'LGBM_DatasetSetFeatureNames':[handle,pointer(C.c_char_p),integer],
            'LGBM_DatasetFree':[handle],
            'LGBM_BoosterCreate':[handle,C.c_char_p,pointer(handle)],
            'LGBM_BoosterFree':[handle],
            'LGBM_BoosterUpdateOneIter':[handle,pointer(integer)],
            'LGBM_BoosterPredictForMat':[handle,handle,integer,C.c_int32,C.c_int32,integer,integer,integer,integer,C.c_char_p,pointer(C.c_int64),pointer(C.c_double)],
            'LGBM_BoosterDumpModel':[handle,integer,integer,integer,C.c_int64,pointer(C.c_int64),pointer(C.c_char)],
            'LGBM_BoosterSaveModelToString':[handle,integer,integer,integer,C.c_int64,pointer(C.c_int64),pointer(C.c_char)],
            'LGBM_BoosterLoadModelFromString':[C.c_char_p,pointer(integer),pointer(handle)],
        }
        for name,args in signatures.items():
            getattr(self.lib,name).argtypes=args;getattr(self.lib,name).restype=integer
        self.lib.LGBM_GetLastError.restype=C.c_char_p

    def call(self,name,*args):
        if getattr(self.lib,name)(*args):raise RuntimeError(self.lib.LGBM_GetLastError().decode('utf-8',errors='replace'))

    def predict(self,booster,x,iterations=0):
        x=np.ascontiguousarray(x,dtype=np.float64);out=np.empty(len(x),dtype=np.float64);length=C.c_int64()
        self.call('LGBM_BoosterPredictForMat',booster,C.c_void_p(x.ctypes.data),1,len(x),x.shape[1],1,1,0,iterations,
                  b'num_threads=4',C.byref(length),out.ctypes.data_as(C.POINTER(C.c_double)))
        if length.value!=len(x):raise ValueError('Unexpected LightGBM result length')
        return out

    def serialize(self,booster,iterations,as_json=True):
        size=1024*1024
        while True:
            buffer=C.create_string_buffer(size);actual=C.c_int64()
            self.call('LGBM_BoosterDumpModel' if as_json else 'LGBM_BoosterSaveModelToString',
                      booster,0,iterations,1,size,C.byref(actual),buffer)
            if actual.value<=size:return buffer.value.decode('utf-8')
            size=actual.value

    def dump(self,booster,iterations):return json.loads(self.serialize(booster,iterations))

    def predict_text(self,text,x):
        booster=C.c_void_p();iterations=C.c_int()
        try:
            self.call('LGBM_BoosterLoadModelFromString',text.encode(),C.byref(iterations),C.byref(booster))
            return self.predict(booster,x)
        finally:
            if booster:self.call('LGBM_BoosterFree',booster)

def load_dataset(path):
    with np.load(path,allow_pickle=False) as data:
        x=np.ascontiguousarray(data['features'],dtype=np.float64)
        y=np.ascontiguousarray(data['labels'],dtype=np.float32)
        raw_groups=data['groups']
    if x.ndim!=2 or x.shape[1]!=len(FEATURE_NAMES) or y.ndim!=1 or raw_groups.ndim!=1 or not len(x) or len(x)>np.iinfo(np.int32).max:
        raise ValueError('Invalid ranking dataset shape')
    if raw_groups.dtype.kind not in 'iuf' or not np.isfinite(raw_groups).all() or not ((raw_groups>=2)&(raw_groups<=np.iinfo(np.int32).max)).all() or not (raw_groups==np.floor(raw_groups)).all():
        raise ValueError('Invalid ranking group lengths')
    groups=np.ascontiguousarray(raw_groups,dtype=np.int32)
    if len(x)!=len(y) or int(groups.sum())!=len(x):raise ValueError('Invalid ranking dataset shape')
    if not np.isfinite(x).all() or not np.isfinite(y).all() or not np.isin(y,[0,1]).all() or not (groups>=2).all():
        raise ValueError('Invalid ranking dataset values')
    offset=0
    for count in groups:
        if not y[offset:offset+count].any():raise ValueError('Unreachable targets belong in end-to-end evaluation, not ranking supervision')
        offset+=count
    return x,y,groups

def group_top1(prediction,x,y,groups):
    correct=0;offset=0
    for count in groups:
        # 输入覆盖长度的优先级与引擎一致；分数相同时保留基础顺序。
        span=x[offset:offset+count,15];scores=prediction[offset:offset+count]
        choices=np.flatnonzero(span==span.max());winner=int(choices[np.argmax(scores[choices])])
        correct+=y[offset+winner]>0;offset+=count
    return float(correct/len(groups))

def train(train_data,valid_data,parameters,trees=300,patience=40):
    api=NativeLightGBM();x,y,groups=train_data;vx,vy,vgroups=valid_data
    params=dict(objective='lambdarank',metric='None',verbosity=-1,num_threads=4,deterministic=True,
                force_col_wise=True,feature_pre_filter=False,zero_as_missing=False,**parameters)
    encoded=' '.join(f'{k}={str(v).lower() if isinstance(v,bool) else v}' for k,v in params.items()).encode()
    dataset=C.c_void_p();booster=C.c_void_p();start=time.monotonic()
    try:
        api.call('LGBM_DatasetCreateFromMat',C.c_void_p(x.ctypes.data),1,len(x),x.shape[1],1,encoded,None,C.byref(dataset))
        api.call('LGBM_DatasetSetField',dataset,b'label',C.c_void_p(y.ctypes.data),len(y),0)
        api.call('LGBM_DatasetSetField',dataset,b'group',C.c_void_p(groups.ctypes.data),len(groups),2)
        names=(C.c_char_p*len(FEATURE_NAMES))(*(name.encode() for name in FEATURE_NAMES))
        api.call('LGBM_DatasetSetFeatureNames',dataset,names,len(FEATURE_NAMES))
        api.call('LGBM_BoosterCreate',dataset,encoded,C.byref(booster))
        best=-1.;best_iteration=0;best_dump=None;history=[]
        for iteration in range(1,trees+1):
            finished=C.c_int();api.call('LGBM_BoosterUpdateOneIter',booster,C.byref(finished))
            if iteration%10==0 or iteration==trees or finished.value:
                accuracy=group_top1(api.predict(booster,vx),vx,vy,vgroups)
                history.append(dict(iteration=iteration,conditional_top1=accuracy))
                if accuracy>best:
                    best=accuracy;best_iteration=iteration;best_dump=api.dump(booster,iteration)
                if iteration-best_iteration>=patience or finished.value:break
        if best_dump is None:raise RuntimeError('No trained ranker checkpoint')
        prediction=api.predict(booster,vx,best_iteration)
        return best_dump,prediction,dict(parameters=params,best_iteration=best_iteration,conditional_top1=best,
            native_model_text=api.serialize(booster,best_iteration,False),
            history=history,seconds=time.monotonic()-start,native_library=str(api.path),training_groups=len(groups),validation_groups=len(vgroups))
    finally:
        if booster:api.call('LGBM_BoosterFree',booster)
        if dataset:api.call('LGBM_DatasetFree',dataset)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--train',required=True,type=Path);parser.add_argument('--valid',required=True,type=Path)
    parser.add_argument('--out',required=True,type=Path);parser.add_argument('--leaves',type=int,default=31)
    parser.add_argument('--depth',type=int,default=6);parser.add_argument('--min-leaf',type=int,default=400)
    parser.add_argument('--trees',type=int,default=300);parser.add_argument('--seed',type=int,default=20261104)
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    model,prediction,report=train(load_dataset(args.train),load_dataset(args.valid),
        dict(num_leaves=args.leaves,max_depth=args.depth,min_data_in_leaf=args.min_leaf,learning_rate=.05,lambda_l2=10,seed=args.seed),trees=args.trees)
    (args.out/'model.json').write_text(json.dumps(model),encoding='utf-8')
    (args.out/'model.txt').write_text(report.pop('native_model_text'),encoding='utf-8')
    report['export']=export_model(model,args.out/'candidate_ranker.coral')
    (args.out/'training.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    np.save(args.out/'validation_predictions.npy',prediction)
    print(json.dumps(report))

if __name__=='__main__':main()
