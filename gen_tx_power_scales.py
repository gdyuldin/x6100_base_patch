import argparse
import numpy as np

def main(base_val: float):
    vals = []
    for tx_p_int in range(1, 101):
        tx_p = tx_p_int / 10
        val = base_val * (tx_p / 10) ** 0.5
        vals.append(np.float32(val))
        # print(tx_p_int)
    step = 4
    for i in range(len(vals) // step):
        for j in range(step):
            print(f"{vals[i * step + j]},", end=" ")
        print()
    # print(len(vals))

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("base_val", type=float)
    args = parser.parse_args()
    main(args.base_val)
