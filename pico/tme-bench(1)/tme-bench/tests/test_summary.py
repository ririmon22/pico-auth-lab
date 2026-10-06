import csv
import math
import os
from pathlib import Path
import statistics
import subprocess
import tempfile
import unittest

class SummaryCSVTests(unittest.TestCase):
    def test_summary_matches_valid_raw_samples(self):
        binary = Path(__file__).resolve().parents[1] / 'tme_bench'
        cpu = min(os.sched_getaffinity(0))
        with tempfile.TemporaryDirectory() as d:
            path = Path(d)/'samples.csv'
            result = subprocess.run([str(binary), '-c',str(cpu),'-s','1','-r','3','-t','.01',
                                     '-o',str(path)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            with path.open() as f: raw=list(csv.DictReader(f))
            with Path(str(path)+'.summary.csv').open() as f: summaries=list(csv.DictReader(f))
            self.assertEqual(len(summaries),4)
            for row in summaries:
                self.assertNotIn(None,row)
                samples=[r for r in raw if r['test']==row['test'] and r['valid']=='1']
                values=[float(r['metric']) for r in samples]
                self.assertEqual(int(row['valid_runs']),len(values))
                self.assertAlmostEqual(float(row['mean']),statistics.mean(values),places=7)
                self.assertAlmostEqual(float(row['median']),statistics.median(values),places=7)
                self.assertAlmostEqual(float(row['sd_population']),statistics.pstdev(values),places=7)
                if len(values)>1:
                    self.assertAlmostEqual(float(row['sd_sample']),statistics.stdev(values),places=7)
                else:
                    self.assertEqual(row['sd_sample'],'')
                self.assertAlmostEqual(float(row['p95']),sorted(values)[math.ceil(.95*len(values))-1],places=7)
                field='mean_latency_ns_load' if row['test']=='latency' else 'mean_throughput_mib_s'
                other='mean_throughput_mib_s' if row['test']=='latency' else 'mean_latency_ns_load'
                self.assertEqual(row[field],row['mean']); self.assertEqual(row[other],'')
            before=Path(str(path)+'.summary.csv').read_bytes()
            again=subprocess.run([str(binary),'-c',str(cpu),'-s','1','-r','1','-t','.01','-o',str(path)],capture_output=True)
            self.assertEqual(again.returncode,0,again.stderr)
            after=Path(str(path)+'.summary.csv').read_bytes()
            self.assertTrue(after.startswith(before))
            with path.open() as f: appended=list(csv.DictReader(f))
            self.assertEqual(len(appended),len(raw)+4)
            self.assertEqual(len({r['execution_id'] for r in appended}),2)
            with Path(str(path)+'.summary.csv').open() as f: added=list(csv.DictReader(f))
            self.assertEqual(len(added),8)
            self.assertTrue(all(None not in r for r in added))
            self.assertTrue(all(r['sd_sample']=='' for r in added[4:]))
            # Unknown schemas must not be changed or appended.
            bad=Path(d)/'bad.csv'; bad.write_text('different,header\nold,row\n')
            prior=bad.read_bytes()
            rejected=subprocess.run([str(binary),'-c',str(cpu),'-s','1','-r','1','-t','.01','-o',str(bad)],capture_output=True)
            self.assertNotEqual(rejected.returncode,0)
            self.assertEqual(prior,bad.read_bytes())
            # v1.1 schema remains appendable without changing old rows/header.
            old=Path(d)/'legacy.csv'
            old.write_text(','.join(list(raw[0])[:-2])+'\n')
            legacy=subprocess.run([str(binary),'-c',str(cpu),'-s','1','-r','1','-t','.01','--test','read','-o',str(old)],capture_output=True)
            self.assertEqual(legacy.returncode,0,legacy.stderr)
            with old.open() as f: oldrows=list(csv.DictReader(f))
            self.assertEqual(len(oldrows),1); self.assertNotIn(None,oldrows[0])

if __name__=='__main__': unittest.main()
