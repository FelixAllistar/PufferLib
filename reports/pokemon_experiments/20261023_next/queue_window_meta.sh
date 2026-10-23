#!/usr/bin/env bash
# Window box: start the meta-species rotation once the B1 reproduction control has finished training.
cd /workspace/PufferLib
until [ -f reports/pokemon_experiments/20261023_next/b1_repro_5.0/trained ]; do sleep 60; done
bash reports/pokemon_experiments/20261023_next/run_meta_rotation.sh
