"""Capability checks for singing-renderer integrations.

This module deliberately refuses to pretend that a generic speaker vector can
be injected into a renderer that only accepts a reference waveform.
"""
from __future__ import annotations
from dataclasses import dataclass
from typing import FrozenSet


@dataclass(frozen=True)
class RendererCapabilities:
    name: str
    input_modes: FrozenSet[str]
    supports_multiple_identity_references: bool = False
    supports_external_identity_embedding: bool = False
    supports_identity_fusion_training: bool = False

    def validate_identity_fusion(self) -> None:
        """Raise until the backend has a real, documented fusion path."""
        if self.supports_multiple_identity_references:
            return
        if self.supports_external_identity_embedding and self.supports_identity_fusion_training:
            return
        raise NotImplementedError(
            f"{self.name} does not expose a verified two-identity fusion interface. "
            "Do not average unrelated embeddings, concatenate reference audio, or "
            "claim a new singer. Implement and validate a model-specific adapter first."
        )


SOULX_SINGER_SVC = RendererCapabilities(
    name="SoulX-Singer-SVC",
    input_modes=frozenset({"prompt_waveform", "target_waveform", "prompt_f0", "target_f0"}),
    supports_multiple_identity_references=False,
    supports_external_identity_embedding=False,
    supports_identity_fusion_training=False,
)
