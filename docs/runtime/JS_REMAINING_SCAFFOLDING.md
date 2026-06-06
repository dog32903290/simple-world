# JS Remaining Scaffolding

JavaScript remains allowed for:

- `node:test` regression suites.
- contract and admission generators.
- artifact inspectors.
- proof shell comparisons.
- documentation gates.
- legacy fixture replay during migration.

JavaScript must not remain the only product owner for:

- graph mutation;
- graph validation;
- runtime dirty propagation;
- runtimeGraph build;
- native UI dispatch;
- product save/reload semantics;
- node runtime maturity promotion.

The JS interaction contract is now reference proof and migration scaffolding.
Product graph mutation is moving to C++; JavaScript remains valid for tests,
generators, and proof inspection, but not as the final native graph truth.
