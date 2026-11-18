import numpy as np
from ghostvoice.embedding import normalize, slerp, cosine

def test_slerp_endpoints():
    a = normalize(np.array([1.0, 0.0, 0.0]))
    b = normalize(np.array([0.0, 1.0, 0.0]))
    assert np.allclose(slerp(a, b, 0.0), a)
    assert np.allclose(slerp(a, b, 1.0), b)

def test_slerp_midpoint_unit_length():
    a = np.array([1.0, 0.0, 0.0])
    b = np.array([0.0, 1.0, 0.0])
    c = slerp(a, b, 0.5)
    assert np.isclose(np.linalg.norm(c), 1.0)
    assert np.isclose(cosine(a, c), cosine(b, c))
