"""Convert the supplied Trio clips to bounded flash-resident unsigned PCM.
Requires ffmpeg on PATH; originals and their attribution remain in assets/trio/source.
"""
from pathlib import Path
import subprocess,hashlib,json,array,sys,math
ROOT=Path(__file__).resolve().parents[1]
styles=['acoustic','electronic'];codes=['lg','hg','dts','dcr','lgs','ulg'];levels=['mild','medium','harsh']
names=[f'trio_{s}_{c}_{v}' for s in styles for c in codes for v in levels]+['trio_tone_'+n for n in ['glass_bell','hard_saw_wave','harp','meow','open_can','open_wrapper','saw_wave','sine_wave']]
pack=bytearray();entries=[];manifest=[]
for name in names:
 source=ROOT/'assets/trio/source'/f'{name}.m4a'
 decoded=subprocess.check_output(['ffmpeg','-v','error','-i',str(source),'-ac','1','-ar','12000','-f','f32le','-acodec','pcm_f32le','pipe:1'])
 floats=array.array('f');floats.frombytes(decoded)
 if sys.byteorder!='little':floats.byteswap()
 assert floats and all(math.isfinite(v) for v in floats)
 peak=max(abs(v) for v in floats);assert peak>0
 # Offline peak normalization: target +/-112 around unsigned-PCM silence 128.
 # Leaves 15 positive steps of headroom; never boosts the speaker test tone.
 gain=(112.0/128.0)/peak
 pcm=bytes(128+max(-112,min(112,round(v*128.0*gain))) for v in floats)
 assert 0<len(pcm)<=12000*30
 entries.append((len(pack),len(pcm)))
 manifest.append(dict(name=name,source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),offset=len(pack),samples=len(pcm),decoded_peak=peak,gain_db=20*math.log10(gain),pcm_peak=112,pcm_sha256=hashlib.sha256(pcm).hexdigest()))
 pack.extend(pcm)
assert len(pack)<4_000_000
(ROOT/'assets/trio/trio_pcm.bin').write_bytes(pack)
(ROOT/'assets/trio/manifest.json').write_text(json.dumps(dict(sample_rate=12000,format='unsigned 8-bit mono PCM',normalization='per-clip peak to +/-112 (about -1.2 dBFS); originals unchanged',clips=manifest),indent=2)+'\n')
header='#pragma once\n#include <stdint.h>\nnamespace alertaudio {\nstruct Clip {uint32_t offset,samples;};\nconstexpr unsigned SampleRate=12000;\nconstexpr Clip clips[]={\n'+''.join(' {%d,%d},\n'%e for e in entries)+'};\nstatic_assert(sizeof(clips)/sizeof(clips[0])==44,"All Trio clips must be embedded");\n}\n'
(ROOT/'include/audio_catalog.h').write_text(header)
print(f'Converted {len(entries)} clips: {len(pack)} PCM bytes, 12000 Hz mono. Offline peak normalization to +/-112; originals unchanged.')
