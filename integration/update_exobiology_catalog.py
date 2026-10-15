"""Build an offline reference catalog from a pinned EDDI data revision."""
import json
import re
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path

REVISION = "d3b964ea7c8bb959ad6537f55b308f293a326905"
BASE = f"https://raw.githubusercontent.com/EDCD/EDDI/{REVISION}/"
ROOT = Path(__file__).resolve().parents[1]


def fetch(path):
    with urllib.request.urlopen(BASE + path, timeout=30) as response:
        data = response.read()
        try:
            return data.decode("utf-8-sig")
        except UnicodeDecodeError:
            return data.decode("cp1252")


def names(path):
    return {item.attrib["name"]: item.findtext("value", "")
            for item in ET.fromstring(fetch(path)).findall("data")}


genus_names = names("DataDefinitions/Properties/OrganicGenus.resx")
species_names = names("DataDefinitions/Properties/OrganicSpecies.resx")
genuses = []
for symbol, distance in re.findall(
        r'OrganicGenus \w+ = new\( "([^"]+)"[^;]*minimumDistanceMeters: (\d+) ',
        fetch("DataDefinitions/OrganicGenus.cs")):
    genuses.append({"symbol": symbol, "name": genus_names.get(symbol, genus_names.get(symbol.replace("_", ""), symbol)),
                    "distance_m": int(distance)})
known = {item["symbol"] for item in genuses}
species = []
for symbol, genus, value in re.findall(
        r'OrganicSpecies \w+ = new\( "([^"]+)", OrganicGenus\.(\w+), (\d+)',
        fetch("DataDefinitions/OrganicSpecies.cs")):
    if genus in known:
        species.append({"symbol": symbol, "name": next(item["name"] for item in genuses if item["symbol"] == genus) + " " + species_names.get(symbol, species_names.get(symbol.replace("_", ""), symbol)),
                        "genus": genus, "price_cr": int(value)})
assert len(genuses) >= 20 and len(species) >= 80
assert next(item for item in species if item["name"] == "Stratum Tectonicas")["price_cr"] == 19010800
catalog = {"schema": 1, "source": "https://github.com/EDCD/EDDI",
           "revision": REVISION, "license": "Apache-2.0",
           "modified": "Converted species values and genus sampling distances into a compact JSON catalog.",
           "price": "Base Vista Genomics reward; excludes first logged bonus.",
           "distance": "Minimum separation between genetic samples, in metres; not remaining travel distance.",
           "genuses": genuses, "species": species}
(ROOT / "exobiology-catalog.json").write_text(json.dumps(catalog, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
license_dir = ROOT / "vendor" / "eddi"
license_dir.mkdir(exist_ok=True)
for remote, local in [("LICENSE.md", "LICENSE.md"), ("LicenseFile.txt", "LicenseFile.txt")]:
    (license_dir / local).write_text(fetch(remote), encoding="utf-8")
(license_dir / "NOTICE.txt").write_text(
    f"EDDI reference data, https://github.com/EDCD/EDDI\nRevision: {REVISION}\n"
    "Copyright EDDI contributors. Licensed under Apache License 2.0.\n"
    "Modified: extracted values, English names and sampling distances into exobiology-catalog.json.\n",
    encoding="utf-8")
print(f"Catalog written: {len(genuses)} genuses, {len(species)} species")
