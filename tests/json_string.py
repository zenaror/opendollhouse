"""Exercise the C++ serializers through a real JSON parser, including UTF-8."""
import json
import subprocess
import sys

result = subprocess.run([sys.argv[1]], check=True, stdout=subprocess.PIPE)
data = json.loads(result.stdout.decode('utf-8'))
assert data['bytes'] == ''.join(chr(i) for i in range(256)), 'opaque bytes changed'
assert data['utf8'] == [
    '', ''.join(chr(i) for i in range(128)), 'Kit de Iniciação Mágica',
    'Ελληνικά / Привет', 'שלום / ไทย', '日本語 / 中文 / 한국어',
    '\U0001f48e\U0010ffff', 'quote" backslash\\ newline\nend',
], 'decoded Unicode text did not survive JSON serialization'
assert data['invalid_utf8'] == [bytes.fromhex(value).decode('latin1') for value in (
    'e9', 'c3', '80', 'c0af', 'e080af', 'eda080', 'f08080af',
    'f4908080', 'f888808080', 'c328', 'e282', 'f09f92',
)], 'invalid UTF-8 must preserve opaque checkpoint bytes in valid JSON'
print('JSON byte and Unicode round trips passed')
