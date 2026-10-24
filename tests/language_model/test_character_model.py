"""人工小网络的独立概率、坏资源拒绝和解码器接入检查。"""
import hashlib
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import random

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/data'))
from character_model_format import quantize_model, read_header


def random_fixture():
    rng = random.Random(7919)
    d, f, v, p = 16, 32, 6, 32
    vocabulary = [0x110000, 0x110001, ord('在'), ord('再'), ord('它'), ord('甲')]
    rand = lambda n: [rng.uniform(-.3, .3) for _ in range(n)]
    weights = rand(v*d) + rand(p*d) + [1.]*d + [0.]*d
    weights += ([1.]*d + [0.]*d + rand(3*d*d) + rand(3*d) + rand(d*d) + rand(d) +
                [1.]*d + [0.]*d + rand(f*d) + rand(f) + rand(d*f) + rand(d))
    payload = struct.pack('<6I', *vocabulary) + struct.pack(f'<{len(weights)}f', *weights)
    header = bytearray(128)
    struct.pack_into('<8sIIQ8IfI', header, 0, b'CCHLM001', 1, 128, 128+len(payload), 1,d,4,f,v,p,0,1,1e-5,0)
    header[64:96] = hashlib.sha256(payload).digest()
    return bytes(header)+payload


def reference(data, context, text):
    version, v, layout = read_header(data[:128], len(data))
    d, h = struct.unpack_from('<II', data, 28)
    epsilon = struct.unpack_from('<f', data, 56)[0]
    vocabulary = struct.unpack_from(f'<{v}I', data, 128)
    mapping = {chr(c): i for i,c in enumerate(vocabulary) if c <= 0x10ffff}
    offset, tensors = 128+v*4, {}
    for name, rows, columns, matrix in layout:
        if matrix and version == 2:
            scales = struct.unpack_from(f'<{rows}f', data, offset); offset += rows*4
            raw = struct.unpack_from(f'<{rows*columns}b', data, offset); offset += rows*columns
            values = [raw[i]*scales[i//columns] for i in range(len(raw))]
        else:
            values = struct.unpack_from(f'<{rows*columns}f', data, offset); offset += rows*columns*4
        tensors[name] = [values[i*columns:(i+1)*columns] for i in range(rows)] if matrix or name == 'position' else values
    def norm(x, gamma, beta):
        mean = sum(x)/d
        inverse = 1/math.sqrt(sum((a-mean)**2 for a in x)/d + epsilon)
        return [(a-mean)*inverse*g+b for a,g,b in zip(x,tensors[gamma],tensors[beta])]
    def linear(x, matrix, bias):
        return [sum(a*b for a,b in zip(row,x))+c for row,c in zip(tensors[matrix],tensors[bias])]
    keys, values = [], []
    def step(token):
        x = [a+b for a,b in zip(tensors['embedding'][token], tensors['position'][len(keys)])]
        y = linear(norm(x,'layer0.gamma1','layer0.beta1'),'layer0.qkv','layer0.qkv_bias')
        keys.append(y[d:2*d]); values.append(y[2*d:]); attention = []
        width = d//h
        for head in range(h):
            a, b = head*width, (head+1)*width
            logits = [sum(q*k for q,k in zip(y[a:b],key[a:b]))/math.sqrt(width) for key in keys]
            exponentials = [math.exp(value-max(logits)) for value in logits]
            total = sum(exponentials)
            attention += [sum(weight*value[i] for weight,value in zip(exponentials,values))/total for i in range(a,b)]
        x = [a+b for a,b in zip(x,linear(attention,'layer0.project','layer0.project_bias'))]
        y = linear(norm(x,'layer0.gamma2','layer0.beta2'),'layer0.expand','layer0.expand_bias')
        y = [.5*a*(1+math.tanh(.7978845608028654*(a+.044715*a**3))) for a in y]
        x = [a+b for a,b in zip(x,linear(y,'layer0.contract','layer0.contract_bias'))]
        x = norm(x,'final_gamma','final_beta')
        logits = [sum(a*b for a,b in zip(row,x)) for row in tensors['embedding']]
        logsum = max(logits)+math.log(sum(math.exp(a-max(logits)) for a in logits))
        return [a-logsum for a in logits]
    next_scores = step(0)
    for char in context:
        if char not in mapping: keys.clear(); values.clear(); next_scores = step(0)
        else: next_scores = step(mapping[char])
    score = 0.
    for char in text:
        score += next_scores[mapping[char]]
        next_scores = step(mapping[char])
    return score


def fixture():
    # 最终归一化的 gamma 为零、beta 为 [1,0]，输出只由手写嵌入第一维决定。
    vocabulary = [0x110000, 0x110001, ord('在'), ord('再'), ord('它'), ord('甲')]
    d, f, v, p = 2, 4, len(vocabulary), 128
    embedding = [0.0] * (v*d)
    embedding[3*d] = 4.0
    weights = embedding + [0.0]*(p*d) + [0.0]*d + [1.0, 0.0]
    weights += [0.0]*(4*d*d+2*d*f+9*d+f)
    payload = struct.pack('<'+'I'*v, *vocabulary) + struct.pack('<'+'f'*len(weights), *weights)
    header = bytearray(128)
    header[:8] = b'CCHLM001'
    struct.pack_into('<IIQIIIIIIIIfI', header, 8, 1, 128, 128+len(payload), 1, d, 1, f, v, p, 0, 1, 1e-5, 0)
    header[64:96] = hashlib.sha256(payload).digest()
    return bytes(header) + payload


def main():
    probe = sys.argv[1]
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        good, bad = root/'tiny.coral', root/'bad.coral'
        good.write_bytes(fixture())
        bad.write_bytes(b'broken')
        text = '\t在\n\t再\n\t在再\n它\t再\n\t龘\n\t\n'
        result = subprocess.run([probe,str(good),str(bad)],input=text,text=True,encoding='utf-8',capture_output=True)
        assert result.returncode == 0, result.stderr
        values = [float(value) for value in result.stdout.splitlines()]
        normalizer = math.log(5+math.exp(4))
        expected = [-normalizer,4-normalizer,4-2*normalizer,4-normalizer]
        assert len(values) == 6 and all(abs(a-b)<1e-5 for a,b in zip(values,expected))
        assert all(math.isnan(value) for value in values[4:])
        for mutation in ['digest','shape','reserved','vocab','trailing','nan']:
            data = bytearray(good.read_bytes())
            if mutation == 'digest': data[-1] ^= 1
            elif mutation == 'shape': struct.pack_into('<I',data,28,0xffffffff)
            elif mutation == 'reserved': data[100]=1
            elif mutation == 'vocab': struct.pack_into('<I',data,128+3*4,ord('在'))
            elif mutation == 'trailing': data += b'\0'
            elif mutation == 'nan': struct.pack_into('<f',data,len(data)-4,float('nan'))
            if mutation in ['vocab','nan']: data[64:96] = hashlib.sha256(data[128:]).digest()
            bad.write_bytes(data)
            assert subprocess.run([probe,str(bad)],capture_output=True).returncode != 0, mutation
        source, quantized = root/'random.coral', root/'quantized.coral'
        source.write_bytes(random_fixture())
        quantize_model(source, quantized)
        assert quantized.stat().st_size < source.stat().st_size
        candidates = ['在再', '在在再', '再在', '在', '再再甲']
        for model in [source, quantized]:
            data = model.read_bytes()
            request = ''.join(context+'\t'+'\t'.join(candidates)+'\n' for context in ['', '它在'])
            result = subprocess.run([probe,str(model),'--batch'],input=request,text=True,encoding='utf-8',capture_output=True)
            assert result.returncode == 0, result.stderr
            actual = [float(value) for line in result.stdout.splitlines() for value in line.split('\t')]
            expected = [reference(data,context,candidate) for context in ['', '它在'] for candidate in candidates]
            assert all(abs(a-b)<.0001 for a,b in zip(actual,expected)), (actual,expected)
            assert len(actual) == len(expected)
        valid = quantized.read_bytes()
        for mutation in ['zero_scale','negative_scale','nan_scale','int8_min','version','truncated']:
            data = bytearray(valid)
            start = 128+6*4
            if mutation == 'zero_scale': struct.pack_into('<f',data,start,0.)
            if mutation == 'negative_scale': struct.pack_into('<f',data,start,-1.)
            if mutation == 'nan_scale': struct.pack_into('<f',data,start,math.nan)
            if mutation == 'int8_min': data[start+6*4] = 128
            if mutation == 'version': struct.pack_into('<I',data,8,3)
            if mutation == 'truncated': data.pop()
            data[64:96] = hashlib.sha256(data[128:]).digest()
            bad.write_bytes(data)
            assert subprocess.run([probe,str(bad)],capture_output=True).returncode != 0, mutation
        try: quantize_model(source, quantized)
        except FileExistsError: pass
        else: raise AssertionError('Quantization overwrote existing model')
        if len(sys.argv) == 5:
            result = subprocess.run([sys.argv[2],sys.argv[3],sys.argv[4],str(good)],capture_output=True)
            assert result.returncode == 0, result.stderr
    print('Character probabilities, invalid resources, and reload preservation passed')


if __name__ == '__main__':
    main()
