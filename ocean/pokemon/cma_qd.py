"""Full-covariance CMA-ES and a bounded, unstructured behavior archive.

CMA equations: Hansen, arXiv:1604.00772. Archive ranking is CMA-ME-inspired,
not a reproduction of CMA-ME/PPGA. Larger lexicographic ranks are better.
"""
from __future__ import annotations
import math
import numpy as np


class CMAES:
    def __init__(self, dimension=5, population=8, seed=0, sigma=0.6):
        if dimension < 2 or population < 4 or population % 2 or not 0 < sigma < math.inf:
            raise ValueError('dimension>=2, even population>=4, positive finite sigma required')
        self.n, self.population = dimension, population
        self.rng = np.random.default_rng(seed)
        self.mean = np.zeros(dimension)
        self.C = np.eye(dimension)
        self.ps = np.zeros(dimension)
        self.pc = np.zeros(dimension)
        self.sigma, self.generation, self.pending = float(sigma), 0, None
        mu = population // 2
        self.w = np.log(mu + .5) - np.log(np.arange(1, mu + 1))
        self.w /= self.w.sum()
        self.mueff = 1 / (self.w @ self.w)
        n, me = dimension, self.mueff
        self.cc = (4 + me/n)/(n + 4 + 2*me/n)
        self.cs = (me + 2)/(n + me + 5)
        self.c1 = 2/((n + 1.3)**2 + me)
        self.cmu = min(1-self.c1, 2*(me-2+1/me)/((n+2)**2+me))
        self.damps = 1 + 2*max(0, math.sqrt((me-1)/(n+1))-1) + self.cs
        self.chi = math.sqrt(n)*(1-1/(4*n)+1/(21*n*n))

    def ask(self):
        if self.pending is not None:
            raise RuntimeError('tell must follow ask')
        d, B = np.linalg.eigh((self.C+self.C.T)/2)
        d = np.clip(d, 1e-12, 1e12)
        self.C = (B*d) @ B.T
        z = self.rng.standard_normal((self.population, self.n))
        self.pending = self.mean + self.sigma*(z @ (B*np.sqrt(d)).T)
        return self.pending.copy()

    def tell(self, ranks):
        if self.pending is None or len(ranks) != self.population:
            raise ValueError('one rank per pending candidate required')
        if not np.all(np.isfinite(np.asarray(ranks, dtype=float))):
            raise ValueError('non-finite ranks')
        order = sorted(range(self.population),
                       key=lambda i: tuple(np.atleast_1d(ranks[i])), reverse=True)
        y = (self.pending[order[:len(self.w)]]-self.mean)/self.sigma
        yw = self.w @ y
        d, B = np.linalg.eigh(self.C)
        inv = (B/np.sqrt(np.maximum(d, 1e-12))) @ B.T
        self.ps = (1-self.cs)*self.ps + math.sqrt(self.cs*(2-self.cs)*self.mueff)*(inv @ yw)
        self.generation += 1
        h = np.linalg.norm(self.ps)/math.sqrt(1-(1-self.cs)**(2*self.generation))/self.chi < 1.4+2/(self.n+1)
        self.pc = (1-self.cc)*self.pc + h*math.sqrt(self.cc*(2-self.cc)*self.mueff)*yw
        self.C = ((1-self.c1-self.cmu)*self.C
                  + self.c1*(np.outer(self.pc, self.pc)+(1-h)*self.cc*(2-self.cc)*self.C)
                  + self.cmu*np.einsum('i,ij,ik->jk', self.w, y, y))
        self.mean += self.sigma*yw
        self.sigma = float(np.clip(self.sigma*math.exp(self.cs/self.damps*(np.linalg.norm(self.ps)/self.chi-1)), 1e-5, 10))
        self.pending = None

    def state(self):
        if self.pending is not None:
            raise RuntimeError('save only completed generations')
        return dict(n=self.n, population=self.population, mean=self.mean.tolist(),
                    C=self.C.tolist(), ps=self.ps.tolist(), pc=self.pc.tolist(),
                    sigma=self.sigma, generation=self.generation, rng=self.rng.bit_generator.state)

    @classmethod
    def restore(cls, state):
        obj = cls(state['n'], state['population'], sigma=state['sigma'])
        for name in ('mean', 'C', 'ps', 'pc'):
            setattr(obj, name, np.array(state[name], dtype=float))
        obj.generation = state['generation']
        obj.rng.bit_generator.state = state['rng']
        return obj


def personality(x):
    w = np.tanh(np.asarray(x, dtype=float))
    return (w/max(1.0, float(np.abs(w).sum()))).tolist()


def sparse_l2(a, b):
    return sum((a.get(k, 0)-b.get(k, 0))**2 for k in sorted(set(a) | set(b)))


def behavior_distance(a, b):
    """Kernel-mean distance; not JSD of sparsely sampled whole-team histograms.

    This prevents unrelated samples from one random team policy automatically
    looking maximally distinct. Features and block weights remain design choices.
    """
    sq = .25*sparse_l2(a['species'], b['species'])/12
    sq += .30*sparse_l2(a['pairs'], b['pairs'])/30
    sq += .20*sparse_l2(a['teams'], b['teams'])/2
    sq += .10*sparse_l2(a['leads'], b['leads'])/2
    sq += .15*float(np.mean((np.array(a['style'])-b['style'])**2))/4
    return math.sqrt(max(0, sq))


class Archive:
    def __init__(self, radius=.12, capacity=64):
        if not 0 < radius <= 1 or capacity < 2:
            raise ValueError('invalid archive limits')
        self.radius, self.capacity, self.records = radius, capacity, []

    def add_batch(self, records):
        """New region > local quality improvement > rejection.

        Quality-first insertion is deterministic. A split-half noise allowance
        is heuristic, not a confidence guarantee. Once full, only local quality
        improvements are admitted; the runner keeps a separate global champion.
        """
        ranks = {}
        for r in sorted(records, key=lambda x: (-x['quality'], x['id'])):
            if not math.isfinite(r['quality']) or not 0 <= r['quality'] <= 1:
                raise ValueError('quality must be a real match score in [0,1]')
            ds = [behavior_distance(r['behavior'], old['behavior']) for old in self.records]
            eligible = [i for i, (d, old) in enumerate(zip(ds, self.records))
                        if d <= self.radius + .5*(r.get('noise', 0)+old.get('noise', 0))]
            if not eligible:
                if len(self.records) < self.capacity:
                    self.records.append(r)
                    ranks[r['id']] = (2, r['quality'])
                else:
                    ranks[r['id']] = (0, 0.0)
                continue
            i = min(eligible, key=lambda j: ds[j])
            gain = r['quality']-self.records[i]['quality']
            if gain > 0:
                self.records[i] = r
                ranks[r['id']] = (1, gain)
            else:
                ranks[r['id']] = (0, gain)
        return ranks
