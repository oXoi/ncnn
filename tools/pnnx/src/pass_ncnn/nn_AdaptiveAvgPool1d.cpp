// Copyright 2021 Tencent
// SPDX-License-Identifier: BSD-3-Clause

#include "pass_ncnn.h"

namespace pnnx {

namespace ncnn {

class nn_AdaptiveAvgPool1d : public GraphRewriterPass
{
public:
    const char* match_pattern_graph() const
    {
        return R"PNNXIR(7767517
3 2
pnnx.Input              input       0 1 input
nn.AdaptiveAvgPool1d    op_0        1 1 input out output_size=1
pnnx.Output             output      1 0 out
)PNNXIR";
    }

    const char* type_str() const
    {
        return "Pooling1D";
    }

    const char* name_str() const
    {
        return "gap";
    }

    void write(Operator* op, const std::map<std::string, Parameter>& /*captured_params*/) const
    {
        op->params["0"] = 1;
        op->params["4"] = 1;
    }
};

REGISTER_GLOBAL_PNNX_NCNN_GRAPH_REWRITER_PASS(nn_AdaptiveAvgPool1d, 20)

class nn_AdaptiveAvgPool1d_n : public GraphRewriterPass
{
public:
    const char* match_pattern_graph() const
    {
        return R"PNNXIR(7767517
3 2
pnnx.Input              input       0 1 input
nn.AdaptiveAvgPool1d    op_0        1 1 input out output_size=%output_size
pnnx.Output             output      1 0 out
)PNNXIR";
    }

    const char* type_str() const
    {
        return "Pooling1D";
    }

    const char* name_str() const
    {
        return "aap";
    }

    void write(Operator* op, const std::map<std::string, Parameter>& captured_params) const
    {
        op->params["0"] = 1;
        op->params["7"] = 1;
        op->params["8"] = captured_params.at("output_size").ai[0];
    }
};

REGISTER_GLOBAL_PNNX_NCNN_GRAPH_REWRITER_PASS(nn_AdaptiveAvgPool1d_n, 21)

} // namespace ncnn

} // namespace pnnx
