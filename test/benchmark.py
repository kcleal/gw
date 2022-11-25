import argparse
from subprocess import run, Popen, PIPE
import random
import pandas as pd
import resource
import os

random.seed(1)


def plot_gw(chrom, start, end, args):
    com = "/usr/bin/time --format '%e' {gw} {genome} -b {bam} -r {chrom}:{start}-{end} --outdir images --no-show"\
        .format(gw=args.tool_path, genome=args.ref_genome, bam=args.bam, chrom=chrom, start=start, end=end)
    p = Popen(com, shell=True, stdout=PIPE, stderr=PIPE)
    out, err = p.communicate()
    t = float(err.decode('ascii').split('\n')[0].strip())
    print(t)
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
    com = "/usr/bin/time -o igvtime.txt --format '%e' {igv} --batch igv_batch.bat".format(igv=args.tool_path)
    p = Popen(com, shell=True, stderr=PIPE)
    out, err = p.communicate()
    t = float(open('igvtime.txt', 'r').readlines()[0].strip())
    return t, resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss / 1e6


def plot_jbrowse2(chrom, start, end, args):
    com = "/usr/bin/time --format '%e' {jb2export} --fasta {genome} --bam {bam} force:true --loc {chrom}:{start}-{end} --out images/jb2_image.svg" \
        .format(jb2export=args.tool_path, genome=args.ref_genome, bam=args.bam, chrom=chrom, start=start, end=end)
    p = Popen(com, shell=True, stdout=PIPE, stderr=PIPE)
    out, err = p.communicate()
    return t, resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss / 1e6


def plot_samplot(chrom, start, end, args):
    com = "/usr/bin/time --format '%e' {samplot} plot -r {genome} -b {bam} -c {chrom} -s {start} -e {end} -o images/samplot_image.png" \
        .format(samplot=args.tool_path, genome=args.ref_genome, bam=args.bam, chrom=chrom, start=start, end=end)
    p = Popen(com, shell=True, stderr=PIPE)
    out, err = p.communicate()
    err = [i for i in err.decode('ascii').split('\n') if 'Warning' not in i][0]
    t = float(err)
    return t, resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss / 1e6


def plot_wally(chrom, start, end, args):
    com = "/usr/bin/time --format '%e' {wally} region -g {genome} -r {chrom}:{start}-{end} {bam}" \
        .format(wally=args.tool_path, genome=args.ref_genome, bam=args.bam, chrom=chrom, start=start, end=end)
    p = Popen(com, shell=True, stderr=PIPE)
    out, err = p.communicate()
    t = float(err.decode('ascii').split('\n')[0].strip())
    run('rm *.png', shell=True)
    return t, resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss / 1e6


def samtools_count(chrom, start, end, args):
    p = Popen(f'/usr/bin/time --format "%e" samtools view -@3 -c {args.bam} {chrom}:{start}-{end}', stdout=PIPE, stderr=PIPE, shell=True)
    out, err = p.communicate()
    t = float(err.decode('ascii').split('\n')[0].strip())
    reads = int(out.decode('ascii').strip())
    return t, reads


if __name__ == "__main__":

    parser = argparse.ArgumentParser(
        prog='GW benchmark',
        description='Test GW vs other genome visualization tools',
    )

    parser.add_argument('ref_genome')
    parser.add_argument('bam')
    parser.add_argument('tool_path')
    parser.add_argument('tool_name', choices=['gw', 'igv', 'samplot', 'wally', 'jbrowse2'])
    args = parser.parse_args()

    if not os.path.exists('images'):
        os.mkdir('images')

    region_sizes = [2, 2000, 20_000, 200_000, 2_000_000]
    max_size = max(region_sizes) * 2
    fai = {}
    with open(args.ref_genome + ".fai", "r") as fin:
        for line in fin:
            l = line.split(maxsplit=2)
            length = int(l[1])
            if length > max_size:
                fai[l[0]] = length

    results = []
    progs = {'gw': plot_gw, 'igv': plot_igv, 'samplot': plot_samplot, 'wally': plot_wally, 'jbrowse2': plot_jbrowse2}
    name = args.tool_name
    prog = progs[name]

    # Select some random regions
    chroms = list(fai.keys())

    for size in region_sizes:
        print('Size: ', size)
        half_size = int(size * 0.5)
        random.shuffle(chroms)
        for c in range(min(10, len(fai))):
            chrom = chroms[c]
            pos = random.randint(half_size, fai[chrom] - half_size)
            start = pos - half_size
            end = pos + half_size
            print(chrom, start, end)
            avg_time, maxRSS = prog(chrom, start, end, args)
            st_t, reads = samtools_count(chrom, start, end, args)
            results.append({'name': name, 'time (s)': avg_time, 'region size (bp)': end - start, 'MaxRSS': maxRSS,
                            'samtools_count_t3 (s)': st_t, 'reads': reads})

    df = pd.DataFrame.from_records(results)
    df = df[['name', 'reads', 'region size (bp)', 'samtools_count_t3 (s)', 'time (s)', 'MaxRSS']]
    df.to_csv(f'{name}.benchmark.csv', index=False)
