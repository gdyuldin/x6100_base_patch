import numpy as np


def main():
    a = np.linspace(0, 1, 8+1, endpoint=False)[1:]
    b = np.linspace(1, 0, 16+1, endpoint=False)[1:]
    cos_a = (np.cos(a * np.pi) + 1) / 2
    cos_b = (np.cos(b * np.pi) + 1) / 2
    for cos in cos_a, cos_b:
        cos = np.reshape(cos, (-1, 4))
        for row in cos:
            print(",".join(str(x) for x in row), ",", sep="")
        print("")
    # idx = np.arange(1000)
    # idx = np.roll(idx, -250).reshape(-1, 25)
    # for row in idx:
    #     print(",".join(str(x) for x in row), ",", sep="")


if __name__ == "__main__":
    main()
