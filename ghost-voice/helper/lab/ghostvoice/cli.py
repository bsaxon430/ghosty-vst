import argparse, json
from .pipeline import build_identity


def main():
    p = argparse.ArgumentParser(description="Build an experimental two-reference identity profile.")
    p.add_argument("--a", action="append", required=True, help="Vocal A WAV; repeat for multiple takes")
    p.add_argument("--b", action="append", required=True, help="Vocal B WAV; repeat for multiple takes")
    p.add_argument("--mix", type=float, default=0.5, help="Weight of Vocal B (0..1)")
    p.add_argument("--novelty", type=float, default=0.0, help="Experimental latent probe (0..0.5); not a voice control")
    p.add_argument("--seed", default="ghost-voice", help="Repeatable novelty probe seed")
    p.add_argument("--out", default="outputs/ghost_voice_c", help="Output prefix")
    args = p.parse_args()
    report = build_identity(args.a, args.b, args.mix, args.out, novelty=args.novelty, seed=args.seed)
    print(json.dumps(report, indent=2))

if __name__ == "__main__":
    main()
