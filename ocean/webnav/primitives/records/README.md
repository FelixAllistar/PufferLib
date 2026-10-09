# Persistent records and queries

`Model.bend` stores records with persistent nonzero keys and revisions. Updates
check the expected revision and field count before replacing a record. Missing
keys, stale revisions and revision overflow preserve the store. Inserts reject
duplicate identities. Collections and allocation of fresh keys belong to the
application; input stores must have unique keys and a consistent schema.

`Value.bend` supports null, boolean, signed 32-bit integer, text and collection
references. Integer bits use two's complement; money and date units are explicit
schema conventions. Comparisons use type order, then value order. Null sorts
first. Text matching is case sensitive. These are declared simulator semantics,
not SQL NULL rules or a claim of Magento search parity.

`Query.bend` composes predicates, sorting and pagination. Sorting breaks ties
with the persistent key, so changes to input/DOM order cannot change page
membership. Query functions return record views without mutating stored data.
`Schema.bend` supplies type/range/length checks and editable-field metadata for
transactions. Reference validation checks collection and nonzero identity;
foreign-key existence remains an application invariant.

The eight record/query laws and 612 independent numeric query cases pass under
stock CPU Bend 2.0.6. Native checks also cover stale writes, missing identities
and revision overflow. Text matching is covered by a concrete law; broader
typed-value native and browser comparisons remain to be added. The numeric
fixture wire is for conformance, not the application observation format.

```sh
node ocean/webnav/families/build.cjs primitive:records --test
```
