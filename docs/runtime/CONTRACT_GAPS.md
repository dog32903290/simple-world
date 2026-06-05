# Contract Gaps

This is the bug triage ledger for runtime contracts. It is not a backlog for
new visual nodes and it does not claim renderer completion.

## Bug Classification Rule

Every bug must be classified as one of:

```text
code bug
contract gap
proof gap
backend gap
```

- `code bug`: the contract is clear, proof coverage exists, and implementation
  violates it.
- `contract gap`: the expected behavior, layer, failure policy, port/type,
  backend degradation, or parity level is not yet defined centrally.
- `proof gap`: the contract exists, but fixture/test/artifact evidence is too
  weak, stale, missing, or host-only for the claim being made.
- `backend gap`: the contract and proof host are clear, but the missing piece is
  real backend capability such as Metal execution, GPU readback, heap policy,
  shader compilation, presentation, or backend diagnostics.

## Contract Gap Workflow

If the classification is `contract gap`:

1. update `docs/runtime/CONTRACT_GAPS.md`
2. add or update a fixture
3. add or update a test
4. only then patch implementation

Do not add visual nodes to close a contract gap. A new node may only follow
after the missing contract has a fixture and test that prove why the node is
needed.

Do not patch proof shells first unless the shell is only being referenced by
the fixture/test update. Proof-host behavior must not grow to hide a missing
contract.

## Open Gap Ledger

Use this table when a bug exposes a missing contract. Keep entries short and
link to the fixture/test once added.

| Date | Bug / Symptom | Classification | Contract Line | Fixture | Test | Status |
| --- | --- | --- | --- | --- | --- | --- |
| 2026-06-06 | Layer wording can confuse proof hosts with renderer completion. | contract gap | `docs/runtime/LAYERING.md` | docs-only routing fixture via this file | `tests/runtime_layering_contract.test.js` | closed |

## Patch Gate

Before implementation changes, ask:

```text
Is this bug caused by code that violates an existing contract?
Is the contract missing or ambiguous?
Is the proof too weak for the claim?
Is this actually waiting for backend capability?
```

Only `code bug` goes straight to implementation patch. `contract gap` and
`proof gap` must tighten the fixture/test/proof line first. `backend gap` must
stay visible as a backend limitation, not be masked by host proof behavior.
