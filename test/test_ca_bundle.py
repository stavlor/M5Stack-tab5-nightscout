"""Offline checks for the complete Mozilla store and ESP32 indexed bundle."""
import hashlib,importlib.util,json
from pathlib import Path
import ssl,unittest,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tools"))
import build_tab5_bundle as tab5
spec=importlib.util.spec_from_file_location('ca_builder',ROOT/'tools/build_ca_bundle.py')
builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)
INFO=json.loads((ROOT/'certs/bundle-info.json').read_text())
class CertificateBundleTests(unittest.TestCase):
    def test_source_snapshot_is_pinned(self):
        pem=(ROOT/'certs/mozilla-ca.pem').read_bytes()
        self.assertNotIn(b'\0',pem)
        self.assertEqual(hashlib.sha256(pem).hexdigest(),INFO['pem_sha256'])
        self.assertEqual(ssl.create_default_context(cafile=str(ROOT/'certs/mozilla-ca.pem')).cert_store_stats()['x509_ca'],INFO['roots'])
    def test_firmware_bundle_matches_every_source_root(self):
        records=builder.records_from_pem((ROOT/'certs/mozilla-ca.pem').read_bytes())
        binary=(ROOT/'certs/x509_crt_bundle.bin').read_bytes()
        self.assertEqual(builder.decode(binary),records)
        self.assertEqual(builder.encode(records),binary)
        self.assertEqual(len(records),INFO['roots'])
        self.assertEqual(hashlib.sha256(binary).hexdigest(),INFO['bundle_sha256'])
    def test_tab5_active_pem_header_matches_source(self):
        header=(ROOT/'include/ca_pem.h').read_bytes()
        embedded=header.split(b'R"NSCA(',1)[1].rsplit(b')NSCA";',1)[0]
        self.assertEqual(embedded,(ROOT/'certs/mozilla-ca.pem').read_bytes())
        self.assertNotIn(b'\0',embedded)
    def test_tab5_sdk_bundle_preserves_all_reviewed_roots(self):
        records=builder.records_from_pem((ROOT/'certs/mozilla-ca.pem').read_bytes())
        binary=(ROOT/'certs/tab5_crt_bundle.bin').read_bytes()
        self.assertEqual(tab5.decode(binary),records)
        self.assertEqual(tab5.encode(records),binary)
        with self.assertRaises(ValueError):tab5.decode(binary[:-1])
        corrupt=bytearray(binary);corrupt[4:8]=b'\0'*4
        with self.assertRaises(ValueError):tab5.decode(corrupt)
        with self.assertRaises(ValueError):tab5.decode((ROOT/'certs/x509_crt_bundle.bin').read_bytes())
    def test_malformed_der_and_truncated_bundle_are_rejected(self):
        with self.assertRaises(ValueError):builder.subject_and_key(b'\x30\x80')
        with self.assertRaises(ValueError):builder.decode((ROOT/'certs/x509_crt_bundle.bin').read_bytes()[:-1])
if __name__=='__main__':unittest.main()
