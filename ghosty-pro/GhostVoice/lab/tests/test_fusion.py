import numpy as np
import pytest
from ghostvoice.fusion import fuse_identity, cosine


def test_endpoints_match_sources():
    a = np.array([1., 0., 0., 0.])
    b = np.array([0., 1., 0., 0.])
    assert cosine(fuse_identity(a, b, 0.0), a) > 0.99999
    assert cosine(fuse_identity(a, b, 1.0), b) > 0.99999


def test_novelty_is_repeatable_and_unit_length():
    a = np.array([1., 0., 0., 0., 0.])
    b = np.array([0., 1., 0., 0., 0.])
    c1 = fuse_identity(a, b, .5, .25, "test")
    c2 = fuse_identity(a, b, .5, .25, "test")
    assert np.allclose(c1, c2)
    assert np.isclose(np.linalg.norm(c1), 1.0)
    assert cosine(c1, a) < 0.999
    assert cosine(c1, b) < 0.999


def test_rejects_invalid_controls():
    with pytest.raises(ValueError):
        fuse_identity(np.ones(4), np.ones(4), 1.1)
    with pytest.raises(ValueError):
        fuse_identity(np.ones(4), np.ones(4), .5, .8)
