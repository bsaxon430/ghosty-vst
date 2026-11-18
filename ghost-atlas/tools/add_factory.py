#!/usr/bin/env python3
# GHOST ATLAS - adds a batch of polished sounds to the factory bank without playing the rest again.
#
#     python3 tools/make_presets.py                                    (writes presets/factory-drafts-new.json: the latest voyage)
#     GhostAtlasTests polish presets/factory-drafts-new.json new.json new.tsv
#     python3 tools/add_factory.py new.json new.tsv                    (into assets/data/factory.json and presets/factory-report.tsv)
#
# The bank is kept exactly as `polish` wrote it (its own layout and line endings): the new sounds are set in as text, after
# the ones already there, and a sound whose name is already in the bank takes that sound's place. Nothing else moves.
import json, os, sys

def pieces(text):
    """The sounds of a bank as `polish` writes it: the text of each, without the brackets round them all."""
    body = text.strip()
    if not (body.startswith("[") and body.endswith("]")): sys.exit("not a bank of sounds")
    body = body[1:-1].strip("\r\n")
    if not body.strip(): return []
    parts = body.split("\r\n  },\r\n  {")                 # (a sound closes and the next opens two spaces in: nothing inside one does)
    out = []
    for i, part in enumerate(parts):
        out.append(("" if i == 0 else "  {") + part + ("" if i == len(parts) - 1 else "\r\n  }"))
    return out

def name_of(piece): return json.loads(piece)["name"]

def main():
    if len(sys.argv) < 3: sys.exit("add_factory.py <polished.json> <report.tsv>")
    here = os.path.dirname(os.path.abspath(__file__))
    bank_path = os.path.join(here, "..", "assets", "data", "factory.json")
    report_path = os.path.join(here, "..", "presets", "factory-report.tsv")
    bank = pieces(open(bank_path, encoding="utf-8", newline="").read())
    new = pieces(open(sys.argv[1], encoding="utf-8", newline="").read())
    new_names = [name_of(p) for p in new]
    kept = [p for p in bank if name_of(p) not in new_names]
    text = "[\r\n" + ",\r\n".join(kept + new) + "\r\n]"
    sounds = json.loads(text)                                # (it must still read as a bank, every sound once)
    names = [s["name"] for s in sounds]
    twice = sorted({n for n in names if names.count(n) > 1})
    if twice: sys.exit("a name is in the bank twice: " + ", ".join(twice))
    open(bank_path, "w", encoding="utf-8", newline="").write(text)

    rows = open(report_path, encoding="utf-8", newline="").read().split("\r\n")
    fresh = [r for r in open(sys.argv[2], encoding="utf-8", newline="").read().split("\r\n")[1:] if r.strip()]
    failed = [r.split("\t")[0] for r in fresh if r.split("\t")[3].startswith("FAIL")]
    if failed: sys.exit("these did not pass and are not in the polished file: " + ", ".join(failed))
    rows = [r for r in rows if r.strip() and r.split("\t")[0] not in new_names] + fresh
    open(report_path, "w", encoding="utf-8", newline="").write("\r\n".join(rows) + "\r\n")
    print(len(new), "added;", len(bank) - len(kept), "of them took the place of a sound already there;", len(sounds), "in the bank")

if __name__ == "__main__":
    main()
