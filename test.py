"""
Rudimentary serialpacker test
"""
import sys

from serialpacker import SerialPacker as S


def test():
    """rudimentary test"""
    a = S(max_idle=9999999999, max_packet=300)

    hello = (" ".join(sys.argv[1:]) or "HiThere").encode("utf-8")
    h, data, t = a.frame(hello)
    data = h + data + t
    if len(data) > 15:
        print(len(hello), data[:5], "…", data[-5:])
    else:
        print(len(hello), data)
    for j in range(3):
        for i, b in enumerate(data):
            x = a.feed(b)
            if x is not None and i != len(data) - 1:
                raise RuntimeError("returned %r, pos %d" % (x, i))
        if x is None:
            raise RuntimeError("returned None %d" % (j,))
        if x != hello:
            raise RuntimeError("returned %r instead of %r" % (x, hello))


if __name__ == "__main__":
    test()
