import Base
import ../../miniwob/TextPROOF.bend as TextProof
import ./Model.bend as M
import ./LAWS.bend as L

def L.finished_absorbs(kind, fields, focus, clipboard, popup, mode, seen,
    matches, total, scaled, action, elapsed, limit):
  {==}

def L.timeout_absorbs(kind, fields, focus, clipboard, popup, mode, seen,
    action, elapsed, limit):
  {==}

def L.deadline_preempts(kind, fields, focus, clipboard, popup, mode, seen,
    action):
  {==}

def L.popup_cancel_restores_form(kind, fields, focus, clipboard, mode, seen):
  {==}

def L.popup_ok_fails(kind, fields, focus, clipboard, mode, seen):
  {==}
