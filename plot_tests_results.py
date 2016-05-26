import sys
import math
import yaml
import collections


def ns_to_readable(val):
    for limit, ext in ((1E9, ''), (1E6, 'm'), (1E3, 'u'), (1, 'n')):
        if val >= limit:
            return "{} {}s".format(int(val / limit), ext)


TestRun = collections.namedtuple("TestRun", ["func", "workers", "msize", "timeout", "server", "runtime"])


class RunData(object):
    def __init__(self, **params):
        self.__dict__.update(params)


def filter_results(data, **filter):
    for item in data:
        for name, value in filter.items():
            if getattr(item, name) != value:
                break
        else:
            yield item


AvgDev = collections.namedtuple("AvgDev", ['avg', 'dev'])


def average_and_dev(vals):
    avg = sum(vals) / len(vals)
    dev = (sum((val - avg) ** 2.0 for val in vals) / (len(vals) - 1)) ** 0.5
    return AvgDev(avg, dev)


def test_label(params):
    return params.func
    # return "{} {}".format(params.func, "local" if params.server == '172.16.40.43:33331' else "remote")


def stime_to_ns(data):
    if data.endswith('ns'):
        return float(data[:-2])
    elif data.endswith('us'):
        return float(data[:-2]) * 1000
    elif data.endswith('ms'):
        return float(data[:-2]) * 1000000
    elif data.endswith('s'):
        return float(data[:-1]) * 1000000000
    raise ValueError("Can't parse {0!r} as time".format(data))


def show_plot(points, data, scale=1, with_dev=True, log_scale_y=False, ylabel=None, xlabel=None):
    import matplotlib.pyplot as plt

    if 10000 not in points:
        points = list(points) + [10000]
    points = sorted(list(points))
    x_coords_all = list([math.log10(i) - 0.8 for i in points])
    pt2coord = {pt: x for pt, x in zip(points, x_coords_all)}
    x_coords_all.append(x_coords_all[-1] + 1)

    all_params = data.keys()
    all_params.sort(key=lambda x: x.func)

    plt.subplot(1, 1, 1)

    for params in all_params:
        wrk_to_avg = data[params]
        y = []
        y_dev = []
        x = []
        for pt in points:
            if pt in wrk_to_avg:
                avg, dev = wrk_to_avg[pt]
                y.append(avg / scale)
                if with_dev:
                    y_dev.append(dev / scale)
                x.append(pt2coord[pt])
        if with_dev:
            plt.errorbar(x, y, y_dev, label=test_label(params))
        else:
            plt.plot(x, y, label=test_label(params))

    ticks = []
    for i in points:
        if i < 1000:
            ticks.append(str(i))
        else:
            assert i % 1000 == 0
            ticks.append(str(i / 1000) + 'k')

    if log_scale_y:
        plt.yscale('log')

    plt.xticks(x_coords_all, ticks + [""])

    plt.xlim([0, x_coords_all[-1]])
    plt.ylim(ymin=0)

    if xlabel is not None:
        plt.xlabel(xlabel)

    if ylabel is not None:
        plt.ylabel(ylabel)

    plt.legend()
    plt.show()


def round_deviation(med_dev):
    med, dev = med_dev

    if dev < 1E-7:
        return med_dev

    dev_div = 10.0 ** (math.floor(math.log10(dev)) - 1)
    dev = int(dev / dev_div) * dev_div
    med = int(med / dev_div) * dev_div
    return AvgDev(type(med_dev[0])(med),
                  type(med_dev[1])(dev))


def make2digit_str(val):
    if isinstance(val, basestring):
        return val
    if val > 100000:
        return str(int(val / 10000) * 10) + 'k'
    elif val > 10000:
        return str(int(val / 1000)) + 'k'
    elif val > 1000:
        return "{:1.1f}k".format(float(val) / 1000)
    elif val > 100:
        return str(int(val / 10) * 10)
    else:
        return str(val)


def avg_dev_to_str(avg_dev):
    avg_dev = round_deviation(avg_dev)
    return "{:>5s} ~ {:>2d}%".format(make2digit_str(avg_dev.avg),
                                     int(avg_dev.dev * 2.5 * 100 / avg_dev.avg))


