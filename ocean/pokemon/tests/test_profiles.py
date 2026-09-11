"""End-to-end checks for the already-built ./pokemon evaluator."""
import json
import subprocess
import unittest


class ProfileTests(unittest.TestCase):
    def run_eval(self, *extra):
        return subprocess.check_output(['./pokemon','eval','random','random','--games=4',*extra],text=True)

    def test_no_gameplay_change(self):
        plain=self.run_eval()
        profiled=self.run_eval('--profile-json')
        self.assertEqual(plain,''.join(line+'\n' for line in profiled.splitlines() if not line.startswith('PK_PROFILE ')))
        rows=[json.loads(line[11:]) for line in profiled.splitlines() if line.startswith('PK_PROFILE ')]
        self.assertEqual(len(rows),4)
        for row in rows:
            d=row['descriptors']
            self.assertEqual(sum(d['species']),6)
            self.assertEqual(sum(d['leads']),1)
            self.assertTrue(6<=sum(d['types'])<=12)
            self.assertIn(row['score'],(0,.5,1))
            for key in ('mean_team_hp','survivors','duration','timeout'):
                self.assertTrue(0<=d[key]<=1,(key,d[key]))

    def test_timeout(self):
        rows=[json.loads(line[11:]) for line in self.run_eval('--profile-json','env.max_updates=1').splitlines() if line.startswith('PK_PROFILE ')]
        self.assertTrue(all(r['score']==.5 and r['descriptors']['timeout']==1 for r in rows))

    def test_compact_summary(self):
        text=self.run_eval('--profile')
        self.assertNotIn('PK_PROFILE',text)
        self.assertIn(self.run_eval().strip(),text)
        self.assertIn('Top species (% of teams)',text)
        self.assertIn('Top leads (% of games)',text)
        self.assertLess(len(text.splitlines()),15)


if __name__=='__main__': unittest.main()
