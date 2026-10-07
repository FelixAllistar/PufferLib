"""Optional Torch tests; native-only installs do not require the sidecar."""
from pathlib import Path
import sys

import numpy as np
import pytest

torch = pytest.importorskip("torch")
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from transformer import CompactTransformer, ModelConfig, loss_statistics, rotate_spatial, selective_attention
from compact_contract import HEADS, LOGITS, SIZES


def configuration():
    return ModelConfig(width=32, layers=1, heads=2, ffn=64, rope_dim=16)


def observations():
    x = torch.zeros(2, 3256)
    x[:, 2600:2603] = torch.tensor([1., 4 / 9, 4 / 9])
    x[:, 2920:2923] = torch.tensor([1., 4 / 9, 4 / 9])
    return x


def test_layout_shared_heads_and_private_information_barrier():
    model = CompactTransformer(configuration()).eval()
    x = observations()
    with torch.no_grad():
        before = model(x)
        x[:, 3000:] = torch.randn(2, 256) * 1000
        after = model(x)
    assert before.shape == (2, LOGITS)
    assert torch.equal(before, after)
    tokens, coords, groups, valid = model.tokenize(x)
    assert tokens.shape == (2, 263, 32)
    assert valid.sum(1).tolist() == [225, 225]  # 200 cells, 2 live units, 23 nonspatial.
    assert not valid[:, 201:220].any() and not valid[:, 221:240].any()
    assert torch.equal(coords[:, 200], torch.tensor([[4., 4.], [4., 4.]]))
    assert groups[0, 240:].sum() == 0


def test_selective_attention_matches_dense_same_farm_oracle_and_gradients():
    torch.manual_seed(9)
    config = configuration()
    q, k, v = [torch.randn(2, 2, 6, 16, requires_grad=True) for _ in range(3)]
    coords = torch.randn(2, 6, 2)
    groups = torch.tensor([[1, 1, 2, 2, 0, 0]] * 2)
    valid = torch.tensor([[True, True, True, False, True, False]] * 2)
    qr = rotate_spatial(q, coords, 16, 100)
    kr = rotate_spatial(k, coords, 16, 100)
    raw, rotated = q @ k.transpose(-1, -2), qr @ kr.transpose(-1, -2)
    same = (groups[:, :, None] == groups[:, None, :]) & (groups[:, :, None] != 0)
    scores = torch.where(same[:, None], rotated, raw) / 4
    expected = scores.masked_fill(~valid[:, None, None, :], -torch.inf).softmax(-1) @ v
    actual = selective_attention(q, k, v, coords, groups, valid, config)
    torch.testing.assert_close(actual, expected, atol=2e-6, rtol=2e-5)
    gradient = torch.randn_like(actual)
    da = torch.autograd.grad(actual, (q, k, v), gradient, retain_graph=True)
    de = torch.autograd.grad(expected, (q, k, v), gradient)
    for a, e in zip(da, de):
        torch.testing.assert_close(a, e, atol=3e-6, rtol=3e-5)


def test_packed_head_order_loss_forced_exclusion_and_oracle():
    logits = torch.randn(3, LOGITS, requires_grad=True)
    labels = torch.full((3, HEADS), -1, dtype=torch.long)
    mask = torch.zeros(3, LOGITS, dtype=torch.bool)
    offsets = np.r_[0, np.cumsum(SIZES)[:-1]]
    for h, (off, width) in enumerate(zip(offsets, SIZES)):
        mask[:2, off:off + width] = True
        labels[0, h] = (h + 7) % width
        mask[1, off:off + width] = False
        mask[1, off] = True  # Forced; ignored even if its logit is enormous.
    loss, count, correct = loss_statistics(logits, labels, mask)
    oracle = sum(torch.nn.functional.cross_entropy(logits[:1, off:off + width],
                 labels[:1, h], reduction="sum") for h, (off, width) in enumerate(zip(offsets, SIZES)))
    torch.testing.assert_close(loss, oracle)
    assert count == 60
    loss.backward()
    assert not logits.grad[1:].any()
    assert torch.isfinite(logits.grad).all()


def test_invalid_backbone_rejected():
    with pytest.raises(AssertionError):
        CompactTransformer(ModelConfig(width=32, heads=3))
