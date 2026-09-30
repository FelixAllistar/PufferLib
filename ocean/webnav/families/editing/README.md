# Editing family source audit

Pinned original: MiniWoB-plusplus revision
`33c3b4ddef8c6eb67c57a29663d844b1eda7e614`, pages `find-word.html`,
`highlight-text.html`, `highlight-text-2.html`, `text-editor.html`, and
`terminal.html` with their local `core`, `ui_utils`, and Quill dependencies.
Local IDs 0 through 4 follow that order. All generation, state transitions,
timeouts, and reward decisions live in stock CPU Bend. C transports ABI v2
actions, validates bounded rows, projects public state, and supplies separate
native/browser oracles. The projection omits private goal fields, seeds, and
rewards; instructions still reveal the source-public ordinal, style, or
extension to the controller.

`find-word` has the source's 10-second clock and case-sensitive grading. The
original generates six through fourteen lorem words, may capitalize words
after periods, strips nonletters from its chosen word, and strips all
nonalphanumeric characters from a submitted answer. The bounded generator
chooses six varied words from eight source vocabulary entries, with a varied
ordinal and answer. The Bend model owns focus, append typing, punctuation
normalization, submission, and timeout. The action adapter currently accepts
focus, append insertion, Submit, and Wait. Input caret movement, replacement,
Backspace, and arbitrary original punctuation placement are outside this
preset; the browser oracle imports and grades original generated paragraphs.

`highlight-text` selects a whole paragraph of three through nine words;
`highlight-text-2` selects one of three paragraphs with five through seven
words each. The source scores the browser selection after removing whitespace,
so exact, partial, empty, and cross-paragraph ranges are distinct. Bend stores
the public document, a private target text, selection offsets, button order,
and deadline. The bounded generator uses source vocabulary entries and varied
paragraphs/target ordinals. Public `WF_SELECT_RANGE` offsets are measured in
the rendered text with one inter-paragraph newline; the browser fixture maps
those offsets to actual DOM Range endpoints. Selection geometry and drag
motions are outside this preset.

`text-editor` uses the source's 15-second clock, Quill Snow toolbar, three or
four generated words, one-word or all-text instructions, bold/italic/underline
or one of six color families, and the source's five accepted shades per color.
Bend tracks selection and per-character formatting and rejects extra styling.
The bounded preset keeps the generated document fixed: text insertion,
deletion, and Quill delta structures beyond format spans are not implemented.
The original scorer compares Quill delta ops; in particular, it can ignore
whitespace within a single styled op, whereas the bounded per-character model
requires the exact selected word span. The browser fixture imports an original
document, applies selection through the original Quill instance, and clicks
its real toolbar controls. The color picker is a separate public control:
opening it reveals the thirty source palette choices, and clicking a choice
closes it in Bend and in the original widget fixture. The original all-text
scorers dereference the first delta op's absent `attributes` field on an
unformatted Submit and throw before ending the episode. Bend preserves that
inert Submit; an unformatted single-word Submit ends with -1 instead.

`terminal` uses the source's 20-second clock and original `ls`, `help`, `rm`,
`exit`, wildcard, missing-file, and unknown-command branches. Bend owns the
command buffer, Backspace, sorted active file set, extension comparison,
terminal outcomes, and the latest output kind. The bounded generator selects
three through five sorted filenames from five original base names and six
original extension values; original-browser imports support all thirteen
extension values. The public view initially shows the terminal prompt and
latest output, then reveals filenames when `ls` displays them. The source
also retains a full scrollable output history and startup/login lines; the
bounded view projects only the latest command output. Printable ASCII
insertion and Backspace are represented, including wrong commands before a
correct deletion. Exact source quirks include `ls` accepting whitespace,
`rm` removing a valid named file before scoring, a wildcard rejection, and
the extensionless success rule requiring no dot in the filename.

The native programs independently calculate correct/partial/incorrect and
timeout grades, exercise dirty-row reset and private-goal projection
noninterference, and run a controller using only `WFView`. The browser oracle
resets the pinned original pages with seeded randomness and drives original
DOM, Quill, and terminal key handlers through CDP. It compares visible
content, selection or formatting state, file lists, completion, and reward
over success and bad-action schedules. Its fixture imports original instances
only at reset and does not replace the source scorer. Source code is not a
qualification claim: the root agent runs serialized proof, native, and
browser gates and records their measured results separately.

Qualification passed on 2026-09-29: 16 laws, independent native checks, 100
original-page episodes (260 actions), and 5,000 full-credit generated public
scripted episodes. Evidence and source hashes are in `RESULTS.json`. These
checks do not constitute a learned-policy result or full browser parity.
