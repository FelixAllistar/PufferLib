import ctypes
import importlib.util
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

BENCH = Path(__file__).parents[1]/'benchmarks'
sys.path.insert(0,str(BENCH))
spec=importlib.util.spec_from_file_location('evaluate_private',BENCH/'evaluate_private.py')
evaluate=importlib.util.module_from_spec(spec)
spec.loader.exec_module(evaluate)

class NativeProgressTest(unittest.TestCase):
    def test_progress_and_final_protocol(self):
        with tempfile.TemporaryDirectory() as directory:
            out=Path(directory)
            evaluate.write_native_progress(out,[],6)
            self.assertEqual((out/'progress.txt').read_text(),'METAMON_V1 0 0 0 0 0 6\n')
            rows=[dict(score=score) for score in (1,0,.5,1,0,1)]
            evaluate.write_native_progress(out,rows,6,final=True)
            fields=(out/'result.txt').read_text().split()
            self.assertEqual(fields[:5],['METAMON_V1','6','3','1','2'])
            self.assertAlmostEqual(float(fields[5]),3.5/6)
            self.assertEqual(fields[6],'6')
            self.assertFalse((out/'result.txt.tmp').exists())

    def test_parent_guard(self):
        with mock.patch.object(evaluate.C,'CDLL') as libc, mock.patch.object(evaluate.os,'getppid',return_value=123):
            libc.return_value.prctl.return_value=0
            evaluate.bind_parent(0)
            libc.assert_not_called()
            evaluate.bind_parent(123)
            if sys.platform.startswith('linux'):
                libc.return_value.prctl.assert_called_once()
            with self.assertRaises(RuntimeError):
                evaluate.bind_parent(124)

if __name__=='__main__':unittest.main()
