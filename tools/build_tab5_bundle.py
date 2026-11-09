"""Build ESP-IDF 5.5.2 offset-table CA data from the reviewed Mozilla PEM.
Format follows Espressif esp_crt_bundle/gen_crt_bundle.py (Apache-2.0).
The Core2 big-endian count-prefixed bundle is NOT interchangeable.
"""
from pathlib import Path
import hashlib, struct
from build_ca_bundle import records_from_pem
ROOT=Path(__file__).resolve().parents[1]
def encode(records):
    if not records or len(records)>200:raise ValueError('Invalid root count')
    offsets=[];payload=bytearray();base=4*len(records)
    for subject,key in records:
        offsets.append(base+len(payload))
        payload.extend(struct.pack('<HH',len(subject),len(key)))
        payload.extend(subject);payload.extend(key)
    return struct.pack('<'+'I'*len(records),*offsets)+payload
def decode(data):
    if len(data)<4:raise ValueError('Missing offsets')
    first=struct.unpack_from('<I',data)[0]
    if first%4 or first<4 or first>len(data) or first//4>200:raise ValueError('Invalid table')
    count=first//4;out=[];expected=first
    for i in range(count):
        offset=struct.unpack_from('<I',data,4*i)[0]
        if offset!=expected or offset+4>len(data):raise ValueError('Invalid offset')
        name,key=struct.unpack_from('<HH',data,offset);end=offset+4+name+key
        if not name or not key or end>len(data):raise ValueError('Invalid record')
        out.append((data[offset+4:offset+4+name],data[offset+4+name:end]));expected=end
    if expected!=len(data) or out!=sorted(out):raise ValueError('Trailing or unsorted data')
    return out
if __name__=='__main__':
    records=records_from_pem((ROOT/'certs/mozilla-ca.pem').read_bytes());binary=encode(records)
    if decode(binary)!=records:raise ValueError('Round trip failed')
    (ROOT/'certs/tab5_crt_bundle.bin').write_bytes(binary)
    digest=hashlib.sha256(binary).hexdigest()
    (ROOT/'include/tab5_bundle_info.h').write_text('#pragma once\nconstexpr const char *TAB5_CA_BUNDLE_SHA256="'+digest+'";\n',encoding='ascii')
    print(f'Generated {len(records)} SDK CA roots, {len(binary)} bytes; SHA256 {digest}')
