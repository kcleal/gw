"""
This script benchmarks time of GW only.
The script was tested on a Pixel 6 smartphone.

Benchmark Requirements
----------------------
gw v0.7.0


Termux requirements
----------------
pkg install hyperfine

example usage
-------------
python3 --help
python3 benchmark_time.py $HG19 NA12878.bam ../gw 1 ''


Output
------
Output is a csv file in the current directory

"""
import argparse
from subprocess import run
import random
try:
    import pandas as pd
    have_pandas = True
except ImportError:
    have_pandas = False
import os
from collections import defaultdict


def plot_gw(chrom, start, end, args, threads, extra_args):
    com = "hyperfine -r 1 --show-output --export-csv gwtime.txt '{gw} {genome} {extra_args} -t {threads} -b {bam} -r {chrom}:{start}-{end} --file images/gw.png --no-show -d 1500x1500' "\
        .format(gw=args.tool_path, genome=args.ref_genome, bam=args.bam, chrom=chrom, start=start, end=end, extra_args=extra_args, threads=threads)
    run(com, shell=True)
    line = open('gwtime.txt', 'r').readlines()[1].strip().split(",")[1]
    m = -1
    print(line)
    return line, m



def samtools_count(chrom, start, end, args, timef, threads):
   # p = Popen(timef + f' -o samtoolstime.txt samtools view -@{threads} -c {args.bam} {chrom}:{start}-{end}', stdout=PIPE, stderr=PIPE, shell=True)
  #  out, err = p.communicate()
 #   reads = int(out.decode('ascii').strip())
  #  line = open('samtoolstime.txt', 'r').readlines()[0].strip().split("\t")
 #   t = float(line[0])
  #  return t, reads
  return -1, -1


def overlap(start1, end1, start2, end2):
    return end1 >= start2 and end2 >= start1


if __name__ == "__main__":

    parser = argparse.ArgumentParser(
        prog='GW benchmark',
        description="Test GW time only "
                    "python benchmark_time.py hg19.fasta gaps.bed HG002.bam gw 1 ''",
    )

    parser.add_argument('ref_genome')
    parser.add_argument('ref_gaps')
    parser.add_argument('bam')
    parser.add_argument('tool_path')
    parser.add_argument('threads')
    parser.add_argument('extra_args')
    args = parser.parse_args()

    random.seed(1)

    if not os.path.exists('images'):
        os.mkdir('images')

    if args.ref_gaps != "NA":
        if have_pandas:
            gaps_list = pd.read_csv(args.ref_gaps, sep='\t')[['chrom', 'chromStart', 'chromEnd']].to_records(index=False)
            gaps = defaultdict(list)
            for chrom, s, e in gaps_list:
                gaps[chrom].append(pd.Interval(s, e))
        else:
            gaps = defaultdict(list)
            with open(args.ref_gaps, "r") as g:
                for line in g:
                    if line[0] == "#":
                        continue
                    l = line.strip().split("\t")
                    gaps[l[1]].append((int(l[2]), int(l[3])))
    else:
        gaps = {}

    region_sizes = [2, 2000, 20_000, 200_000, 2_000_000][::-1]
    max_size = 20_000_000
    fai = {}
    with open(args.ref_genome + ".fai", "r") as fin:
        for line in fin:
            l = line.split(maxsplit=2)
            length = int(l[1])
            if length > max_size:
                fai[l[0]] = length

    results = []
    progs = {'gw': plot_gw}
    name = 'gw'
    extra_args = args.extra_args
    threads = args.threads
    prog = plot_gw

    chroms = ['chr1'] * 100
    print(chroms)
    for size in region_sizes:
        print('Size: ', size)
        half_size = int(size * 0.5)
        random.shuffle(chroms)
        N = 0
        c = 0
        start = None
        end = None
        reads = 0
        while N < 20:
            print(c)
            chrom = chroms[c]
            good = False
            for j in range(10):  # 10 tried to find an interval without overlapping a gap
                pos = random.randint(half_size, fai[chrom] - half_size)
                start = pos - half_size
                end = pos + half_size
                if have_pandas:
                    itv = pd.Interval(start, end)
                    if chrom in gaps and any(itv.overlaps(itv2) for itv2 in gaps[chrom]):
                        continue
                else:
                    good = True
                    for istart, iend in gaps[chrom]:
                        if overlap(istart, iend, start, end):
                            good = False
                            break
                    if not good:
                        continue
                break

            if not good:
                c += 1
                continue
            N += 1
            c += 1
            if N >= len(chroms):
                raise ValueError('Error: ran out of chroms to plot')
            if start is None:
                raise ValueError('Error: couldnt find an interval with reads')

            print('Region:', chrom, start, end)

            avg_time, maxRSS = prog(chrom, start, end, args, threads, extra_args)

            results.append({'name': name, 'region': f'{chrom}:{start}-{end}', 'time (s)': avg_time,
                            'region size (bp)': end - start, 'RSS': maxRSS,
                            'threads': threads})

    cols = ['name', 'region', 'region size (bp)', 'time (s)', 'RSS', 'threads']
    if have_pandas:
        df = pd.DataFrame.from_records(results)
        df = df[cols]
        df.to_csv(f'{name + extra_args.replace(" ", "_")}.{threads}.benchmark.csv', index=False)
    else:
        with open(f'{name + extra_args.replace(" ", "_")}.{threads}.benchmark.csv', 'w') as o:
            o.write(','.join(cols) + "\n")
            for r in results:
                o.write('{n},{r},{s},{tim},{mem},{th}\n'.format(
                    n=r['name'],
                    r=r['region'],
                    s=r['region size (bp)'],
                    tim=r['time (s)'],
                    mem=r['RSS'],
                    th=r['threads'],
                ))

    run('rm gwtime.txt', shell=True)
