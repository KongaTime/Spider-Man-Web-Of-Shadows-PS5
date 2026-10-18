import json
import pathlib
import re
import sys

issues = []
for path in pathlib.Path(sys.argv[1]).glob('*_recomp.*.cpp'):
    text = path.read_text()
    functions = list(re.finditer(r'^DEFINE_REX_FUNC\(([^)]+)\)', text, re.M))
    for index, function in enumerate(functions):
        end = functions[index + 1].start() if index + 1 < len(functions) else len(text)
        body = text[function.start():end]
        targets = set(re.findall(r'\bgoto\s+(loc_[0-9A-F]+)', body))
        labels = set(re.findall(r'^(loc_[0-9A-F]+):', body, re.M))
        for label in sorted(targets - labels):
            issues.append({'file': path.name, 'function': function.group(1), 'missing_label': label})
print(json.dumps(issues, indent=2))
sys.exit(bool(issues))
