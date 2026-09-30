import array,importlib.util,random,tempfile,unittest
from pathlib import Path
ENV=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('upgrade',ENV/'tools/upgrade_checkpoint.py');upgrade=importlib.util.module_from_spec(spec);spec.loader.exec_module(upgrade)
class UpgradeTests(unittest.TestCase):
 def test_encoder_and_all_remaining_weights_are_preserved(self):
  h,l=16,2;rng=random.Random(7)
  weights=array.array('f',(rng.uniform(-1,1) for _ in range((1224+395)*h+3*h*h*l)))
  with tempfile.TemporaryDirectory() as tmp:
   source=Path(tmp)/'old.bin';dest=Path(tmp)/'current.bin';source.write_bytes(weights.tobytes())
   metadata=upgrade.upgrade(source,dest);current=array.array('f');current.frombytes(dest.read_bytes())
   self.assertEqual(source.read_bytes(),weights.tobytes());self.assertEqual(metadata['hidden_size'],h);self.assertEqual(metadata['num_layers'],l)
   self.assertEqual(current[h*1234:],weights[h*1224:])
   obs=[rng.uniform(-1,1) for _ in range(1234)]
   for i in range(h):
    self.assertEqual(current[i*1234:i*1234+1224],weights[i*1224:(i+1)*1224])
    self.assertEqual(sum(current[i*1234+j]*obs[j] for j in range(1234)),sum(weights[i*1224+j]*obs[j] for j in range(1224)))
   with self.assertRaises(FileExistsError):upgrade.upgrade(source,dest)
if __name__=='__main__':unittest.main()
