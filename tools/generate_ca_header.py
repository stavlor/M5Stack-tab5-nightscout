"""Generate the NUL-terminated C++ CA store without mutating the canonical PEM."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
pem=(ROOT/'certs/mozilla-ca.pem').read_text(encoding='ascii')
if '\0' in pem or ')NSCA"' in pem:raise ValueError('Invalid PEM source')
(ROOT/'include/ca_pem.h').write_text('#pragma once\n// Generated from certs/mozilla-ca.pem; see certs/CERTIFI-LICENSE.txt.\nstatic const char mozillaPem[] = R"NSCA('+pem+')NSCA";\n',encoding='ascii',newline='\n')
