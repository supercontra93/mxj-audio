# mxj_dsp

Signal processing without a user interface. See CLAUDE.md and docs/phases/.

- `convert/`: offline sample rate conversion (phase 2).
- `effects/`: effect framework (parameters, processors, effect types, catalogue, chains) and the built-in
  effects (phase 3, decision record 0007).
- `filters/`: biquad designs (RBJ cookbook) and their magnitude response.
- `analysis/`: loudness (EBU R128), true peak, level meter, spectrum.
- `process/`: offline rendering of effects into new blocks, generators, analysis of a range,
  loudness matching.
