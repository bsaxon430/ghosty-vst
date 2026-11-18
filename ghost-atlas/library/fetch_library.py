#!/usr/bin/env python3
# GHOST ATLAS - fetches the starter sound library named in manifest.json into the library folder.
#
# Nothing here goes around a login, a paywall or a "name your price" page: every entry is a direct public download that
# its author or host offers, and every entry's licence is recorded beside the files (ATLAS-SOURCE.json) so the importer can
# show where each instrument came from. Archives are checked against the sha256 in the manifest when one is pinned.
# Python 3 standard library only (it runs on the Mac's own python3).
#
#   fetch_library.py [--dest <Ghost Atlas folder>] [--only id,id] [--all] [--list] [--pin]
import argparse, hashlib, json, os, shutil, ssl, subprocess, sys, tarfile, time, urllib.error, urllib.request, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
UA = {"User-Agent": "GhostAtlas-library-fetch/1 (private use; python-urllib)"}


def default_dest():
    return os.path.join(os.path.expanduser("~"), "Music", "GhostBlock", "Ghost Atlas")


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


_WAYS = None      # the ways of checking a server's certificate that this machine offers (see tls_ways)
_WAY = 0          # the one that worked last, so the next download starts with it


def tls_ways():
    """Ways to check a server's certificate, the ordinary one first. Checking is never turned off.

    A python.org build of Python on a Mac has no root certificates until its "Install Certificates" step has been run,
    so every download fails with CERTIFICATE_VERIFY_FAILED although nothing is wrong with the server. The system keeps
    its own roots in /etc/ssl/cert.pem, and certifi brings a set when it is installed: either does the same check.
    """
    global _WAYS
    if _WAYS is None:
        ways = [("this Python's own root certificates", ssl.create_default_context())]
        try:
            import certifi  # noqa
            ways.append(("certifi's root certificates", ssl.create_default_context(cafile=certifi.where())))
        except Exception:  # noqa
            pass
        for ca in ("/etc/ssl/cert.pem", "/private/etc/ssl/cert.pem", "/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt"):
            if os.path.isfile(ca):
                try:
                    ways.append((f"the system's root certificates ({ca})", ssl.create_default_context(cafile=ca)))
                except Exception:  # noqa
                    pass
        _WAYS = ways
    return _WAYS


def certificate_refused(e):
    reason = getattr(e, "reason", e)
    return isinstance(reason, ssl.SSLCertVerificationError) or "CERTIFICATE_VERIFY_FAILED" in str(e)


def download(url, path):
    global _WAY
    tmp = path + ".part"
    ways = tls_ways()
    refused = 0           # how many of the ways refused the server's certificate
    attempt = 0
    while attempt < 4 and refused < len(ways):
        name, context = ways[_WAY % len(ways)]
        try:
            req = urllib.request.Request(url, headers=UA)
            with urllib.request.urlopen(req, timeout=120, context=context) as r, open(tmp, "wb") as f:
                shutil.copyfileobj(r, f, 1 << 20)
            os.replace(tmp, path)
            return
        except Exception as e:  # noqa
            if certificate_refused(e):
                # not something that waiting mends: the same check is made against the next set of root certificates
                refused += 1
                _WAY += 1
                if refused < len(ways):
                    print(f"    {name} could not vouch for {url.split('/')[2]}; checking it against {ways[_WAY % len(ways)][0]}", flush=True)
                continue
            attempt += 1
            print(f"    download failed ({e}); retry {attempt}", flush=True)
            time.sleep(3 + 4 * attempt)
    if refused >= len(ways) and shutil.which("curl"):
        # the last resort checks the certificate too: curl uses the system's own trust store
        print("    none of Python's root certificates could vouch for the server; fetching with the system's curl", flush=True)
        r = subprocess.run(["curl", "--fail", "--location", "--silent", "--show-error", "--retry", "3", "--connect-timeout", "30",
                            "--user-agent", UA["User-Agent"], "--output", tmp, url], capture_output=True, text=True)
        if r.returncode == 0 and os.path.isfile(tmp):
            os.replace(tmp, path)
            return
        print(f"    curl: {(r.stderr or '').strip()[:300]}", flush=True)
    if os.path.isfile(tmp):
        os.remove(tmp)
    if refused >= len(ways):
        raise RuntimeError(f"could not download {url}: the server's certificate could not be checked on this machine "
                           "(if this Python came from python.org, run its \"Install Certificates\" step once)")
    raise RuntimeError(f"could not download {url}")


