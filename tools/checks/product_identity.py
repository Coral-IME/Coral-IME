"""Generate or verify product identity bindings without running installation."""
from pathlib import Path
import argparse
import json
import re

ROOT = Path(__file__).resolve().parents[2]

def load_identity():
    document = json.loads((ROOT / 'resources/product_identity.json').read_text('utf-8'))
    if set(document) != {'format_version','values'} or document['format_version'] != 1:
        raise ValueError('Unsupported product identity schema')
    values = document['values']
    required = {'settings_exe','update_exe','diagnostic_exe','service_exe','session_host_exe','renderer_exe',
                'tsf_x64_dll','tsf_x86_dll','service_name','service_bridge','app_registry','settings_registry',
                'launcher_registry','install_path_value','installation_mutex','session_pipe_prefix','session_mutex_prefix','session_stop_prefix',
                'renderer_pipe_prefix','renderer_mutex_prefix'}
    if set(values) != required: raise ValueError('Product identity keys differ from the contract')
    for key,value in values.items():
        if not isinstance(value,str) or not value or len(value) > 512 or any(ord(c) < 32 or ord(c) > 126 for c in value) or any(c in value for c in '"\'$;'):
            raise ValueError('Unsafe identity value: ' + key)
        if key.endswith(('_exe','_dll')) and not re.fullmatch(r'[A-Za-z0-9_]+\.(exe|dll)',value):
            raise ValueError('Executable must be a basename: ' + key)
    if len(set(v.casefold() for k,v in values.items() if k.endswith(('_exe','_dll')))) != 8:
        raise ValueError('Duplicate binary names')
    return values

def bindings(values):
    header = ['// 由 tools/checks/product_identity.py 生成；请编辑 resources/product_identity.json。',
              '#pragma once','','namespace coral::product {']
    for key,value in values.items():
        name = 'k' + ''.join(part.title() for part in key.split('_'))
        literal = json.dumps(value)
        header += [f'inline constexpr char {name}[] = {literal};',f'inline constexpr wchar_t {name}Wide[] = L{literal};']
    header += ['} // 命名空间 coral::product 结束','']
    nsis = ['; Generated product identity; edit resources/product_identity.json.']
    psd = ['# Generated product identity; edit resources/product_identity.json.','@{']
    cmake = ['# Generated product identity; edit resources/product_identity.json.']
    for key,value in values.items():
        nsis.append(f'!define CORAL_ID_{key.upper()} "{value}"')
        psd.append(f"    {key} = '{value}'")
        cmake.append(f'set(CORAL_ID_{key.upper()} "{value.replace(chr(92),chr(92)*2)}")')
    psd += ['}','']; nsis += ['']; cmake += ['']
    return {'include/coral/product/product_identity.h':'\n'.join(header),
            'packaging/windows/product_identity.nsh':'\n'.join(nsis),
            'packaging/windows/product_identity.psd1':'\n'.join(psd),
            'cmake/product_identity.cmake':'\n'.join(cmake)}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write',action='store_true')
    args = parser.parse_args()
    stale = []
    for rel,text in bindings(load_identity()).items():
        path = ROOT / rel
        expected = text.replace('\n','\r\n').encode('utf-8')
        if args.write:
            path.parent.mkdir(parents=True,exist_ok=True); path.write_bytes(expected)
        elif not path.is_file() or path.read_text('utf-8') != text: stale.append(rel)
    if stale: raise ValueError('Regenerate identity bindings: ' + ', '.join(stale))
    print('Product identity bindings are current')

if __name__ == '__main__': main()
