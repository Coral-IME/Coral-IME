"""Shared output boundaries and checksums for data tools."""
import sys
sys.dont_write_bytecode=True
from pathlib import Path
import hashlib

CODE_ROOT=Path(__file__).resolve().parents[2]

def digest(path,algorithm='sha256'):
    h=hashlib.new(algorithm)
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b''):h.update(chunk)
    return h.hexdigest()

def output_path(path,inputs=(),directory=False):
    dest=Path(path).resolve()
    if dest.is_relative_to(CODE_ROOT) and not dest.is_relative_to(CODE_ROOT/'build'):
        raise ValueError('Output must be outside the source tree or under its build/ directory')
    for value in inputs:
        source=Path(value).resolve()
        if dest==source or (directory and source.is_relative_to(dest)) or (source.is_dir() and dest.is_relative_to(source)):
            raise ValueError('Input and output paths must not overlap')
    if dest.exists() and (not directory or not dest.is_dir() or any(dest.iterdir())):
        raise ValueError('Output already exists; choose a new empty destination: '+str(dest))
    (dest if directory else dest.parent).mkdir(parents=True,exist_ok=True)
    return dest

def require_files(root,names):
    root=Path(root).resolve()
    missing=[name for name in names if not (root/name).is_file()]
    if missing:raise ValueError('Missing input files: '+', '.join(missing))
    return root
