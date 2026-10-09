"""Optional Torch tests; native-only installs do not require the sidecar."""
from pathlib import Path
import sys

import numpy as np
import pytest

torch = pytest.importorskip("torch")
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from transformer import (CompactTransformer, ModelConfig, game_seeds, loss_statistics,
                         restore_training_state, rotate_spatial, save_training_checkpoint,
                         selective_attention)
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


def test_worker_identity_preserved_and_occupancy_uses_only_live_units():
    x = observations()
    # Two hands at the same position with the same inventory need distinct
    # identities, even though a permutation-equivariant trunk has shared heads.
    x[:, 2616:2632] = x[:, 2600:2616]
    x[:, 2632:2648] = x[:, 2600:2616]
    model = CompactTransformer(configuration())
    tokens, _, _, _ = model.tokenize(x)
    assert not torch.equal(tokens[:, 201], tokens[:, 202])
    old = CompactTransformer(ModelConfig(width=32, layers=1, heads=2, ffn=64, rope_dim=16, adapter_version=1))
    old_tokens, _, _, _ = old.tokenize(x)
    assert torch.equal(old_tokens[:, 201], old_tokens[:, 202])
    captured = {}
    hook = model.cell.register_forward_pre_hook(lambda _, inputs: captured.update(cell=inputs[0]))
    model.tokenize(x)
    hook.remove()
    cell = captured["cell"]
    assert cell[:, 44, -2].tolist() == [1., 1.]
    assert cell[:, 44, -1].tolist() == pytest.approx([.1, .1])
    assert cell[:, 100, -2:].sum() == 0  # Absent zero-position units add no occupancy.


def test_map_seed_cohorts_match_native_reset_and_remain_separate():
    assert game_seeds(0, 3, "native-index") == [1013904223, 1015568748, 1017233273]
    assert game_seeds(73, 3, "explicit") == [73, 74, 75]
    assert not set(game_seeds(0, 16, "native-index")).intersection(game_seeds(2026100700, 64, "explicit"))


def test_checkpoint_continues_adam_and_random_streams(tmp_path):
    model = CompactTransformer(configuration())
    optimizer = torch.optim.Adam(model.parameters(), lr=1e-4, eps=1e-5)
    rng = np.random.default_rng(73)
    for _ in range(2):
        rng.permutation(5)

    def update(net, opt):
        opt.zero_grad(set_to_none=True)
        sum(p.square().mean() for p in net.parameters()).backward()
        opt.step()

    update(model, optimizer)
    path = tmp_path / "epoch_2.pt"
    save_training_checkpoint(path, model, optimizer, 2, rng, (0., -.5), None, "test-data")
    expected_order, expected_random = rng.permutation(5), torch.rand(4)
    update(model, optimizer)
    saved = torch.load(path, weights_only=True)
    restored = CompactTransformer(configuration())
    restored.load_state_dict(saved["model"])
    restored_opt = torch.optim.Adam(restored.parameters(), lr=.01)
    restored_rng = np.random.default_rng(999)
    assert restore_training_state(saved, restored_opt, restored_rng, 5) == "full optimizer/RNG resume"
    np.testing.assert_array_equal(restored_rng.permutation(5), expected_order)
    assert torch.equal(torch.rand(4), expected_random)
    update(restored, restored_opt)
    for actual, expected in zip(restored.parameters(), model.parameters()):
        assert torch.equal(actual, expected)


def test_legacy_resume_labels_optimizer_reset_and_advances_game_order():
    model = torch.nn.Linear(2, 2)
    optimizer = torch.optim.Adam(model.parameters())
    rng, expected = np.random.default_rng(73), np.random.default_rng(73)
    for _ in range(7):
        expected.permutation(442)
    kind = restore_training_state({"epoch": 7}, optimizer, rng, 442)
    assert "Adam moments reset" in kind
    np.testing.assert_array_equal(rng.permutation(442), expected.permutation(442))
    assert not optimizer.state
