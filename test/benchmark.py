import argparse
from subprocess import run, Popen
import random
import time
import pandas as pd
import resource
import os


def plot_gw(chrom, start, end, args):
    t0 = time.time()
    com = "{gw} {genome} -b {bam} -r {chrom}:{start}-{end} --outdir images --no-show"\
        .format(gw=args.tool_path, genome=args.ref_genome, bam=args.bam, chrom=chrom, start=start, end=end)
    p = Popen(com, shell=True)
    p.wait()
    t = (time.time() - t0) / 10
    return t, resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss / 1e6


def plot_igv(chrom, start, end, args):
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
    t0 = time.time()
    com = "{igv} --batch igv_batch.bat".format(igv=args.tool_path)
    p = Popen(com, shell=True)
    p.wait()
    t = (time.time() - t0) / 10
    return t, resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss / 1e6


def plot_samplot(chrom, start, end, args):
    t0 = time.time()
    com = "{samplot} plot -r {genome} -b {bam} -c {chrom} -s {start} -e {end} -o images/samplot_image.png" \
        .format(samplot=args.tool_path, genome=args.ref_genome, bam=args.bam, chrom=chrom, start=start, end=end)
    p = Popen(com, shell=True)
    p.wait()
    t = (time.time() - t0) / 10
    return t, resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss / 1e6


if __name__ == "__main__":

    parser = argparse.ArgumentParser(
        prog='GW benchmark',
        description='Test GW vs other genome visualization tools',
    )

    parser.add_argument('ref_genome')
    parser.add_argument('bam')
    parser.add_argument('tool_path')
    parser.add_argument('tool_name', choices=['gw', 'igv', 'samplot'])
    args = parser.parse_args()

    if not os.path.exists('images'):
        os.mkdir('images')

    fai = {}
    with open(args.ref_genome + ".fai", "r") as fin:
        for line in fin:
            l = line.split(maxsplit=2)
            length = int(l[1])
            if length > 10_000_000:
                fai[l[0]] = length

    results = []
    progs = {'gw': plot_gw, 'igv': plot_igv, 'samplot': plot_samplot}
    name = args.tool_name
    prog = progs[name]

    # Select some random regions
    chroms = list(fai.keys())
    region_sizes = [2000, 20_000, 200_000, 2_000_000]
    for size in region_sizes:
        print('Size: ', size)
        half_size = int(size * 0.5)
        random.shuffle(chroms)
        for c in range(10):
            chrom = chroms[c]
            pos = random.randint(half_size, fai[chrom] - half_size + 1)
            start = pos - half_size
            end = pos + half_size
            avg_time, maxRSS = prog(chrom, start, end, args)
            results.append({'name': name, 'time (s)': avg_time, 'region size (bp)': end - start, 'MaxRSS (mb)': maxRSS})

    df = pd.DataFrame.from_records(results)
    df.to_csv(f'{name}.benchmark.csv')
    # sns.pointplot(df, x='region size (bp)', y='time (s)', hue='name')
    # plt.show()
