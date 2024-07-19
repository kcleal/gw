"""
Arguments
---------
glob_pattern
tag

Usage
-----
Dont forget quotes around glob:

python3 plot.py '*1.benchmark.csv' threads_1
python3 plot.py '*.benchmark.csv' threads_1_or_4
"""
import pandas as pd
import seaborn as sns
import glob
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import sys
pd.options.mode.chained_assignment = None

glob_pattern = sys.argv[1]
tag = sys.argv[2]
tables = glob.glob(glob_pattern)
print(tables)
dfs = []
for p in tables:
    d = pd.read_csv(p)
    d['file'] = p
    dfs.append(d)
df = pd.concat(dfs)
df['RSS'] = df['RSS'] / 1e6
print(df.columns)
print(df.name)

gw_times = {}
gw_mem = {}
samtools = {}
for idx, grp in df[(df['name'] == 'gw') & (df['threads'] == 1)].groupby('region size (bp)'):
    gw_times[idx] = grp['time (s)'].mean()
    gw_mem[idx] = grp['RSS'].mean()
for idx, grp in df.groupby(['region size (bp)', 'threads']):
    samtools[idx] = grp['samtools_count (s)'].mean()

print(gw_times)
# use the mean time of 2bp region as start_time
min_load_time = {k: dd['time (s)'].min() for k, dd in df[df['region size (bp)'] == df['region size (bp)'].min()].groupby('name')}
min_memory = {k: dd['RSS'].min() for k, dd in df[df['region size (bp)'] == df['region size (bp)'].min()].groupby('name')}
df['total_time'] = df['time (s)']
df['start_time'] = [min_load_time[k] for k in df['name']]
df['total_mem'] = df['RSS']
df['start_mem'] = [min_memory[k] for k in df['name']]
df = df[df['region size (bp)'] != 2]

df = df[~((df['name'] == 'bamsnap') & (df['region size (bp)'] > 200000)) ]
df = df[df['total_time'] != -1]

su = []
for idx, grp in df.groupby(['name', 'region size (bp)', 'threads']):
    su.append({'name': idx[0] if idx[2] == 1 else f'{idx[0]} -t{idx[2]}',
               'region size (bp)': idx[1],
               'samtools': samtools[(idx[1], idx[2])],
               'total_time': grp['total_time'].mean(),
               'start_time': grp['start_time'].mean(),
               'render': grp['total_time'].mean() - grp['start_time'].mean(),
               'total_mem': grp['total_mem'].mean(),
               'start_mem': grp['start_mem'].mean(),
               'relative_time': grp['total_time'].mean() / gw_times[idx[1]],
               "relative_mem": grp['total_mem'].mean() / gw_mem[idx[1]]})

df2 = pd.DataFrame.from_records(su)
gw_render_times = {k: t for k, t in zip(df2[df2['name'] == 'gw']['region size (bp)'],
                                        df2[df2['name'] == 'gw']['render'])}
print('render time', gw_render_times)

# 0.005 is the limit of /sr/bin/time
df2['relative_render_time'] = [k / gw_render_times[s] if gw_render_times[s] > 0 else -1 for k, s in zip(df2['render'], df2['region size (bp)'])]
df2 = df2[['name', 'region size (bp)', 'samtools', 'total_time', 'relative_time',
           'start_time', 'render', 'relative_render_time', 'total_mem', 'start_mem', 'relative_mem']]

order = {'gw': 0, 'gw -t4': 0.5, 'igv': 1, 'igv -t4': 1.5, 'jb2export': 2, 'samplot': 3, 'wally': 4, 'bamsnap': 5, 'genomeview': 6, 'samtools': 7}
df2['srt'] = [order[k] for k in df2['name']]
df2.sort_values(['srt', 'name'], inplace=True)
del df2['srt']

print(df2.round(3).to_markdown(index=False))
with open(f'benchmark.{tag}.md', 'w') as b:
    b.write(df2.round(3).to_markdown(index=False))


custom_params = {"axes.spines.right": False, "axes.spines.top": False}
sns.set_theme(style="ticks", rc=custom_params)

palette = sns.color_palette('tab10', n_colors=10)
colors = {}
for clr, name in zip(palette, ('gw', 'gw -t4', 'igv', 'igv -t4', 'jb2export', 'samplot', 'bamsnap', 'genomeview', 'samtools', 'samtools -t4')):
    colors[name] = clr

#
for item in ['total_time', 'relative_time', 'render', 'relative_render_time', 'total_mem', 'relative_mem']:
    sns.set_style('ticks')
    fig, ax = plt.subplots()
    # the size of A4 paper
    fig.set_size_inches(20, 8.27)
    g = sns.pointplot(data=df2,
                    x='region size (bp)', y=item, hue='name',
                    alpha=0.6, palette=colors, ax=ax)
                    # kind='point', alpha=0.6, palette=colors, ax=ax)
    # g.ax.set_xlabel("Region size (bp)", fontsize=14)
    # label = list(item.replace('_', ' '))
    # label[0] = label[0].upper()
    # g.ax.set_ylabel(''.join(label), fontsize=14)
    # g.ax.tick_params(labelsize=15)
    # g.ax.set_yscale('log')
    # g.set_xticklabels(rotation=30)
    plt.xticks(rotation=30)
    plt.subplots_adjust(left=0.15)
    plt.subplots_adjust(bottom=0.25)
    # plt.grid(True, which="major", ls="-", c='gray', alpha=0.2)
    # labels = [item.get_text() for item in g.ax.get_xticklabels()]
    # labels = ["{:,}".format(int(l)) for l in labels]
    # g.ax.set_xticklabels(labels)
    plt.savefig(f'plots/benchmark_{item}_log.pdf')
    plt.close()
# plt.show()


