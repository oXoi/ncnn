// Copyright 2021 Tencent
// SPDX-License-Identifier: BSD-3-Clause

#include "pass_ncnn.h"

namespace pnnx {

namespace ncnn {

class nn_GroupNorm : public GraphRewriterPass
{
public:
    const char* match_pattern_graph() const
    {
        return R"PNNXIR(7767517
3 2
pnnx.Input              input       0 1 input
nn.GroupNorm            op_0        1 1 input out num_groups=%num_groups num_channels=%num_channels eps=%eps affine=%affine @weight @bias
pnnx.Output             output      1 0 out
)PNNXIR";
    }

    const char* type_str() const
    {
        return "GroupNorm";
    }

    const char* name_str() const
    {
        return "gn";
    }

    void write(Operator* op, const std::map<std::string, Parameter>& captured_params, const std::map<std::string, Attribute>& captured_attrs) const
    {
        op->params["0"] = captured_params.at("num_groups");
        op->params["1"] = captured_params.at("num_channels");
        op->params["2"] = captured_params.at("eps");
        op->params["3"] = captured_params.at("affine").b ? 1 : 0;

        if (captured_params.at("affine").b)
        {
            op->attrs["0"] = captured_attrs.at("op_0.weight");
            op->attrs["1"] = captured_attrs.at("op_0.bias");
        }
    }
};

REGISTER_GLOBAL_PNNX_NCNN_GRAPH_REWRITER_PASS(nn_GroupNorm, 20)

} // namespace ncnn

} // namespace pnnx
