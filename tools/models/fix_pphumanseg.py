#!/usr/bin/env python3
"""Finalises the PP-HumanSeg ONNX exports.

paddle2onnx exports the SPPM adaptive average pools (output 2x2 / 4x4) with an
unresolved kernel (-1) because the export has dynamic input dims, and appends an
ArgMax. This script:
  1. fixes the input shape,
  2. removes the ArgMax so the two-class softmax is the output,
  3. rewrites each adaptive AveragePool as an exact AveragePool(+Gather) for the
     now-known feature-map size,
  4. verifies the graph runs in ONNX Runtime.
"""
import sys

import numpy as np
import onnx
from onnx import helper, numpy_helper, shape_inference, TensorProto

# adaptive output sizes in ONNX node order (from the Paddle program: 2x2 then 4x4)
ADAPTIVE_OUTPUTS = [(2, 2), (4, 4)]


def adaptive_windows(n, out):
    starts, ends = [], []
    for i in range(out):
        starts.append((i * n) // out)
        ends.append(-(-((i + 1) * n) // out))  # ceil
    return starts, ends


def probe_shapes(m, adaptive, shape):
    import copy
    import onnxruntime as ort
    probe = copy.deepcopy(m)
    pg = probe.graph
    names = sorted({n.input[0] for n in adaptive})
    for n in list(pg.node):
        if n.name in {a.name for a in adaptive}:
            idx = list(pg.node).index(n)
            pg.node.remove(n)
            pg.node.insert(idx, helper.make_node("GlobalAveragePool", [n.input[0]], [n.output[0]], name=n.name + "_probe"))
    for t in names:
        pg.output.append(helper.make_tensor_value_info(t, TensorProto.FLOAT, None))
    so = ort.SessionOptions()
    so.log_severity_level = 3
    s = ort.InferenceSession(probe.SerializeToString(), so, providers=["CPUExecutionProvider"])
    outs = s.run(names, {s.get_inputs()[0].name: np.zeros(shape, np.float32)})
    return {t: list(o.shape) for t, o in zip(names, outs)}


def fix(path, shape):
    m = onnx.load(path)
    g = m.graph
    # 1. fixed input shape
    dims = g.input[0].type.tensor_type.shape.dim
    for i, v in enumerate(shape):
        dims[i].dim_value = v
        dims[i].dim_param = ""
    # 2. drop ArgMax
    for n in list(g.node):
        if n.op_type == "ArgMax":
            src = n.input[0]
            g.node.remove(n)
            del g.output[:]
            g.output.append(helper.make_tensor_value_info(src, TensorProto.FLOAT, [1, 2, shape[2], shape[3]]))
    # 3. adaptive pools: probe the feature-map size by running a copy of the graph
    #    in which the adaptive pools are temporarily global pools.
    adaptive = [n for n in g.node if n.op_type == "AveragePool"
                and any(a.name == "kernel_shape" and list(a.ints)[0] <= 0 for a in n.attribute)]
    shapes = probe_shapes(m, adaptive, shape)
    assert len(adaptive) == len(ADAPTIVE_OUTPUTS), f"unexpected adaptive pools: {len(adaptive)}"
    new_inits = []
    for node, (oh, ow) in zip(adaptive, ADAPTIVE_OUTPUTS):
        in_shape = shapes[node.input[0]]
        H, W = in_shape[2], in_shape[3]
        sh, eh = adaptive_windows(H, oh)
        sw, ew = adaptive_windows(W, ow)
        kh = eh[0] - sh[0]
        kw = ew[0] - sw[0]
        assert all(e - s == kh for s, e in zip(sh, eh)) and all(e - s == kw for s, e in zip(sw, ew)), \
            f"non-uniform adaptive windows for {H}x{W} -> {oh}x{ow}"
        idx = list(g.node).index(node)
        out_name = node.output[0]
        uniform_h = H % oh == 0 and sh == list(range(0, H, kh))[:oh]
        uniform_w = W % ow == 0 and sw == list(range(0, W, kw))[:ow]
        if uniform_h and uniform_w:
            pool = helper.make_node("AveragePool", [node.input[0]], [out_name], name=node.name + "_fixed",
                                    kernel_shape=[kh, kw], strides=[kh, kw])
            g.node.remove(node)
            g.node.insert(idx, pool)
            print(f"  {node.name}: {H}x{W} -> {oh}x{ow} as AveragePool k={kh}x{kw} s={kh}x{kw}")
        else:
            pool = helper.make_node("AveragePool", [node.input[0]], [out_name + "_dense"], name=node.name + "_dense",
                                    kernel_shape=[kh, kw], strides=[1, 1])
            ih = numpy_helper.from_array(np.array(sh, dtype=np.int64), out_name + "_rows")
            iw = numpy_helper.from_array(np.array(sw, dtype=np.int64), out_name + "_cols")
            new_inits += [ih, iw]
            gh = helper.make_node("Gather", [out_name + "_dense", ih.name], [out_name + "_rows_g"], axis=2)
            gw = helper.make_node("Gather", [out_name + "_rows_g", iw.name], [out_name], axis=3)
            g.node.remove(node)
            for k, nn in enumerate([pool, gh, gw]):
                g.node.insert(idx + k, nn)
            print(f"  {node.name}: {H}x{W} -> {oh}x{ow} as AveragePool k={kh}x{kw} s=1 + Gather rows {sh} cols {sw}")
    g.initializer.extend(new_inits)
    onnx.checker.check_model(m)
    onnx.save(m, path)
    # 4. verify
    import onnxruntime as ort
    so = ort.SessionOptions()
    so.log_severity_level = 3
    s = ort.InferenceSession(path, so, providers=["CPUExecutionProvider"])
    x = np.random.rand(*shape).astype(np.float32)
    out = s.run(None, {s.get_inputs()[0].name: x})[0]
    print(f"  verified: output {out.shape} range [{out.min():.3f}, {out.max():.3f}] class-sum {out.sum(axis=1).mean():.3f}")


if __name__ == "__main__":
    jobs = [("models/converted/pphumanseg_v2_lite_192.onnx", [1, 3, 192, 192]),
            ("models/converted/pphumanseg_v2_portrait_256x144.onnx", [1, 3, 144, 256])]
    if len(sys.argv) > 1:
        jobs = [(sys.argv[1], [int(v) for v in sys.argv[2].split(",")])]
    for p, shape in jobs:
        print(p)
        fix(p, shape)
