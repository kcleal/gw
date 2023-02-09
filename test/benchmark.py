"""
This script benchmarks the time and memory usage of genome browsers when generating am image of a
target region.
The script has been tested on Ubuntu and Mac.

Requirements
------------
gw v0.5.0
IGV v2.11.9
Jbrowse2 v2.3.2
samplot v1.3

Mac requirements
----------------
gnu-time, install using 'brew install gnu-time'

example usage
-------------
python3 --help
python3 benchmark.py $HG19 NA12878.bam ../gw gw
python3 ../benchmark.py $HG19 HG002.bam igv.sh igv
python3 ../benchmark.py $HG19 HG002.bam jb2export jbrowse2
python3 ../benchmark.py $HG19 HG002.bam samplot samplot

Output
------
Output is a csv file in the current directory

"""
import argparse
from subprocess import run, Popen, PIPE
from multiprocessing import Process, Pipe
import random
import pandas as pd
import resource
import os
import platform
from collections import defaultdict


splitf = lambda err: float(err.decode('ascii').split('\n')[0].strip())
mem_usage = lambda: resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss / 1e6


def plot_gw(conn, chrom, start, end, args):
    com = timef + " --format '%e' {gw} {genome} -b {bam} -r {chrom}:{start}-{end} --outdir images --no-show -d 1500x1500"\
        .format(gw=args.tool_path, genome=args.ref_genome, bam=args.bam, chrom=chrom, start=start, end=end)
    p = Popen(com, shell=True, stdout=PIPE, stderr=PIPE)
    out, err = p.communicate()
    t = splitf(err)
    conn.send([t, mem_usage()])
    conn.close()


def plot_igv(conn, chrom, start, end, args):
    # remove this to test file creation speed - snapshot images/igv.png
    v = """new
genome {genome}
load {bam}
snapshotDirectory images
goto {chrom}:{start}-{end}
snapshot images/igv.png
exit""".format(genome=args.ref_genome, bam=args.bam, chrom=chrom, start=start, end=end)
    with open("igv_batch.bat", "w") as b:
        b.write(v)
    run(f"echo {chrom}\t{start}\t{end}\n > regions.bed", shell=True)
    com = timef + " -o igvtime.txt --format '%e' {igv} --batch igv_batch.bat".format(igv=args.tool_path)
    p = Popen(com, shell=True, stderr=PIPE)
    out, err = p.communicate()
    t = float(open('igvtime.txt', 'r').readlines()[0].strip())
    conn.send([t, mem_usage()])
    conn.close()


def plot_jbrowse2(conn, chrom, start, end, args):
    com = "export NODE_OPTIONS=--max_old_space_size=320000; " + timef + " --format '%e' {jb2export} --fasta {genome} --bam {bam} force:true --loc {chrom}:{start}-{end} --out images/jb2_image.svg" \
        .format(jb2export=args.tool_path, genome=args.ref_genome, bam=args.bam, chrom=chrom, start=start, end=end)
    p = Popen(com, shell=True, stdout=PIPE, stderr=PIPE)
    out, err = p.communicate()
    t = splitf(err)
    conn.send([t, mem_usage()])
    conn.close()


def plot_samplot(conn, chrom, start, end, args):
    com = timef + " --format '%e' {samplot} plot -r {genome} -b {bam} -c {chrom} -s {start} -e {end} -o images/samplot_image.png -W 5 -H 5" \
        .format(samplot=args.tool_path, genome=args.ref_genome, bam=args.bam, chrom=chrom, start=start, end=end)
    p = Popen(com, shell=True, stderr=PIPE)
    out, err = p.communicate()
    err = [i for i in err.decode('ascii').split('\n') if 'Warning' not in i][0]
    t = float(err)
    conn.send([t, mem_usage()])
    conn.close()


def samtools_count(conn, chrom, start, end, args):
    p = Popen(timef + f' --format "%e" samtools view -@3 -c {args.bam} {chrom}:{start}-{end}', stdout=PIPE, stderr=PIPE, shell=True)
    out, err = p.communicate()
    reads = int(out.decode('ascii').strip())
    t = splitf(err)
    conn.send([t, reads])
    conn.close()


if __name__ == "__main__":

    parser = argparse.ArgumentParser(
        prog='GW benchmark',
        description='Test GW vs other genome visualization tools',
    )

    parser.add_argument('ref_genome')
    parser.add_argument('ref_gaps')
    parser.add_argument('bam')
    parser.add_argument('tool_path')
    parser.add_argument('tool_name', choices=['gw', 'igv', 'samplot', 'jbrowse2'])
    args = parser.parse_args()

    random.seed(1)

    if platform.system() == "Linux":
        timef = "/usr/bin/time "
    else:  # Assuming Mac
        timef = "gtime "

    if not os.path.exists('images'):
        os.mkdir('images')

    if args.ref_gaps != "NA":
        gaps_list = pd.read_csv(args.ref_gaps, sep='\t')[['chrom', 'chromStart', 'chromEnd']].to_records(index=False)
        gaps = defaultdict(list)
        for chrom, s, e in gaps_list:
            gaps[chrom].append(pd.Interval(s, e))
    else:
        gaps = {}

    region_sizes = [2, 2000, 20_000, 200_000, 2_000_000]
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
        while N < 10:
            chrom = chroms[c]
            good = False
            for j in range(10):  # 10 tried to find an interval without overlapping a gap
                pos = random.randint(half_size, fai[chrom] - half_size)
                start = pos - half_size
                end = pos + half_size
                itv = pd.Interval(start, end)
                if chrom in gaps and any(itv.overlaps(itv2) for itv2 in gaps[chrom]):
                    continue
                # sanity check for any reads. also helps trigger os hdd buffering making benchmarking more stable
                parent_conn, child_conn = Pipe()
                p = Process(target=samtools_count, args=(child_conn, chrom, start, end, args))
                p.start()
                st_t, reads = parent_conn.recv()
                p.join()
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

            pos = random.randint(half_size, fai[chrom] - half_size)
            start = pos - half_size
            end = pos + half_size
            print(chrom, start, end)

            # run samtools again to prime disk buffering
            _, child_conn = Pipe()
            p = Process(target=samtools_count, args=(child_conn, chrom, start, end, args))
            p.start()
            p.join()

            # getrusage finds max mem usage of parent process, so we need to launch the function in a new child process
            # and collect mem usage, otherwise max mem usage of this parent process will be recorded.
            parent_conn, child_conn = Pipe()
            p = Process(target=prog, args=(child_conn, chrom, start, end, args))
            p.start()
            avg_time, maxRSS = parent_conn.recv()
            p.join()

            parent_conn, child_conn = Pipe()
            p = Process(target=samtools_count, args=(child_conn, chrom, start, end, args))
            p.start()
            st_t, reads = parent_conn.recv()
            p.join()

            results.append({'name': name, 'region': f'{chrom}:{start}-{end}', 'time (s)': avg_time,
                            'region size (bp)': end - start, 'RSS': maxRSS,
                            'samtools_count_t3 (s)': st_t, 'reads': reads})

    df = pd.DataFrame.from_records(results)
    df = df[['name', 'region', 'reads', 'region size (bp)', 'samtools_count_t3 (s)', 'time (s)', 'RSS']]
    df.to_csv(f'{name}.benchmark.csv', index=False)
