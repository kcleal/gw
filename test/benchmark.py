"""
This script benchmarks the time and memory usage of genome browsers when generating am image of a
target region.
The script has been tested on Ubuntu and Mac.

Benchmark Requirements
----------------------
gw v0.7.0
IGV v2.16.0
Jbrowse2 v2.4.0
samplot v1.3

Mac requirements
----------------
gnu-time, install using 'brew install gnu-time'

example usage
-------------
python3 --help
python3 benchmark_time.py $HG19 NA12878.bam ../gw gw 1 ''


Output
------
Output is a csv file in the current directory

"""
import argparse
from subprocess import run, Popen, PIPE
import random
try:
    import pandas as pd
    have_pandas = True
except ImportError:
    have_pandas = False
import os
import platform
from collections import defaultdict


def plot_gw(chrom, start, end, args, timef, threads, extra_args):
    com = timef + " -o gwtime.txt {gw} {genome} {extra_args} -t {threads} -b {bam} -r {chrom}:{start}-{end} --file images/gw.png --no-show -d 1500x1500"\
        .format(gw=args.tool_path, genome=args.ref_genome, bam=args.bam, chrom=chrom, start=start, end=end, extra_args=extra_args, threads=threads)
    run(com, shell=True)
    line = open('gwtime.txt', 'r').readlines()[0].strip().split("\t")
    t = float(line[0])
    m = float(line[1])
    return t, m


def plot_igv(chrom, start, end, args, timef, threads):
    # Configure igv max visibility range to 250000000
    # also remove hg19.json from ~/igv/genomes
    # To set single thread mode add this to igv.args
    # -XX:ActiveProcessorCount=1
    igv_path = os.path.dirname(args.tool_path)
    # Mac path could be at
    # igv_path = "/Applications/IGV_2.16.0.app/Contents/Java"
    with open(igv_path + "/igv.args", "r") as f:
        lines = f.readlines()
    with open(igv_path + "/igv.args", "w") as f:
        for line in lines:
            if 'ActiveProcessorCount' in line:
                f.write(f'-XX:ActiveProcessorCount={threads}')
                continue
            f.write(line)
    v = """new
genome {genome}
load {bam}
snapshotDirectory images
goto {chrom}:{start}-{end}
snapshot igv.png
exit""".format(genome=args.ref_genome, bam=args.bam, chrom=chrom, start=start, end=end)
    with open("igv_batch.bat", "w") as b:
        b.write(v)
    com = timef + " -o igvtime.txt {igv} --batch igv_batch.bat".format(igv=args.tool_path)
    run(com, shell=True)
    line = open('igvtime.txt', 'r').readlines()[0].strip().split("\t")
    t = float(line[0])
    m = float(line[1])
    return t, m


def plot_jbrowse2(chrom, start, end, args, timef, threads=1):
    com = "export NODE_OPTIONS=--max_old_space_size=320000; " + timef + " -o jbrowse2time.txt  {jb2export} --fasta {genome} --bam {bam} force:true --loc {chrom}:{start}-{end} --out images/jb2_image.svg" \
        .format(jb2export=args.tool_path, genome=args.ref_genome, bam=args.bam, chrom=chrom, start=start, end=end)
    run(com, shell=True)
    line = open('jbrowse2time.txt', 'r').readlines()[0].strip().split("\t")
    t = float(line[0])
    m = float(line[1])
    return t, m


def plot_samplot(chrom, start, end, args, timef, threads=1):
    com = timef + " -o samplottime.txt {samplot} plot -r {genome} -b {bam} -c {chrom} -s {start} -e {end} -o images/samplot_image.png -W 5 -H 5" \
        .format(samplot=args.tool_path, genome=args.ref_genome, bam=args.bam, chrom=chrom, start=start, end=end)
    run(com, shell=True)
    line = open('samplottime.txt', 'r').readlines()[0].strip().split("\t")
    t = float(line[0])
    m = float(line[1])
    return t, m


