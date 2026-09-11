"""Small dependency-free solver for empirical symmetric zero-sum games."""
import math


def solve(matrix, iterations=20000, uniform_mix=.1):
    """Fictitious play; return a symmetric mixture and empirical exploitability.

    Payoffs are in [-1, 1], antisymmetric, and concern only the supplied pool.
    Uniform exploration is applied AFTER solving and included in the gap.
    This is an approximation, not a full-game equilibrium certificate.
    """
    n = len(matrix)
    if not n or type(iterations) is not int or iterations < 1 or not 0 <= uniform_mix <= 1:
        raise ValueError('invalid solver settings')
    if any(len(row) != n for row in matrix): raise ValueError('square matrix required')
    for i in range(n):
        for j in range(n):
            if not math.isfinite(matrix[i][j]) or abs(matrix[i][j]) > 1 or abs(matrix[i][j] + matrix[j][i]) > 1e-9:
                raise ValueError('finite antisymmetric [-1,1] payoffs required')
    rows, cols = [1.0]*n, [1.0]*n
    row_values = [sum(row) for row in matrix]
    col_values = [sum(matrix[i][j] for i in range(n)) for j in range(n)]
    for _ in range(iterations):
        # Prefer the less-used strategy on exact ties; a flat payoff matrix
        # should not collapse the league onto the first name alphabetically.
        row = max(range(n), key=lambda i: (row_values[i], -rows[i]))
        col = min(range(n), key=lambda j: (col_values[j], cols[j]))
        rows[row] += 1
        cols[col] += 1
        for i in range(n): row_values[i] += matrix[i][col]
        for j in range(n): col_values[j] += matrix[row][j]
    total = sum(rows) + sum(cols)
    raw = [(rows[i]+cols[i])/total for i in range(n)]
    weights = [(1-uniform_mix)*p+uniform_mix/n for p in raw]
    exploitability = max(sum(matrix[i][j]*weights[j] for j in range(n)) for i in range(n))
    return {'weights': weights, 'empirical_exploitability': max(0, exploitability),
            'iterations': iterations, 'uniform_mix': uniform_mix}
