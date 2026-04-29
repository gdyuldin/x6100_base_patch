import numpy as np


def main():
    a = np.linspace(0, 1, 1000, endpoint=False)
    sin = np.sin(a * 2 * np.pi)
    sin = np.concatenate([sin, sin])[:1250]
    sin = sin.reshape(-1, 5)
    for row in sin:
        print(",".join(str(x) for x in row), ",", sep="")
    print("")
    # idx = np.arange(1000)
    # idx = np.roll(idx, -250).reshape(-1, 25)
    # for row in idx:
    #     print(",".join(str(x) for x in row), ",", sep="")


if __name__ == "__main__":
    main()
