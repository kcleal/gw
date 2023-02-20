import pandas as pd
import seaborn as sns
import glob
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import sys
pd.options.mode.chained_assignment = None

folder = sys.argv[1]
tables = glob.glob(folder + '/*.csv')
dfs = []
for p in tables:
    d = pd.read_csv(p)
    d['file'] = p
    dfs.append(d)
df = pd.concat(dfs)
df['RSS'] = df['RSS'] / 1e6
print(df.columns)
gw_times = {}
gw_mem = {}
samtools = {}
for idx, grp in df[df['name'] == 'gw'].groupby('region size (bp)'):
    gw_times[idx] = grp['time (s)'].mean()
    gw_mem[idx] = grp['RSS'].mean()
for idx, grp in df.groupby('region size (bp)'):
    samtools[idx] = grp['samtools_count_t3 (s)'].mean()

print(gw_times)
# use the mean time of 2bp region as start time
min_load_time = {k: dd['time (s)'].min() for k, dd in df[df['region size (bp)'] == 2].groupby('name')}
df['total'] = df['time (s)']
df['start'] = [min_load_time[k] for k in df['name']]
df = df[df['region size (bp)'] != 2]

su = []
for idx, grp in df.groupby(['name', 'region size (bp)']):
    su.append({'name': idx[0],
               'region_size': idx[1],
               'samtools': samtools[idx[1]],
               'total': grp['total'].mean(),
               'start': grp['start'].mean(),
               'render': grp['total'].mean() - grp['start'].mean(),
               'RSS': grp['RSS'].mean(),
               'relative_time': grp['time (s)'].mean() / gw_times[idx[1]],
               "relative_RSS": grp['RSS'].mean() / gw_mem[idx[1]]})

df2 = pd.DataFrame.from_records(su).round(3)
gw_render_times = {k: t for k, t in zip(df2[df2['name'] == 'gw']['region_size'],
                                        df2[df2['name'] == 'gw']['render'])}
df2['relative_render_time'] = [k / gw_render_times[s] for k, s in zip(df2['render'], df2['region_size'])]
df2 = df2[['name', 'region_size', 'samtools', 'total', 'relative_time',
           'start', 'render', 'relative_render_time', 'RSS', 'relative_RSS']]
print(df2.to_markdown())
with open('results/benchmark.md', 'w') as b:
    b.write(df2.to_markdown())


fig, axes = plt.subplots(nrows=4, ncols=1, figsize=(7, 6))
fig.subplots_adjust(bottom=0.15, hspace=0.8)

idx = 0
for size, grp in df.groupby('region size (bp)'):
    ax = sns.barplot(y='name', x='total', data=grp,
                     color='royalblue', ax=axes[idx], errorbar=('ci', 95), linewidth=0, errwidth=1)
    ax2 = sns.barplot(y='name', x='start', data=grp,
                      color='lightsteelblue', ax=axes[idx], errorbar=('ci', 95), linewidth=0, errwidth=1)

    sns.despine(left=True, bottom=True)
    ax.grid(axis='x')
    if size == 2000:
        ax.set_title('2 kb')
    elif size == 20_000:
        ax.set_title('20 kb')
    elif size == 200_000:
        ax.set_title('200 kb')
    elif size == 2_000_000:
        ax.set_title('2 Mb')
    if idx == 3:
        ax.set_xlabel('Time (s)')
    else:
        ax.set_xlabel('')
    ax.set_ylabel('')
    idx += 1

b_patch = mpatches.Patch(color='royalblue', label='Total')
b2_patch = mpatches.Patch(color='lightsteelblue', label='Start')
plt.legend(loc='lower center', ncol=2, handles=[b_patch, b2_patch], bbox_to_anchor=(0.5, -1.2),
           prop={'size': 10})
plt.savefig('time.png')
# plt.show()
# plt.close()


fig, axes = plt.subplots(nrows=4, ncols=1, figsize=(7, 6))
fig.subplots_adjust(bottom=0.15, hspace=0.8)
idx = 0
for size, grp in df.groupby('region size (bp)'):
    ax = sns.barplot(y='name', x='RSS', data=grp,
                     color='rosybrown', ax=axes[idx], errorbar=('ci', 95), linewidth=0, errwidth=1)
    ax2 = sns.barplot(y='name', x='RSS', data=grp,
                     color='rosybrown', ax=axes[idx], errorbar=('ci', 95), linewidth=0, errwidth=1)
    sns.despine(left=True, bottom=True)
    ax.grid(axis='x')
    if size == 2000:
        ax.set_title('2 kb')
    elif size == 20_000:
        ax.set_title('20 kb')
    elif size == 200_000:
        ax.set_title('200 kb')
    elif size == 2_000_000:
        ax.set_title('2 Mb')
    if idx == 3:
        ax.set_xlabel('Resident set size (GB)')
    else:
        ax.set_xlabel('')
    ax.set_ylabel('')
    idx += 1

plt.savefig('memory.png')
plt.show()
plt.close()
