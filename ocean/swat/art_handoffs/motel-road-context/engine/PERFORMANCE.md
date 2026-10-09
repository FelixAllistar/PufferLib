# Frame-cost probe

Native GTX 1060 3 GB, concurrent Puffer training and desktop load. Same motel
seed 42, 1,173 objects, nine actors, 180 indoor-shooting frames, 30 shots, no
audio. Baseline uses the prior ground/perimeter and omits the new dressing;
current uses the coordinated revision and new dressing.

| Metric | Baseline | Current |
| --- | ---: | ---: |
| Total median | 22.719 ms | 43.749 ms |
| Total mean | 31.064 ms | 65.366 ms |
| Simulation mean | 2.749 ms | 5.852 ms |
| Weapon draw mean | 0.327 ms | 2.834 ms |
| Managed environment textures | 229.30 MiB | 233.30 MiB |
| Duplicate texture allocation saved | 245.99 MiB | 246.99 MiB |

The overall timing comparison is inconclusive: unchanged simulation and weapon
work also slowed, while background training continued. These runs do not prove
a frame-rate improvement or isolate the new art's frame cost. The deterministic
resource difference is four additional unique textures, five model texture owners
and about 4 MiB of managed texture storage. New road geometry is 40 triangles;
36 far-field grass placements submit 56,412 triangles before camera culling.
Six native full/culled views agree exactly, including shadow passes. A stable-load
human test remains useful before accepting the finished level's performance.
