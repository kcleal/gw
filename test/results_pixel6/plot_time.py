"""
Arguments
---------
glob_pattern
tag

Usage
-----

python3 plot_time.py '*1.benchmark.csv' threads_1
"""
import pandas as pd
import glob
import sys
pd.options.mode.chained_assignment = None

glob_pattern = sys.argv[1]
tag = sys.argv[2]
tables = glob.glob(glob_pattern)
dfs = []
for p in tables:
    d = pd.read_csv(p)
    d['file'] = p
    dfs.append(d)
df = pd.concat(dfs)

gw_times = {}
for idx, grp in df[df['name'] == 'gw'].groupby('region size (bp)'):
    gw_times[idx] = grp['time (s)'].mean()

# use the mean time of 2bp region as start_time
min_load_time = {k: dd['time (s)'].min() for k, dd in df[df['region size (bp)'] == 2].groupby('name')}
df['total_time'] = df['time (s)']
df['start_time'] = [min_load_time[k] for k in df['name']]
df = df[df['region size (bp)'] != 2]
su = []
for idx, grp in df.groupby(['name', 'region size (bp)']):
    su.append({'name': idx[0],
               'region_size': idx[1],
               'total_time': grp['total_time'].mean(),
               'start_time': grp['start_time'].mean(),
               'render': grp['total_time'].mean() - grp['start_time'].mean(),
               })

df2 = pd.DataFrame.from_records(su).round(3)
df2 = df2[['name', 'region_size', 'total_time', 'start_time', 'render']]
print(df2.to_markdown())
with open(f'benchmark.{tag}.md', 'w') as b:
    b.write(df2.to_markdown())
