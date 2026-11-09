import hashlib,json,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class AudioAssets(unittest.TestCase):
 def test_all_recordings_and_originals_match_manifest(self):
  folder=ROOT/'assets/trio';m=json.loads((folder/'manifest.json').read_text());data=(folder/'trio_pcm.bin').read_bytes()
  self.assertEqual(m['sample_rate'],12000);self.assertEqual(len(m['clips']),44)
  offset=0;names=set()
  for clip in m['clips']:
   self.assertEqual(clip['offset'],offset);self.assertGreater(clip['samples'],0);self.assertLessEqual(clip['samples'],12000*30)
   raw=data[offset:offset+clip['samples']];self.assertEqual(hashlib.sha256(raw).hexdigest(),clip['pcm_sha256'])
   source=folder/'source'/(clip['name']+'.m4a');self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(),clip['source_sha256'])
   self.assertNotIn(clip['name'],names);names.add(clip['name']);offset+=clip['samples']
  self.assertEqual(offset,len(data));self.assertLess(len(data),4_000_000)
  self.assertEqual(len(list((folder/'source').glob('*.m4a'))),44)
 def test_every_clip_contains_non_silent_pcm(self):
  folder=ROOT/'assets/trio';m=json.loads((folder/'manifest.json').read_text());data=(folder/'trio_pcm.bin').read_bytes()
  for c in m['clips']:
   raw=data[c['offset']:c['offset']+c['samples']]
   self.assertGreater(max(raw)-min(raw),4,c['name'])
 def test_normalized_clips_have_headroom_without_clipping(self):
  folder=ROOT/'assets/trio';m=json.loads((folder/'manifest.json').read_text());data=(folder/'trio_pcm.bin').read_bytes()
  self.assertIn('per-clip peak',m['normalization'])
  for c in m['clips']:
   raw=data[c['offset']:c['offset']+c['samples']]
   self.assertEqual(max(abs(v-128) for v in raw),112,c['name'])
   self.assertGreaterEqual(min(raw),16);self.assertLessEqual(max(raw),240)
   self.assertNotIn(0,raw);self.assertNotIn(255,raw)
 def test_pack_has_the_six_variants_and_eight_tones(self):
  clips=json.loads((ROOT/'assets/trio/manifest.json').read_text())['clips'];names={c['name'] for c in clips}
  for family in ['acoustic','electronic']:
   for event in ['lg','hg','dts','dcr','lgs','ulg']:
    for level in ['mild','medium','harsh']:self.assertIn(f'trio_{family}_{event}_{level}',names)
  self.assertEqual(sum(n.startswith('trio_tone_') for n in names),8)
if __name__=='__main__':unittest.main()
