#!/usr/bin/env python3
"""Headless startup regressions; requires a built ./gw and samtools on PATH.

All fixtures and renders live in a temporary directory. Run from any directory:
    python3 test/test_startup_validation.py
"""
import gzip
import pathlib
import shutil
import subprocess
import tempfile
import unittest

GW = pathlib.Path(__file__).resolve().parents[1] / 'gw'


class StartupValidation(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not GW.is_file() or not shutil.which('samtools'):
            raise unittest.SkipTest('Build gw and install samtools first')
        cls.temp = tempfile.TemporaryDirectory(prefix='gw-startup-')
        cls.root = pathlib.Path(cls.temp.name)
        for chrom in ('1', 'chr1', 'other'):
            ref = cls.root / (chrom + '.fa')
            ref.write_text('>' + chrom + '\n' + 'A' * 500 + '\n')
            subprocess.run(['samtools', 'faidx', str(ref)], check=True)
        for chrom in ('1', 'chr1'):
            sam = cls.root / (chrom + '.sam')
            sam.write_text('@HD\tVN:1.6\tSO:coordinate\n@SQ\tSN:' + chrom + '\tLN:500\n'
                           'read1\t0\t' + chrom + '\t451\t60\t50M\t*\t0\t0\t'
                           + 'A' * 50 + '\t' + 'I' * 50 + '\n')
            bam = cls.root / (chrom + '.bam')
            subprocess.run(['samtools', 'view', '-b', '-o', str(bam), str(sam)], check=True)
            subprocess.run(['samtools', 'index', str(bam)], check=True)
        shutil.copyfile(cls.root / '1.bam', cls.root / 'unindexed.bam')
        (cls.root / 'invalid.bam').write_text('invalid alignment header\n')

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def render(self, ref, bam, region, *extra):
        out = self.root / 'render.png'
        out.unlink(missing_ok=True)
        result = subprocess.run([str(GW), str(self.root / (ref + '.fa')),
                                 '-b', str(self.root / (bam + '.bam')),
                                 '-r', region, '-n', '-d', '600x350', '-f', str(out), *extra],
                                cwd=self.root, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(out.read_bytes()[:8], b'\x89PNG\r\n\x1a\n')
        return result

    def test_contig_aliases_buffered_and_streamed(self):
        for ref, bam, region in [('1', 'chr1', 'chr1:440-500'),
                                  ('chr1', '1', 'chr1:440-500'),
                                  ('1', 'chr1', '1:440-500'),
                                  ('chr1', '1', '1:440-500')]:
            for extra in [('-c', 'sort strand'), ('-t', '1'), ('-t', '2')]:
                with self.subTest(ref=ref, bam=bam, region=region, extra=extra):
                    # Large regions exercise streamed drawing with one or multiple threads.
                    view = region if extra[0] == '-c' else region.split(':')[0] + ':1-2000000'
                    result = self.render(ref, bam, view, *extra)
                    self.assertNotIn('unknown sequence', result.stderr)
                    self.assertNotIn('reference has no bases', result.stderr)

    def test_unknown_contig(self):
        result = self.render('1', '1', 'absent:10-50')
        self.assertIn('reference has no bases', result.stderr)
        self.assertIn('unknown sequence', result.stderr)

    def test_missing_reference_contig(self):
        self.render('other', '1', '1:440-500')

    def test_out_of_bounds_reference(self):
        self.render('1', '1', '1:490-550')
        self.render('1', '1', '1:600-650')

    def test_invalid_alignment_inputs(self):
        for bam in ('unindexed', 'invalid', 'missing'):
            with self.subTest(bam=bam):
                result = subprocess.run([str(GW), str(self.root / '1.fa'), '-b',
                                         str(self.root / (bam + '.bam')), '-r', '1:10-50', '-n'],
                                        cwd=self.root, capture_output=True, text=True, timeout=30)
                self.assertGreater(result.returncode, 0, result.stderr)  # no fatal signal
                self.assertIn('Error:', result.stderr)

    def assert_clean_error(self, *args):
        result = subprocess.run([str(GW), *map(str, args)], cwd=self.root,
                                capture_output=True, text=True, timeout=30)
        self.assertGreater(result.returncode, 0, result.stderr)
        self.assertIn('Error:', result.stderr)
        self.assertNotIn('uncaught exception', result.stderr)
        self.assertNotIn('libc++abi', result.stderr)
        return result

    def test_missing_variants_before_window_creation(self):
        for suffix in ('vcf', 'vcf.gz', 'bcf', 'bed'):
            path = self.root / ('missing.' + suffix)
            for extra in ((), ('-n', '-o', self.root)):
                with self.subTest(suffix=suffix, extra=extra):
                    result = self.assert_clean_error(self.root / '1.fa', '-v', path, *extra)
                    self.assertIn(str(path), result.stderr)
                    self.assertNotIn('GLFW', result.stderr)
        # Validation also precedes the interactive genome picker.
        self.assert_clean_error('-v', self.root / 'missing.vcf')

    def test_variant_open_errors(self):
        invalid = self.root / 'invalid.vcf'
        invalid.write_text('invalid variant header\n')
        unindexed = self.root / 'unindexed.vcf.gz'
        with gzip.open(unindexed, 'wt') as out:
            out.write('##fileformat=VCFv4.2\n##contig=<ID=1,length=500>\n'
                      '#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\n')
        for path in (invalid, unindexed):
            with self.subTest(path=path):
                result = self.assert_clean_error(self.root / '1.fa', '-v', path, '-n', '-o', self.root)
                self.assertIn(str(path), result.stderr)

    def test_other_input_exceptions(self):
        for extra in (('-r', '1:50-10'), ('-d', 'invalid')):
            with self.subTest(extra=extra):
                self.assert_clean_error(self.root / '1.fa', '-n', *extra)
        invalid = self.root / 'invalid.bed'
        invalid.write_text('1\tinvalid\t100\n')
        self.assert_clean_error(self.root / '1.fa', '-n', '--track', invalid, '-r', '1:10-50')


if __name__ == '__main__':
    unittest.main()
