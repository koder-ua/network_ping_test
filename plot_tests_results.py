import sys
import yaml
import functools
import collections

try:
    # import numpy
    # import scipy
    import matplotlib
    # matplotlib.use('svg')
    import matplotlib.pyplot as plt
except ImportError:
    plt = None


class TestRun(object):
    def __init__(self, bsize, timeout, is_local, workers, test_name, run_data):
        self.bsize = bsize
        self.timeout = timeout
        self.is_local = is_local
        self.workers = workers
        self.test_name = test_name
        self.run_data = run_data


class RunData(object):
    def __init__(self, messages, utime, stime, ctime):
        self.messages = messages
        self.ctime = ctime
        self.stime = stime
        self.utime = utime


def get_point_data(data, **filter):
    for item in data:
        for name, value in filter.items():
            if getattr(item, name) != value:
                break
        else:
            yield item


def average(vals):
    return sum(vals) / len(vals)


def aggregate(data):
    # (is_local, test_name) => {workers: [RunData]}
    aggregated = collections.defaultdict(functools.partial(collections.defaultdict, list))
    for item in data:
        first_key = (item.is_local, item.test_name)
        aggregated[first_key][item.workers].append(item.run_data)
    return aggregated


def main(argv):
    fname = argv[1]
    results = []
    for block in yaml.load(open(fname)):
        meta = block.copy()
        meta['is_local'] = (meta.pop('server').lower() == 'local')
        del meta['data']
        tr_templ = TestRun(run_data=None, test_name=None, **meta)
        for run in block['data']:
            tr = TestRun(**tr_templ.__dict__)
            run = run.copy()
            tr.test_name = run.pop('func')
            tr.run_data = RunData(**run)
            results.append(tr)

    agg = aggregate(results)

    # pdata = list(get_point_data(results, is_local=True, workers=100))
    # print(len(pdata))

    all_tests = set(key[1] for key in agg.keys() if key[0])
    mps = {}
    points = set()

    for name in all_tests:
        mps[name] = {wrk: int(average([i.messages / i.ctime / 100 for i in itm]))
                     for wrk, itm in agg[(True, name)].items()}
        points.update(mps[name].keys())

    points = sorted(points)
    x_coords_all = list(range(len(points) + 2))
    x_coords_data = x_coords_all[1:-1]

    for name, data in mps.items():
        plt.plot(x_coords_data, [data[pt] for pt in points], label=name)

    plt.xticks(x_coords_all, [""] + list(map(str, points)) + [""])
    plt.ylim([0, 4000])
    plt.legend()
    plt.show()


if __name__ == "__main__":
    exit(main(sys.argv))
