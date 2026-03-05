import functools

import numpy as np
from scipy import signal


def sos_prescale(sos):
    return sos, 1.0
    k = sos[0, :3][0]
    k = k ** 0.5
    # print(sos[0, :3])
    # if k < 0.0001:
    #     import pdb; pdb.set_trace()
    sos[0, :3] /= k
    return sos, k


def make_elliptic(Wn, order=10, rp=0.25, rs=60, high_pass=False):
    # Default
    if high_pass:
        btype = "high"
    else:
        btype = "low"
    sos = signal.ellip(order, rp, rs, Wn, analog=False, btype=btype, output='sos')
    return sos


def make_cheb1(Wn, order=10, rp=0.01, high_pass=False):
    if high_pass:
        btype = "high"
    else:
        btype = "low"
    sos = signal.cheby1(order, rp, Wn, analog=False, btype=btype, output='sos')
    return sos

def make_filters(filter_fn, high_pass):
    # Calculate 995 filters

    filters = []
    print("")
    N = 996
    for i in range(N):
        f = i / N + 0.002
        if (f > 1):
            f = 1 - 1e-12
        print(f"\ri: {i}, f: {f}", end="")
        sos = filter_fn(f, high_pass=high_pass)
        sos, k = sos_prescale(sos)
        flt = np.concatenate([sos.astype(np.float32).flatten(), [np.float32(k)]])
        filters.append(flt)

    filters = np.concatenate(filters)
    return filters.tobytes(order="C")



def main():
    # Default
    # filter_fn = functools.partial(make_elliptic)

    # New
    filter_fn = functools.partial(make_elliptic, rp=0.025)
    # filter_fn = functools.partial(make_cheb1, rp=0.03)


    hpf_filters = make_filters(filter_fn, True)
    lpf_filters = make_filters(filter_fn, False)

    with open("filters_hpf.data", "wb") as f:
        f.write(hpf_filters)

    with open("filters_lpf.data", "wb") as f:
        f.write(lpf_filters)

if __name__ == "__main__":
    main()
