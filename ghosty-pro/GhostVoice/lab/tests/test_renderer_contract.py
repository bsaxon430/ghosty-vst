import pytest
from ghostvoice.renderer_contract import RendererCapabilities, SOULX_SINGER_SVC


def test_soulx_svc_is_not_misrepresented_as_two_voice_fusion():
    with pytest.raises(NotImplementedError, match="does not expose"):
        SOULX_SINGER_SVC.validate_identity_fusion()


def test_renderer_with_explicit_two_reference_support_passes():
    backend = RendererCapabilities(
        name="test-backend",
        input_modes=frozenset({"two_identity_references"}),
        supports_multiple_identity_references=True,
    )
    backend.validate_identity_fusion()


def test_external_embedding_requires_fusion_training_support():
    backend = RendererCapabilities(
        name="untrained-embedding-backend",
        input_modes=frozenset({"identity_embedding"}),
        supports_external_identity_embedding=True,
        supports_identity_fusion_training=False,
    )
    with pytest.raises(NotImplementedError):
        backend.validate_identity_fusion()
