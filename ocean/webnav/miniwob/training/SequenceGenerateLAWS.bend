import Base
import ./SequenceGenerate.bend as G

# These claims pin the source-facing parts of the training generator.  Numeric
# row shape, packed termination and seed variation are checked by the native
# evaluator because they are properties of the caller-owned Array ABI.

law button_task_uses_tag_zero:
  {G.task_tag(0) == 0 : U32}

law link_task_uses_tag_zero:
  {G.task_tag(1) == 0 : U32}

law focus_task_uses_tag_two:
  {G.task_tag(2) == 2 : U32}

law sequence_task_uses_tag_seven:
  {G.task_tag(3) == 7 : U32}

law choose_task_uses_tag_eight:
  {G.task_tag(4) == 8 : U32}

law unsupported_tasks_have_no_tag:
  {G.task_tag(99) == 255 : U32}

law button_nodes_are_buttons:
  {G.widget_role(0) == 1 : U32}

law link_nodes_are_pointer_targets:
  {G.widget_role(1) == 4 : U32}

law quoted_click_instruction:
  {G.click_query("Alpha") == "Click \"Alpha\"." : String}

law focus_instruction_is_source_text:
  {G.focus_query() == "Focus into the textbox." : String}

law sequence_instruction_is_source_text:
  {G.sequence_query() == "Click button ONE, then click button TWO." : String}

law choose_instruction_names_target_and_submit:
  {G.choose_query("Canada") == "Select Canada from the list and click Submit." : String}
