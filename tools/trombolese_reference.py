"""Makes Source/Core/TromboleseTables.inc's numbers and Tests/fixtures/trombolese/*.f64
(decision 0093), with KallumS/Trombolese's own Python:
    python tools/trombolese_reference.py OUT_FOLDER PATH_TO_TROMBOLESE
tables.json then becomes TromboleseTables.inc (one row per sample rate)."""
import sys, json, numpy as np
sys.path.insert(0, sys.argv[2] + "/src")   # a checkout of KallumS/Trombolese
from trombolese import Trombolese, pitch_neutral
from trombolese.synth import Controls, Voice
from trombolese.waveguide import tune_to_waveguide, section_bore, _fit_mouth_reflection
out = sys.argv[1]
info = {}
for sr in (44100, 48000, 88200, 96000):
    inst = tune_to_waveguide(pitch_neutral(Trombolese(), regime=3, n_alpha=9), regime=3, n_alpha=9, sample_rate=sr)
    pc = inst.pitch_compensation
    bore = section_bore(inst, 0.0, 0.0, float(sr), dispersion_reference_hz=150.0)
    g, p = _fit_mouth_reflection(bore)
    info[str(sr)] = {"alphas": list(map(float, pc.alphas)), "lengths": list(map(float, pc.lengths)),
                     "target_f1": float(pc.target_f1), "gain": float(g), "pole": float(p),
                     "vent": inst.vent is not None, "sections": int(bore.n_sections)}
    print(sr, info[str(sr)], flush=True)
    if sr == 48000:
        held = pc.target_f1
        cases = {
            "steady": lambda pos: Controls(pressure=4200.0 * min(pos / 0.1, 1.0), lip_frequency=held * 0.97),
            "morphed": lambda pos: Controls(pressure=4000.0 * min(pos / 0.1, 1.0), lip_frequency=held * 0.97, alpha=0.6, beta=0.3, slide=0.2),
            "sweep": lambda pos: Controls(pressure=4200.0 * min(pos / 0.1, 1.0), lip_frequency=held * 0.97, alpha=pos),
        }
        for name, auto in cases.items():
            v = Voice(inst, sample_rate=48000.0)
            v.reset()
            y = v.render(int(0.3 * 48000), automation=auto)
            y.astype(np.float64).tofile(f"{out}/{name}.f64")
            print(name, float(np.sqrt(np.mean(y**2))), flush=True)
json.dump(info, open(f"{out}/tables.json", "w"), indent=1)
