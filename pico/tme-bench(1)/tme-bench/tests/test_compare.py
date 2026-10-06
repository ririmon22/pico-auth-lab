import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('compare', Path(__file__).parents[1] / 'compare.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

class CompareTests(unittest.TestCase):
    def test_percent(self):
        self.assertAlmostEqual(m.delta(100, 90), -10)
        self.assertAlmostEqual(m.delta(100, 110), 10)
    def test_constant_interval(self):
        lo, hi = m.interval([100]*10, [90]*10)
        self.assertAlmostEqual(lo, -10)
        self.assertAlmostEqual(hi, -10)
    def test_faults_and_switch_filter(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / 'data.csv'
            header = 'label,test,' + ','.join(m.CONFIG) + ',metric,valid,nvcsw,nivcsw\n'
            cfg = '2,512,31,1,12345,never,0,0'
            p.write_text(header + f'off,read,{cfg},100,1,0,0\n' +
                         f'off,read,{cfg},80,0,0,0\n' + f'off,read,{cfg},90,1,0,1\n')
            _, groups, rejected = m.load(p, 'off', False)
            self.assertEqual(groups['read'], [100,90]); self.assertEqual(rejected,1)
            _, groups, rejected = m.load(p, 'off', True)
            self.assertEqual(groups['read'], [100]); self.assertEqual(rejected,2)
            with self.assertRaises(ValueError): m.load(p, 'on', False)

if __name__ == '__main__': unittest.main()