def extract(archive, dest):
    os.makedirs(dest, exist_ok=True)
    low = archive.lower()
    if low.endswith(".zip"):
        with zipfile.ZipFile(archive) as z:
            for m in z.infolist():
                name = m.filename
                if name.startswith("/") or ".." in name.split("/") or name.startswith("__MACOSX"):
                    continue
                z.extract(m, dest)
        return
    if low.endswith((".tar.xz", ".tar.bz2", ".tar.gz", ".tgz")):
        try:
            with tarfile.open(archive) as t:
                safe = [m for m in t.getmembers() if not (m.name.startswith("/") or ".." in m.name.split("/")) and (m.isfile() or m.isdir())]
                try:
                    t.extractall(dest, members=safe, filter="data")      # (Python 3.12 and later: nothing but plain files and folders)
                except TypeError:
                    t.extractall(dest, members=safe)
            return
        except (tarfile.CompressionError, ImportError):
            pass        # this Python was built without that compression: the system's tar reads it (and refuses paths that climb out)
        r = subprocess.run(["tar", "-xf", archive, "-C", dest], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if r.returncode == 0 and os.listdir(dest):
            return
        raise RuntimeError("could not unpack " + archive)
    if low.endswith(".7z"):
        # the Mac's own tar (libarchive) reads 7z, and so does the copy of libarchive inside cmake; elsewhere 7z / 7zz / bsdtar
        for cmd, cwd in ((["tar", "-xf", archive, "-C", dest], None), (["bsdtar", "-xf", archive, "-C", dest], None),
                         (["7z", "x", "-y", "-o" + dest, archive], None), (["7zz", "x", "-y", "-o" + dest, archive], None),
                         (["cmake", "-E", "tar", "xf", archive], dest)):
            try:
                r = subprocess.run(cmd, cwd=cwd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                if r.returncode == 0 and os.listdir(dest):
                    return
            except FileNotFoundError:
                continue
        raise RuntimeError("no tool on this machine could open the 7z archive " + archive)
    if low.endswith((".sf2", ".md", ".txt", ".csv")):
        shutil.copy2(archive, os.path.join(dest, os.path.basename(archive)))    # used as it is: a SoundFont, its licence and readme
        return
    raise RuntimeError("unknown archive type: " + archive)


def git_sparse(url, ref, paths, dest, commit=""):
    """A shallow, blob-less clone of just the folders asked for. With a pinned commit, exactly that commit is fetched
    (so the library is the one the factory sounds were made against); if the host will not serve it, the branch's head."""
    env = dict(os.environ, GIT_TERMINAL_PROMPT="0")

    def run(*args, quiet=False):
        return subprocess.run(["git"] + list(args), check=True, env=env, capture_output=quiet, text=True)

    def sparse():
        if paths:
            run("-C", dest, "sparse-checkout", "init", "--no-cone")
            # (a name with no dot is a folder, except the files that are written in capitals: LICENSE, COPYING, README)
            run("-C", dest, "sparse-checkout", "set", "--no-cone",
                *["/" + p.strip("/") + ("" if "*" in p or "." in os.path.basename(p) or os.path.basename(p).isupper() else "/") for p in paths])

    done = False
    if commit:
        try:
            shutil.rmtree(dest, ignore_errors=True)
            run("init", "--quiet", dest)
            run("-C", dest, "remote", "add", "origin", url)
            run("-C", dest, "config", "remote.origin.promisor", "true")
            run("-C", dest, "config", "remote.origin.partialclonefilter", "blob:none")
            run("-C", dest, "fetch", "--quiet", "--depth", "1", "--filter=blob:none", "origin", commit)
            sparse()
            run("-C", dest, "checkout", "--quiet", "FETCH_HEAD")
            done = True
        except subprocess.CalledProcessError:
            print(f"    the pinned commit {commit[:10]} could not be fetched on its own: taking the branch's head", flush=True)
    if not done:
        shutil.rmtree(dest, ignore_errors=True)
        args = ["clone", "--quiet", "--depth", "1", "--filter=blob:none", "--no-checkout"]
        if ref:
            args += ["--branch", ref]
        run(*args, url, dest)
        sparse()
        run("-C", dest, "checkout", "--quiet")
    head = run("-C", dest, "rev-parse", "HEAD", quiet=True).stdout.strip()
    shutil.rmtree(os.path.join(dest, ".git"), ignore_errors=True)   # the samples only: no second copy inside .git
    return head

def sfz_samples_present(folder):
    """Do the SFZ files in a folder find their recordings? (a few looked at per file: a map pointing elsewhere fails at once)"""
    import re
    seen = 0
    for root, _, files in os.walk(folder):
        for f in files:
            if not f.lower().endswith(".sfz"):
                continue
            text = open(os.path.join(root, f), encoding="utf-8", errors="replace").read()
            m = re.search(r"default_path=([^\r\n<]*?)(?:\s+\w+=|\s*$)", text, re.M)
            base = (m.group(1).strip() if m else "")
            for s in re.findall(r"sample=([^\r\n]+?\.(?:wav|flac|ogg|aif|aiff))", text, re.I)[:6]:
                seen += 1
                if not os.path.isfile(os.path.join(root, base, s.strip()).replace("\\", "/")):
                    return False
    return seen > 0


def github_release_asset(repo, tag, want):
    """The download address of a release's asset whose name holds `want` (GitHub's public API, no login)."""
    url = f"https://api.github.com/repos/{repo}/releases/tags/{tag}"
    path = os.path.join(os.path.expanduser("~"), ".ghost-atlas-release.json")
    download(url, path)
    try:
        info = json.load(open(path, encoding="utf-8"))
    finally:
        os.remove(path)
    names = [(x["name"], x["browser_download_url"], x.get("size", 0)) for x in info.get("assets", [])]
    fits = [n for n in names if want.lower() in n[0].lower() and n[0].lower().endswith(".zip")]
    if fits:      # (the largest that fits: the recordings, not a zip of maps alone)
        name, link, _ = max(fits, key=lambda n: n[2])
        return name, link
    raise RuntimeError(f"{repo} {tag} has no asset with '{want}' in its name (it has: {', '.join(n[0] for n in names) or 'none'})")


def fetch_into(g, dest, cache, a, e, record):
    """Fetches one way (archive, git or github-release) into dest, replacing what was there."""
    if g["type"] == "archive":
        tmp = dest + ".incoming"
        shutil.rmtree(tmp, ignore_errors=True)
        for i, url in enumerate(g["urls"]):
            name = url.split("/")[-1].split("?")[0]
            arc = os.path.join(cache, name)
            if not os.path.isfile(arc):
                download(url, arc)
            digest = sha256_of(arc)
            want = (g.get("sha256") or [None] * len(g["urls"]))[i]
            if a.pin:
                print(f"    PIN {e['id']} {name} sha256 {digest}")
            if want and want != digest:
                os.remove(arc)
                raise RuntimeError(f"sha256 of {name} is {digest}, the manifest pins {want}: the download was thrown away")
            extract(arc, tmp)
            os.remove(arc)
        shutil.rmtree(dest, ignore_errors=True)
        os.replace(tmp, dest)
    elif g["type"] == "git":
        tmp = dest + ".incoming"
        head = git_sparse(g["url"], g.get("ref", ""), g.get("paths", []), tmp, "" if a.pin else g.get("commit", ""))
        if a.pin:
            print(f"    PIN {e['id']} commit {head}")
        if g.get("commit") and g["commit"] != head:
            print(f"    note: {e['id']} is now at {head} (the manifest was written against {g['commit']})")
        record["fetched_commit"] = head
        shutil.rmtree(dest, ignore_errors=True)
        os.replace(tmp, dest)
    elif g["type"] == "github-release":
        # each asset (a zip) into its own folder; the maps inside must find their recordings, or the whole way fails
        tmp = dest + ".incoming"
        shutil.rmtree(tmp, ignore_errors=True)
        try:
            for item in g["assets"]:
                part = os.path.join(tmp, item.get("folder", item["tag"]))
                try:
                    name, link = github_release_asset(g["repo"], item["tag"], item.get("want", "wav"))
                    arc = os.path.join(cache, name)
                    download(link, arc)
                    if a.pin:
                        print(f"    PIN {e['id']} {name} sha256 {sha256_of(arc)}")
                    extract(arc, part)
                    os.remove(arc)
                    if not sfz_samples_present(part):
                        raise RuntimeError(f"the SFZ map in {name} does not find its recordings")
                    print(f"    {name}", flush=True)
                except Exception as one:  # noqa
                    fb = item.get("fallback")      # this one instrument another way; the others keep their release
                    if not fb:
                        raise
                    print(f"    {item['tag']}: {one}; taking {fb['type']} for it instead", flush=True)
                    shutil.rmtree(part, ignore_errors=True)
                    fetch_into(fb, part, cache, a, e, record)
            for lic in g.get("licence_files", []):
                download(lic["url"], os.path.join(tmp, lic["name"]))
        except Exception:
            shutil.rmtree(tmp, ignore_errors=True)
            raise
        shutil.rmtree(dest, ignore_errors=True)
        os.replace(tmp, dest)
    else:
        raise RuntimeError("unknown get type " + g["type"])


def caveman_cosmonaut(dest):
    """Karoryfer's Caveman Cosmonaut, made playable by sfizz, and its thirteen ARIA presets made into SFZ programs.

    Its main program opens the low-pass filter with an ARIA-only 'var' modulation: 9000 cents, times the cutoff
    control (cc120), times a curve over the keyboard (curve 11: 1 up to note 24, 0.63 at note 84, 0.25 at note 127),
    with the filter tracking the keyboard at 70 cents a note. sfizz does not play 'var', so the filter stayed at
    250 Hz and only the low end was heard. Here the same opening is written in standard SFZ: cutoff_oncc120 at what
    the curve gives in the middle of the keyboard (9000 x 0.778 = 7002 cents) and the curve's slope folded into the
    keyboard tracking (70 - 55.5 cents a note at the program's cutoff), which is the original to within a few cents
    from note 24 to note 84. The velocity 'var' (cutoff times velocity times cc124) is off in every program (cc124
    is 0), so it goes with nothing in its place. The recordings and the mappings are not touched.
    Each ARIA preset (.ariax: control number and value 0..1) becomes a copy of that program with its controls set."""
    import glob, re
    progs = glob.glob(os.path.join(dest, "**", "Programs", "main.sfz"), recursive=True)
    if not progs:
        raise RuntimeError("Caveman Cosmonaut: Programs/main.sfz is not where it was")
    main = progs[0]
    text = open(main, encoding="utf-8", errors="replace").read()
    lines = [l for l in text.split("\n") if not re.match(r"\s*var\d+_", l)]
    text = "\n".join(lines)
    defaults = {int(k): int(v) for k, v in re.findall(r"^set_cc(\d+)=(\d+)", text, re.M)}

    def program(cc, title):
        x = cc.get(120, 127) / 127.0
        body = re.sub(r"^set_cc\d+=\d+\s*\n", "", text, flags=re.M)
        body = body.replace("<control>\n", "<control>\n" + "".join(f"set_cc{k}={v}\n" for k, v in sorted(cc.items())), 1)
        body = re.sub(r"^fil_keytrack=70", f"fil_keytrack={70.0 - 55.5 * x:.1f}", body, flags=re.M)
        body = body.replace("fileg_depthcc125=", "fileg_depth_oncc125=")      # (ARIA's old spelling of the pluck's depth; sfizz reads the standard one)
        body = body.replace("cutoff=250\n", "cutoff=250\ncutoff_oncc120=7002 //Ghost Atlas: the ARIA 'var' opening, in standard SFZ (see its fetch_library.py)\n", 1)
        return f"// {title}: Karoryfer's Caveman Cosmonaut, rewritten by Ghost Atlas so that sfizz opens its filter as ARIA does\n" + body

    folder = os.path.dirname(main)
    maps = {}
    for m in ("poly_map", "mono_first_map", "mono_legato_map"):
        with open(os.path.join(folder, "mappings", m + ".sfz"), encoding="utf-8", errors="replace") as fh:
            maps[m] = fh.read()

    def pruned(cc, slug, body):
        """The program's three maps hold every oscillator type, unison voice and mono mode the instrument has (about
        12,000 regions, which take seconds to load); which of them can sound is chosen by controls a program sets and
        nothing changes while it plays (16, 18, 20, 22: the oscillators' types; 25: unison; 108: mono). Each program
        gets maps holding only its own: a <master> block whose locc/hicc on those controls leaves out the program's
        value is dropped with what it includes. Nothing else is touched."""
        fixed = (16, 18, 20, 22, 25, 108)
        for m, text in maps.items():
            parts = re.split(r"(?=<master>)", text)
            keep = [parts[0]]
            for block in parts[1:]:
                head = block.split("#include", 1)[0]
                ok = True
                for n in fixed:
                    v = cc.get(n, 0)
                    lo = re.search(r"\blocc%d=(\d+)" % n, head); hi = re.search(r"\bhicc%d=(\d+)" % n, head)
                    if (lo and v < int(lo.group(1))) or (hi and v > int(hi.group(1))): ok = False; break
                if ok: keep.append(block)
            name = f"ga_{slug}_{m}.sfz"
            with open(os.path.join(folder, "mappings", name), "w", encoding="utf-8") as fh:
                fh.write(f"// {m}.sfz with only the regions this program can sound (written by Ghost Atlas)\n" + "".join(keep))
            body = body.replace(f'#include "mappings/{m}.sfz"', f'#include "mappings/{name}"')
        return body

    def write(cc, title, path, slug):
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(pruned(cc, slug, program(cc, title)))

    write(dict(defaults), "Caveman Cosmonaut", main, "main")
    made = 1
    for f in sorted(glob.glob(os.path.join(dest, "**", "*.ariax"), recursive=True)):
        xml = open(f, encoding="utf-8", errors="replace").read()
        cc = dict(defaults)
        for k, v in re.findall(r'<Param id="(\d+)" value="([0-9.eE+-]+)"', xml):
            cc[int(k)] = max(0, min(127, int(round(float(v) * 127.0))))
        name = os.path.splitext(os.path.basename(f))[0]
        write(cc, name, os.path.join(folder, name + ".sfz"), re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_"))
        made += 1
    print(f"    Caveman Cosmonaut: its filter opens in sfizz now; {made} programs, each with only its own regions", flush=True)


AFTER = {"caveman-cosmonaut": caveman_cosmonaut}


def folder_bytes(path):
    total = 0
    for root, _, files in os.walk(path):
        for f in files:
            try:
                total += os.path.getsize(os.path.join(root, f))
            except OSError:
                pass
    return total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dest", default=default_dest())
    ap.add_argument("--only", default="")
    ap.add_argument("--all", action="store_true", help="also fetch the entries marked default:false")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--pin", action="store_true", help="print the sha256 / commit of what was fetched (to pin in manifest.json)")
    ap.add_argument("--manifest", default=os.path.join(HERE, "manifest.json"))
    a = ap.parse_args()

    manifest = json.load(open(a.manifest, encoding="utf-8"))
    entries = manifest["libraries"]
    only = [s for s in a.only.split(",") if s]
    lib_root = os.path.join(a.dest, "Library")
    cache = os.path.join(a.dest, "Downloads")
    os.makedirs(lib_root, exist_ok=True)
    os.makedirs(cache, exist_ok=True)

    if a.list:
        for e in entries:
            print(f"{e['id']:28s} {e['license']:22s} {e.get('size_mb', '?'):>6} MB  {'default' if e.get('default', True) else 'optional'}  {e['name']}")
        return 0

    ok = fail = skipped = 0
    for e in entries:
        if only and e["id"] not in only:
            continue
        if not only and not a.all and not e.get("default", True):
            continue
        if e["get"]["type"] == "manual":
            continue
        dest = os.path.join(lib_root, e["id"])
        stamp = os.path.join(dest, "ATLAS-SOURCE.json")
        if os.path.isfile(stamp):
            try:
                had = json.load(open(stamp, encoding="utf-8"))
                if had.get("version") == e.get("version", ""):
                    # already here: the files stay, but the record follows the manifest (what to leave out, names, categories)
                    now = dict(e)
                    for k in ("fetched", "bytes", "fetched_commit"):
                        if k in had:
                            now[k] = had[k]
                    if now != had:
                        json.dump(now, open(stamp, "w", encoding="utf-8"), indent=1)
                    skipped += 1
                    continue
            except Exception:  # noqa
                pass
        print(f"== {e['id']}: {e['name']}  ({e['license']})", flush=True)
        t0 = time.time()
        try:
            g = e["get"]
            record = dict(e)
            try:
                fetch_into(g, dest, cache, a, e, record)
            except Exception as first:  # noqa
                fb = g.get("fallback")
                if not fb:
                    raise
                print(f"    {g['type']} did not work ({first}): taking {fb['type']} instead", flush=True)
                record["fetched_from"] = "fallback"
                fetch_into(fb, dest, cache, a, e, record)
            if e.get("after"):
                AFTER[e["after"]](dest)
            record["fetched"] = time.strftime("%Y-%m-%d")
            record["bytes"] = folder_bytes(dest)
            json.dump(record, open(stamp, "w", encoding="utf-8"), indent=1)
            print(f"    done: {record['bytes'] / 1048576:.0f} MB in {time.time() - t0:.0f}s", flush=True)
            ok += 1
        except Exception as ex:  # noqa
            print(f"    FAILED {e['id']}: {ex}", flush=True)
            shutil.rmtree(dest + ".incoming", ignore_errors=True)
            fail += 1
    try:
        os.rmdir(cache)
    except OSError:
        pass
    print(f"library: {ok} fetched, {skipped} already here, {fail} failed  ->  {lib_root}", flush=True)
    return 1 if fail else 0       # anything missing is a failure: the sounds that need it would be silent


if __name__ == "__main__":
    sys.exit(main())
