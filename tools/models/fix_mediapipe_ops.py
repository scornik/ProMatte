#!/usr/bin/env python3
"""Replaces the MediaPipe custom op Convolution2DTransposeBias (left as a placeholder by
tf2onnx) with a standard ONNX ConvTranspose so the graph runs in ONNX Runtime."""
import sys
import numpy as np
import onnx
from onnx import helper, numpy_helper


def fix(path):
    m = onnx.load(path)
    g = m.graph
    inits = {i.name: i for i in g.initializer}
    prod = {o: n for n in g.node for o in n.output}
    changed = False
    for n in list(g.node):
        if n.op_type != "TFL_Convolution2DTransposeBias":
            continue
        x, w_name, b_name = n.input
        w = numpy_helper.to_array(inits[w_name])  # OHWI
        b = numpy_helper.to_array(inits[b_name])
        w_iohw = np.ascontiguousarray(w.transpose(3, 0, 1, 2))  # -> (I, O, kH, kW)
        g.initializer.remove(inits[w_name])
        g.initializer.append(numpy_helper.from_array(w_iohw, w_name + "_iohw"))
        # tf2onnx inserted an NCHW->NHWC transpose in front of the custom op; bypass it.
        src = x
        p = prod.get(x)
        if p is not None and p.op_type == "Transpose":
            src = p.input[0]
            g.node.remove(p)
        kh, kw = w.shape[1], w.shape[2]
        ct = helper.make_node("ConvTranspose", [src, w_name + "_iohw", b_name], [n.output[0] + "_nchw"],
                              name=n.name + "_convT", kernel_shape=[kh, kw], strides=[kh, kw], pads=[0, 0, 0, 0])
        tr = helper.make_node("Transpose", [n.output[0] + "_nchw"], [n.output[0]], name=n.name + "_nhwc",
                              perm=[0, 2, 3, 1])
        idx = list(g.node).index(n)
        g.node.remove(n)
        g.node.insert(idx, tr)
        g.node.insert(idx, ct)
        changed = True
        print(f"{path}: replaced {n.name} with ConvTranspose k={kh}x{kw}")
    if changed:
        onnx.checker.check_model(m)
        onnx.save(m, path)
    return changed


if __name__ == "__main__":
    for p in sys.argv[1:]:
        fix(p)