def show_table(points, data, with_dev=True):
    import texttable as TT
    table = TT.Texttable(max_width=120)
    table.set_deco(TT.Texttable.VLINES | TT.Texttable.HEADER | TT.Texttable.BORDER)

    points = sorted(points)
    table.header(["Test"] + [str(pt) if pt < 1000 else str(pt / 1000) + 'k'
                             for pt in points])
    table.set_cols_dtype(['t'] * (len(points) + 1))
    table.set_cols_align(['c'] * (len(points) + 1))

    all_params = data.keys()
    all_params.sort(key=lambda x: x.func)

    for params in all_params:
        wrk_to_avg = data[params]
        row = [params.func]
        for pt in points:
            if pt in wrk_to_avg:
                if with_dev:
                    row.append(avg_dev_to_str(wrk_to_avg[pt]))
                else:
                    row.append("{:>2s}".format(make2digit_str(wrk_to_avg[pt][0])))
            else:
                row.append("---")
        table.add_row(row)
    print table.draw()


def main(argv):
    func_names = ('uvloop', 'asyncio', 'gevent', 'thread', 'cpp_th', 'cpp_epoll', 'selector')
    # func_names = None
    server = '172.16.40.43:33331'
    # server = '172.16.40.37:33331'
    files = sys.argv[1:]

    results = collections.defaultdict(list)
    for fname in files:
        for block in yaml.load(open(fname)):
            test_run_params = dict(
                workers=block['workers'],
                msize=block['msize'],
                timeout=block['timeout'],
                server=block['server'],
                runtime=block['runtime']
            )

            for run in block['data']:
                test_run_params['func'] = run.pop('func')
                results[TestRun(**test_run_params)].append(RunData(**run))

    for_plot = {}
    for key, val in results.items():
        if func_names is not None and key.func not in func_names:
            continue
        if server is not None and key.server != server:
            continue
        for_plot[key] = val

    # all_tests = set(key[1] for key in agg.keys() if key[0])
    mps = collections.defaultdict(dict)
    lat_50 = collections.defaultdict(dict)
    lat_95 = collections.defaultdict(dict)
    lat_50_s = collections.defaultdict(dict)
    lat_95_s = collections.defaultdict(dict)
    stime = collections.defaultdict(dict)
    utime = collections.defaultdict(dict)
    points = set()

    for params, data in for_plot.items():
        dparams = dict(**params.__dict__)
        workers = dparams['workers']
        dparams['workers'] = None

        mps[TestRun(**dparams)][workers] = average_and_dev([i.messages / i.ctime for i in data])

        if len(data) == 1:
            raise ValueError("Test {} has only one results. Can't calculate stats".format(dparams))

        avg, dev = average_and_dev([stime_to_ns(i.lat_95) for i in data])
        if avg >= 1E9 - 1000:
            avg_s = ">1s"
        else:
            avg_s = ns_to_readable(avg)
        lat_95_s[TestRun(**dparams)][workers] = AvgDev(avg_s, None)
        lat_95[TestRun(**dparams)][workers] = AvgDev(avg / 1000000., dev / 1000000.)

        avg, dev = average_and_dev([stime_to_ns(i.lat_50) for i in data])
        if avg >= 1E9 - 1000:
            avg_s = ">1s"
        else:
            avg_s = ns_to_readable(avg)
        lat_50_s[TestRun(**dparams)][workers] = AvgDev(avg_s, None)
        lat_50[TestRun(**dparams)][workers] = AvgDev(avg / 1000000., dev / 1000000.)

        stime[TestRun(**dparams)][workers] = average_and_dev([int(i.stime * 100 / params.runtime + 0.5) for i in data])
        utime[TestRun(**dparams)][workers] = average_and_dev([int(i.utime * 100 / params.runtime + 0.5) for i in data])

        points.add(workers)

    min_mps = min(min(i.avg for i in per_worker_map.values())
                  for per_worker_map in mps.values())

    rel_mps_s = collections.defaultdict(dict)
    rel_mps = collections.defaultdict(dict)
    for key1, val1 in mps.items():
        for key2, val2 in val1.items():
            vl = int(val2.avg / min_mps + 0.5)
            rel_mps_s[key1][key2] = AvgDev("{:>2d}".format(vl), None)
            rel_mps[key1][key2] = AvgDev(vl, None)

    # show_plot(points, mps, 1000)
    # show_table(points, mps, with_dev=True)
    # show_table(points, lat_95_s, with_dev=False)
    # show_table(points, lat_50_s, with_dev=False)
    show_plot(points, lat_95, with_dev=False,
              log_scale_y=True, ylabel="lat, ms", xlabel="conn. count")

    # show_table(points, utime, with_dev=False)


if __name__ == "__main__":
    exit(main(sys.argv))
