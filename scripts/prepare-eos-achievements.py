"""Build a private EOS bulk import from metadata extracted from the user's XEX.

Requires Pillow for the portal's icon sizing. No game metadata/images are bundled
with this script. The template must be downloaded from the Epic Developer Portal.
"""
import argparse
import csv
import io
import json
from pathlib import Path
import zipfile


def csv_bytes(header, rows):
    out = io.StringIO(newline="")
    writer = csv.writer(out, lineterminator="\r\n")
    writer.writerow(header)
    writer.writerows(rows)
    return out.getvalue().encode("utf-8")


def build(catalog, template, output):
    from PIL import Image

    if output.exists():
        raise ValueError("Use a new output ZIP to preserve existing files")
    data = json.loads(catalog.read_text(encoding="utf-8-sig"))
    if data.get("titleId") != "584109B5" or not data.get("achievements"):
        raise ValueError("Expected the local Snoopy Flying Ace achievement catalog")
    expected = {
        "achievementDefinitions.csv": ["name", "hidden", "statThresholds"],
        "achievementLocalizations.csv": ["name", "locale", "lockedTitle", "lockedDescription",
                                         "unlockedTitle", "unlockedDescription", "flavorText", "lockedIcon", "unlockedIcon"],
        "stats.csv": ["name", "aggregationType"],
    }
    with zipfile.ZipFile(template) as source:
        for name, header in expected.items():
            reader = csv.reader(io.StringIO(source.read(name).decode("utf-8-sig"), newline=""))
            if next(reader) != header:
                raise ValueError("The portal template schema changed: " + name)
    definitions, localizations, icons, ids = [], [], {}, set()
    for a in data["achievements"]:
        key = f"SFA_{int(a['id']):08X}"
        if a.get("eosId") != key or key in ids:
            raise ValueError("Unexpected or duplicate native achievement ID")
        ids.add(key)
        if not a.get("label") or not a.get("description") or not a.get("icon"):
            raise ValueError("Incomplete native achievement metadata: " + key)
        image_path = (catalog.parent / a["icon"]).resolve()
        if catalog.parent.resolve() not in image_path.parents:
            raise ValueError("Achievement icon must stay inside the exported catalog")
        with Image.open(image_path) as image:
            if image.width != image.height:
                raise ValueError("Native achievement icon must be square")
            image = image.convert("RGBA")
            # Preserve the pixels of the native icon; do not synthesize artwork.
            if image.width < 256:
                image = image.resize((256, 256), Image.Resampling.NEAREST)
            encoded = io.BytesIO()
            image.save(encoded, format="PNG")
        icon_name = key + ".png"
        icons[icon_name] = encoded.getvalue()
        # XDBF bit 0x8 means the unachieved information is visible.
        definitions.append([key, "FALSE" if a["flags"] & 8 else "TRUE", ""])
        localizations.append([key, "", a["label"], a["lockedDescription"] or a["description"],
                              a["label"], a["description"], f"{a['gamerscore']} G", icon_name, icon_name])
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as target:
        target.writestr("achievementDefinitions.csv", csv_bytes(expected["achievementDefinitions.csv"], definitions))
        target.writestr("achievementLocalizations.csv", csv_bytes(expected["achievementLocalizations.csv"], localizations))
        target.writestr("stats.csv", csv_bytes(expected["stats.csv"], []))
        for name, image in icons.items():
            target.writestr(name, image)
    print(f"Prepared {len(ids)} native achievements and {len(icons)} icons for private portal import")
    print("Keep the ZIP, catalog and exported icons out of Git and public release packages.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", required=True, type=Path)
    parser.add_argument("--template", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    build(args.catalog, args.template, args.output)