def samtools_count(chrom, start, end, args, timef, threads):
    p = Popen(timef + f' -o samtoolstime.txt samtools view -@{threads} -c {args.bam} {chrom}:{start}-{end}', stdout=PIPE, stderr=PIPE, shell=True)
    out, err = p.communicate()
    reads = int(out.decode('ascii').strip())
    line = open('samtoolstime.txt', 'r').readlines()[0].strip().split("\t")
    t = float(line[0])
    return t, reads


def overlap(start1, end1, start2, end2):
    return end1 >= start2 and end2 >= start1


if __name__ == "__main__":

    parser = argparse.ArgumentParser(
        prog='GW benchmark',
        description="Test GW vs other genome visualization tools "
                    "benchmark.py hg19.fasta gaps.bed HG002.bam gw gw 1 ''",
    )

    parser.add_argument('ref_genome')
    parser.add_argument('ref_gaps')
    parser.add_argument('bam')
    parser.add_argument('tool_path')
    parser.add_argument('tool_name', choices=['gw', 'igv', 'samplot', 'jbrowse2'])
    parser.add_argument('threads')
    parser.add_argument('extra_args')
    args = parser.parse_args()

    random.seed(1)

    if platform.system() == "Linux":
        timef = "/usr/bin/time --format '%e\t%M' "
    else:  # Assuming Mac
        timef = "gtime --format '%e\t%M' "

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
    progs = {'gw': plot_gw, 'igv': plot_igv, 'samplot': plot_samplot, 'jbrowse2': plot_jbrowse2}
    name = args.tool_name
    extra_args = args.extra_args
    threads = args.threads
    prog = progs[name]

    # Select some random regions
    chroms = list(fai.keys())
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
                # sanity check for any reads. also helps trigger os hdd buffering making benchmarking more stable
                st_t, reads = samtools_count(chrom, start, end, args, timef, threads)
                if reads > 0:
                    good = True
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

            print('Region:', chrom, start, end, 'n_reads:', reads)

            # run samtools again to prime disk buffering
            _, _ = samtools_count(chrom, start, end, args, timef, threads)
            if name == "gw":
                avg_time, maxRSS = prog(chrom, start, end, args, timef, threads, extra_args)
            else:
                avg_time, maxRSS = prog(chrom, start, end, args, timef, threads)
            st_t, reads = samtools_count(chrom, start, end, args, timef, threads)

            results.append({'name': name, 'region': f'{chrom}:{start}-{end}', 'time (s)': avg_time,
                            'region size (bp)': end - start, 'RSS': maxRSS,
                            'samtools_count (s)': st_t, 'reads': reads, 'threads': threads})

    cols = ['name', 'region', 'reads', 'region size (bp)', 'samtools_count (s)', 'time (s)', 'RSS', 'threads']
    if have_pandas:
        df = pd.DataFrame.from_records(results)
        df = df[cols]
        df.to_csv(f'{name + extra_args.replace(" ", "_")}.{threads}.benchmark.csv', index=False)
    else:
        with open(f'{name + extra_args.replace(" ", "_")}.{threads}.benchmark.csv', 'w') as o:
            o.write(','.join(cols) + "\n")
            for r in results:
                o.write('{n},{r},{rds},{s},{st},{tim},{mem},{th}\n'.format(
                    n=r['name'],
                    r=r['region'],
                    rds=r['reads'],
                    s=r['region size (bp)'],
                    st=r['samtools_count (s)'],
                    tim=r['time (s)'],
                    mem=r['RSS'],
                    th=r['threads'],
                ))

    if name == 'samplot':
        run('rm samplottime.txt', shell=True)
    elif name == 'igv':
        run('rm igvtime.txt', shell=True)
        run('rm igv_batch.bat', shell=True)
    elif name == 'jbrowse2':
        run('rm jbrowse2time.txt', shell=True)
    else:
        run('rm gwtime.txt', shell=True)
    run('rm samtoolstime.txt', shell=True)
