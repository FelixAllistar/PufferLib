"""Python side of the compact primitive ABI; checked against the C replay bridge."""
POLICY_VERSION, OBSERVATION_VERSION = 7, 5
OBS, STEPS = 3256, 720
SIZES = (44, 20) * 20 + (22, 100) * 10
HEADS, LOGITS = len(SIZES), sum(SIZES)
PACKED = (LOGITS + 7) // 8
