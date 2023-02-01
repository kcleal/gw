"""
This script benchmarks the time and memory usage of genome browsers when generating am image of a
target region.
The script has been tested on Ubuntu and Mac.

Requirements
------------
gw
IGV
Jbrowse2
samplot

Mac requirements
----------------
gnu-time, install using 'brew install gnu-time'

example usage
-------------
python3 benchmark.py $HG19 NA12878.bwa.bam ../gw gw

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


splitf = lambda err: float(err.decode('ascii').split('\n')[0].strip())
mem_usage = lambda: resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss / 1e6


def plot_gw(conn, chrom, start, end, args):
    com = timef + " --format '%e' {gw} {genome} -b {bam} -r {chrom}:{start}-{end} --outdir images --no-show"\
        .format(gw=args.tool_path, genome=args.ref_genome, bam=args.bam, chrom=chrom, start=start, end=end)
    p = Popen(com, shell=True, stdout=PIPE, stderr=PIPE)
    out, err = p.communicate()
    t = splitf(err)
    conn.send([t, mem_usage()])
    conn.close()


def plot_igv(conn, chrom, start, end, args):
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
    com = "export NODE_OPTIONS=--max_old_space_size=320000; " + timef + " --format '%e' {jb2export} --fasta {genome} --bam {bam} force:true --loc {chrom}:{start}-{end} --out images/jb2_image.svg --width 1024" \
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
    conn.send([t, mem_usage()])
    conn.close()


if __name__ == "__main__":

    parser = argparse.ArgumentParser(
        prog='GW benchmark',
        description='Test GW vs other genome visualization tools',
    )

    parser.add_argument('ref_genome')
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

    for size in region_sizes:
        print('Size: ', size)
        half_size = int(size * 0.5)
        random.shuffle(chroms)
        for c in range(10):
            chrom = chroms[c]
            pos = random.randint(half_size, fai[chrom] - half_size)
            start = pos - half_size
            end = pos + half_size
            print(chrom, start, end)

            # run samtools once beforehand to prime disk buffering, otherwise the second run of samtools
            # will always be quicker on a mechanical drive
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

            results.append({'name': name, 'time (s)': avg_time, 'region size (bp)': end - start, 'RSS': maxRSS,
                            'samtools_count_t3 (s)': st_t, 'reads': reads})

    df = pd.DataFrame.from_records(results)
    df = df[['name', 'reads', 'region size (bp)', 'samtools_count_t3 (s)', 'time (s)', 'RSS']]
    df.to_csv(f'{name}.benchmark.csv', index=False)
