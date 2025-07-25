# Copyright 2021 Tencent
# SPDX-License-Identifier: BSD-3-Clause

import numpy as np
import pytest

import ncnn


def test_net():
    dr = ncnn.DataReaderFromEmpty()

    with ncnn.Net() as net:
        ret = net.load_param("tests/test.param")
        net.load_model(dr)
        assert ret == 0 and len(net.blobs()) == 3 and len(net.layers()) == 3

        input_names = net.input_names()
        output_names = net.output_names()
        assert len(input_names) > 0 and len(output_names) > 0

        in_mat = ncnn.Mat((227, 227, 3))

        with net.create_extractor() as ex:
            ex.input("data", in_mat)
            ret, out_mat = ex.extract("output")

        assert ret == 0 and out_mat.dims == 1 and out_mat.w == 1

        net.clear()
        assert len(net.blobs()) == 0 and len(net.layers()) == 0


def test_net_mem():
    modelbin = bytearray(303940)
    modelbin[0:4] = 71,107,48,1
    modelbin[180:184] = 71,107,48,1

    with ncnn.Net() as net:
        ret = net.load_param("tests/test.param")
        net.load_model_mem(bytes(modelbin))
        assert ret == 0 and len(net.blobs()) == 3 and len(net.layers()) == 3

        input_names = net.input_names()
        output_names = net.output_names()
        assert len(input_names) > 0 and len(output_names) > 0

        in_mat = ncnn.Mat((227, 227, 3))

        with net.create_extractor() as ex:
            ex.input("data", in_mat)
            ret, out_mat = ex.extract("output")

        assert ret == 0 and out_mat.dims == 1 and out_mat.w == 1

        net.clear()
        assert len(net.blobs()) == 0 and len(net.layers()) == 0


def test_net_vulkan():
    if not hasattr(ncnn, "get_gpu_count"):
        return

    dr = ncnn.DataReaderFromEmpty()

    net = ncnn.Net()
    net.opt.use_vulkan_compute = True
    ret = net.load_param("tests/test.param")
    net.load_model(dr)
    assert ret == 0 and len(net.blobs()) == 3 and len(net.layers()) == 3

    in_mat = ncnn.Mat((227, 227, 3))

    ex = net.create_extractor()
    ex.input("data", in_mat)
    ret, out_mat = ex.extract("output")

    assert ret == 0 and out_mat.dims == 1 and out_mat.w == 1

    ex.clear()

    net.clear()
    assert len(net.blobs()) == 0 and len(net.layers()) == 0


def test_custom_layer():
    class CustomLayer(ncnn.Layer):
        customLayers = []

        def __init__(self):
            ncnn.Layer.__init__(self)
            self.one_blob_only = True

            self.customLayers.append(self)

        def forward(self, bottom_blob, top_blob, opt):
            x = np.array(bottom_blob)
            x += 1

            top_blob.clone_from(ncnn.Mat(x), opt.blob_allocator)
            if top_blob.empty():
                return -100

            return 0

    def CustomLayer_layer_creator():
        return CustomLayer()

    def CustomLayer_layer_destroyer(layer):
        for i in range(len(CustomLayer.customLayers)):
            if CustomLayer.customLayers[i] == layer:
                del CustomLayer.customLayers[i]
                break

    dr = ncnn.DataReaderFromEmpty()

    net = ncnn.Net()
    net.register_custom_layer(
        "CustomLayer", CustomLayer_layer_creator, CustomLayer_layer_destroyer
    )
    ret = net.load_param("tests/custom_layer.param")
    net.load_model(dr)
    assert ret == 0 and len(net.blobs()) == 2 and len(net.layers()) == 2

    in_mat = ncnn.Mat(1)
    in_mat.fill(1.0)

    ex = net.create_extractor()
    ex.input("data", in_mat)
    ret, out_mat = ex.extract("output")
    assert ret == 0 and out_mat.dims == 1 and out_mat.w == 1 and out_mat[0] == 2.0

    ex.clear()

    net.clear()
    assert len(net.blobs()) == 0 and len(net.layers()) == 0


def test_vulkan_device_index():
    if not hasattr(ncnn, "get_gpu_count"):
        return

    net = ncnn.Net()
    assert net.vulkan_device() is None

    net.set_vulkan_device(0)
    assert net.vulkan_device() is not None


def test_vulkan_device_vkdev():
    if not hasattr(ncnn, "get_gpu_count"):
        return

    net = ncnn.Net()
    assert net.vulkan_device() is None

    vkdev = ncnn.get_gpu_device(0)
    net.set_vulkan_device(vkdev)
    assert net.vulkan_device() is not None
