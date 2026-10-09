"""Measure actual teacher-data BC updates and optionally profile CUDA operators."""
import argparse
import configparser
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time
import types

import torch

from transformer import (BCObjective, COMPILE_OPTIONS, CompactTransformer, Dataset,
                         ModelConfig, ROOT, STEPS, configure_attention, loss_statistics)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--microbatch', type=int, default=128)
    parser.add_argument('--games', type=int, default=5)
    parser.add_argument('--compile', action='store_true')
    parser.add_argument('--objective', action='store_true')
    parser.add_argument('--profile', action='store_true')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--sdpa', choices=('auto', 'cudnn', 'efficient'), default='auto')
    parser.add_argument('--fused-adam', action='store_true')
    parser.add_argument('--verify-reference', help='git revision of original transformer.py for CUDA loss/gradient comparison')
    parser.add_argument('--checkpoint', type=Path)
    args = parser.parse_args()
    assert args.microbatch > 0 and args.games > 0
    torch.set_num_threads(4)
    torch.manual_seed(73)
    configure_attention(args.sdpa)
    ini = configparser.ConfigParser()
    ini.read(ROOT / 'config/kaggriculture.ini')
    data = Dataset(ROOT / ini['bc']['data'])
    model = CompactTransformer(ModelConfig()).cuda().train()
    if args.checkpoint:
        model.load_state_dict(torch.load(args.checkpoint, weights_only=True, map_location='cuda')['model'])
    opt = torch.optim.Adam(model.parameters(), lr=1e-4, eps=1e-5, fused=args.fused_adam)
    forward = torch.compile(model, dynamic=False, fullgraph=True, options=COMPILE_OPTIONS) if args.compile and not args.objective else model
    objective = BCObjective(model)
    if args.compile and args.objective:
        objective = torch.compile(objective, dynamic=False, fullgraph=True, options=COMPILE_OPTIONS)
    obs, labels, masks = data.game(0, 'cuda')
    frames = max(1, int((labels >= 0).any(-1).sum()))

    def microstep(first, last):
        if args.objective:
            loss, count, correct = objective(obs[first:last], labels[first:last], masks[first:last])
        else:
            with torch.autocast('cuda', dtype=torch.bfloat16):
                logits = forward(obs[first:last])
            loss, count, correct = loss_statistics(logits, labels[first:last], masks[first:last])
        (loss / frames).backward()
        return torch.stack((loss.detach(), count, correct))

    def update(game=None):
        nonlocal obs, labels, masks, frames
        if game is not None:
            obs, labels, masks = data.game(game % data.train_games, 'cuda')
            frames = max(1, int((labels >= 0).any(-1).sum()))
        opt.zero_grad(set_to_none=True)
        stats = torch.zeros(3, device='cuda')
        for first in range(0, STEPS, args.microbatch):
            stats += microstep(first, min(first + args.microbatch, STEPS))
        torch.nn.utils.clip_grad_norm_(model.parameters(), 5., error_if_nonfinite=True)
        opt.step()
        return stats.cpu().tolist()

    started = time.monotonic()
    for _ in range(2):
        update()
    torch.cuda.synchronize()
    warmup_seconds = time.monotonic()-started
    print(json.dumps(dict(warmup_seconds=warmup_seconds)), flush=True)
    torch.cuda.reset_peak_memory_stats()
    started = time.monotonic()
    for game in range(args.games):
        stats = update(game)
    torch.cuda.synchronize()
    elapsed = time.monotonic()-started
    result = dict(microbatch=args.microbatch, compiled=args.compile, objective=args.objective,
        games=args.games, seconds=elapsed, seconds_per_game=elapsed/args.games,
        frames_per_second=args.games*STEPS/elapsed,
        peak_bytes=torch.cuda.max_memory_allocated(), stats=stats,
        torch=torch.__version__, gpu=torch.cuda.get_device_name(),
        sdpa=args.sdpa, fused_adam=args.fused_adam,
        compile_options=COMPILE_OPTIONS if args.compile else None, warmup_seconds=warmup_seconds,
        dataset_sha256=data.metadata['sha256'], checkpoint=str(args.checkpoint) if args.checkpoint else None,
        source_sha256=hashlib.sha256((ROOT / 'ocean/kaggriculture_direct/transformer.py').read_bytes()).hexdigest())
    print(json.dumps(result), flush=True)
    if args.output:
        with args.output.open('x') as stream:
            json.dump(result, stream, indent=2)
    if args.verify_reference:
        reference_source = subprocess.check_output(['git', 'show',
            f'{args.verify_reference}:ocean/kaggriculture_direct/transformer.py'], cwd=ROOT, text=True)
        reference_module = types.ModuleType('transformer_reference')
        reference_module.__file__ = str(ROOT / 'ocean/kaggriculture_direct/transformer.py')
        sys.modules[reference_module.__name__] = reference_module
        exec(compile(reference_source, reference_module.__file__, 'exec'), reference_module.__dict__)
        reference = reference_module.CompactTransformer().cuda().train()
        reference.load_state_dict(model.state_dict())
        obs, labels, masks = data.game(13, 'cuda')
        frames = max(1, int((labels >= 0).any(-1).sum()))
        model.zero_grad(set_to_none=True)
        candidate_stats = torch.zeros(3, device='cuda')
        for first in range(0, STEPS, args.microbatch):
            candidate_stats += microstep(first, min(first + args.microbatch, STEPS))
        candidate_grads = [p.grad.detach().cpu().clone() for p in model.parameters()]
        model.zero_grad(set_to_none=True)
        reference_stats = torch.zeros(3, device='cuda')
        for first in range(0, STEPS, 128):
            last = first + 128
            with torch.autocast('cuda', dtype=torch.bfloat16):
                logits = reference(obs[first:last])
            loss, count, correct = reference_module.loss_statistics(logits, labels[first:last], masks[first:last])
            (loss / frames).backward()
            reference_stats += torch.stack((loss.detach(), count, correct))
        error, magnitude = 0., 0.
        for actual, parameter in zip(candidate_grads, reference.parameters()):
            expected = parameter.grad.detach().cpu()
            assert torch.isfinite(actual).all() and torch.isfinite(expected).all()
            error += (actual.double() - expected.double()).square().sum().item()
            magnitude += expected.double().square().sum().item()
        actual, expected = candidate_stats.tolist(), reference_stats.tolist()
        validation = dict(reference=args.verify_reference, candidate_stats=actual,
            reference_stats=expected, gradient_relative_l2=(error/max(magnitude, 1e-30))**.5,
            loss_relative_error=abs(actual[0]-expected[0])/max(abs(expected[0]), 1e-30))
        print('TRANSFORMER_PARITY ' + json.dumps(validation), flush=True)
        if args.output:
            with args.output.with_suffix('.parity.json').open('x') as stream:
                json.dump(validation, stream, indent=2)
        assert actual[1] == expected[1]
        assert validation['loss_relative_error'] < .002
        assert validation['gradient_relative_l2'] < .02
    if args.profile:
        with torch.profiler.profile(activities=[torch.profiler.ProfilerActivity.CPU,
                                               torch.profiler.ProfilerActivity.CUDA]) as prof:
            update()
        print(prof.key_averages().table(sort_by='self_cuda_time_total', row_limit=30), flush=True)
        print('ATTENTION_BACKENDS', [(e.key, e.count) for e in prof.key_averages()
                                     if 'attention' in e.key or 'bmm' in e.key], flush=True)


if __name__ == '__main__':
    main()
