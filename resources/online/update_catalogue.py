"""Regenerate the compiled-in catalogue after editing catalogue.json."""
import json
from pathlib import Path
root = Path(__file__).resolve().parent
source = root.joinpath('catalogue.json').read_text()
json.loads(source)
assert ')GWCATALOGUE"' not in source
root.joinpath('catalogue.inc').write_text('R"GWCATALOGUE(' + source + ')GWCATALOGUE"\n')
