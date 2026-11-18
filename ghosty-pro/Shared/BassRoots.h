#pragma once
// The root note (MIDI, with cents) of each of the 17 Block Step basses (kit bass-01 .. bass-17), measured offline
// with YIN (/home/claude/shared/bass_roots.py: median pitch over the body of the sound). The sample bass of the ACID page
// plays each note at 2^((note - root) / 12) times the sample's speed.
namespace ghosty::bassroots {
constexpr int kCount = 17;
constexpr float kRoot[kCount] = { 31.933f, 39.158f, 27.176f, 30.153f, 23.993f, 24.001f, 23.857f, 23.992f, 36.003f, 35.901f, 23.973f, 23.997f, 29.120f, 26.214f, 26.277f, 30.799f, 33.205f };
}
