# Draft transactions

`Model.bend` composes the shared record store and schema. Begin copies a record
into an editable draft. Edits leave committed records unchanged. Invalid field
values can remain visible for correction; commit validates all fields and the
record revision before applying an atomic update. Cancel drops the draft.
Repeated commit of a committed draft has no further effect.

Only one draft is open in this component. Beginning another while editing keeps
the original draft. Applications decide how navigation and confirmation dialogs
expose that behavior. Multi-record atomic transactions, creates/deletes and
durable deduplication across separate sessions remain application/extensions
work; the current idempotence rule is scoped to this draft's lifetime.

Seven laws and 260 native edit/commit/cancel cases pass, together with conflict
recovery, invalid-value correction and read-only/missing-field rejection. The
numeric fixture is independent of policy observations and does not constitute
an original-site comparison.

```sh
node ocean/webnav/families/build.cjs primitive:transaction --test
```
