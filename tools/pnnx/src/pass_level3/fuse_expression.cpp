// Copyright 2021 Tencent
// SPDX-License-Identifier: BSD-3-Clause

#include "fuse_expression.h"

#include <algorithm>

#include "storezip.h"

namespace pnnx {

static bool operand_maybe_shape_tensor(const Operand* operand)
{
    const Operator* op = operand->producer;

    if (op->type == "aten::size")
    {
        return op->inputs.size() == 1;
    }

    if (op->type == "Tensor.to")
    {
        return operand_maybe_shape_tensor(op->inputs[0]);
    }

    return false;
}

static bool operand_maybe_tensor(const Operand* operand)
{
    const Operator* op = operand->producer;

    if (op->type == "prim::Constant")
    {
        const Parameter& param = op->params.at("value");
        if (param.type == 0 || param.type == 1 || param.type == 2 || param.type == 3 || param.type == 4 || param.type == 5 || param.type == 6 || param.type == 7 || param.type == 10)
        {
            return false;
        }
        else
        {
            return true;
        }
    }

    if (op->type == "prim::NumToTensor")
    {
        return operand_maybe_tensor(op->inputs[0]);
    }

    if (op->type == "prim::ListConstruct")
    {
        return false;
    }

    if (op->type == "torch.unbind" && op->inputs[0]->shape.size() == 1)
    {
        return false;
    }

    if (op->type == "aten::size")
    {
        return op->inputs.size() == 1;
    }

    if (op->type == "Tensor.size")
    {
        return !op->has_param("dim");
    }

    if (op->type == "Tensor.slice")
    {
        // static slice
        const size_t inputs_size = op->inputs.size();
        if (inputs_size != 3 && inputs_size != 4 && inputs_size != 5)
            return true;

        for (size_t i = 0; i < inputs_size; i++)
        {
            if (op->inputs[i]->producer->type != "prim::Constant")
                return true;

            if (op->inputs[i]->producer->params.at("value").type != 2)
                return true;
        }

        // dim=0
        if (inputs_size == 3 && op->params.at("dim").i != 0)
            return true;
        if ((inputs_size == 4 || inputs_size == 5) && op->inputs[0]->producer->params.at("value").i != 1)
            return true;

        // step=1
        if ((inputs_size == 3 || inputs_size == 4) && op->params.at("step").i != 1)
            return true;
        if (inputs_size == 5 && op->inputs[4]->producer->params.at("value").i != 1)
            return true;

        return !operand_maybe_shape_tensor(op->inputs[0]);
    }

    if (op->type == "aten::Int")
    {
        return operand_maybe_tensor(op->inputs[0]);
    }

    if (op->type == "Tensor.to" || op->type == "aten::detach")
    {
        return operand_maybe_tensor(op->inputs[0]);
    }

    if (op->type == "aten::ScalarImplicit")
    {
        return false;
    }

    if (op->type == "aten::abs"
            || op->type == "aten::acos"
            || op->type == "aten::acosh"
            || op->type == "aten::asin"
            || op->type == "aten::asinh"
            || op->type == "aten::atan"
            || op->type == "aten::atanh"
            || op->type == "aten::ceil"
            || op->type == "aten::cos"
            || op->type == "aten::cosh"
            || op->type == "aten::erf"
            || op->type == "aten::exp"
            || op->type == "aten::floor"
            || op->type == "aten::log"
            || op->type == "aten::log10"
            || op->type == "aten::neg"
            || op->type == "aten::reciprocal"
            || op->type == "aten::round"
            || op->type == "aten::rsqrt"
            || op->type == "aten::sign"
            || op->type == "aten::sin"
            || op->type == "aten::sinh"
            || op->type == "aten::sqrt"
            || op->type == "aten::square"
            || op->type == "aten::tan"
            || op->type == "aten::tanh"
            || op->type == "aten::trunc")
    {
        return operand_maybe_tensor(op->inputs[0]);
    }

    if (op->type == "aten::atan2"
            || op->type == "aten::div"
            || op->type == "aten::floor_divide"
            || op->type == "aten::fmod"
            || op->type == "aten::logaddexp"
            || op->type == "aten::max"
            || op->type == "aten::maximum"
            || op->type == "aten::min"
            || op->type == "aten::minimum"
            || op->type == "aten::mul"
            || op->type == "aten::pow"
            || op->type == "aten::remainder")
    {
        return operand_maybe_tensor(op->inputs[0]) || operand_maybe_tensor(op->inputs[1]);
    }

    if (op->type == "aten::__and__" || op->type == "aten::__or__" || op->type == "aten::__xor__" || op->type == "aten::__lshift__" || op->type == "aten::__rshift__")
    {
        return operand_maybe_tensor(op->inputs[0]) || operand_maybe_tensor(op->inputs[1]);
    }

    if (op->type == "aten::add" || op->type == "aten::sub" || op->type == "aten::rsub")
    {
        if (op->inputs.size() == 2)
            return operand_maybe_tensor(op->inputs[0]) || operand_maybe_tensor(op->inputs[1]);
        else // if (op->inputs.size() == 3)
            return operand_maybe_tensor(op->inputs[0]) || operand_maybe_tensor(op->inputs[1]) || operand_maybe_tensor(op->inputs[2]);
    }

    return true;
}

static void fuse_expression(Graph& graph, Operand* operand, std::string& expr, std::vector<Operand*>& inputs, const std::set<std::string>& foldable_constants, StoreZipReader& zip, bool checksubgraph = true)
{
    Operator* op = operand->producer;

    // fprintf(stderr, "fuse_expression %s %s\n", op->type.c_str(), operand->name.c_str());

    if (checksubgraph && operand_maybe_tensor(operand))
    {
        if (op->outputs.size() > 1 || op->outputs[0]->consumers.size() > 1)
        {
            goto DEFAULT;
        }
    }

    if (op->type == "prim::Constant")
    {
        const Parameter& param = op->params["value"];
        //         fprintf(stderr, "fuse_expression prim::Constant %d\n", param.type);
        if (param.type == 0)
        {
            expr += "None";
        }
        else if (param.type == 1)
        {
            expr += param.b ? "True" : "False";
        }
        else if (param.type == 2)
        {
            char tmp[32];
            sprintf(tmp, "%d", param.i);
            expr += tmp;
        }
        else if (param.type == 3)
        {
            char tmp[32];
            sprintf(tmp, "%e", param.f);
            expr += tmp;
        }
        else if (param.type == 4)
        {
            expr += param.s;
        }
        else if (param.type == 5)
        {
            // ints
            expr += "[";
            for (int i = 0; i < (int)param.ai.size(); i++)
            {
                char tmp[32];
                sprintf(tmp, "%d", param.ai[i]);
                expr += tmp;
                if (i != (int)param.ai.size() - 1)
                    expr += ",";
            }
            expr += "]";
        }
        else if (param.type == 6)
        {
            // floats
            expr += "[";
            for (int i = 0; i < (int)param.af.size(); i++)
            {
                char tmp[32];
                sprintf(tmp, "%e", param.af[i]);
                expr += tmp;
                if (i != (int)param.af.size() - 1)
                    expr += ",";
            }
            expr += "]";
        }
        else if (param.type == 10)
        {
            char tmp[32];
            sprintf(tmp, "%e%+ej", param.c.real(), param.c.imag());
            expr += tmp;
        }
        else
        {
            goto DEFAULT;
        }
    }
    else if (op->type == "pnnx.Attribute")
    {
        // fprintf(stderr, "operand pnnx.Attribute %s\n", operand->name.c_str());

        const Attribute& data = op->attrs["data"];
        if (data.shape.size() == 1 && data.shape[0] == 1 && data.type != -1)
        {
            if (data.type == 0)
            {
                expr += "None";
            }
            else if (data.type == 1)
            {
                char tmp[32];
                sprintf(tmp, "%e", ((const float*)data.data.data())[0]);
                expr += tmp;
            }
            else if (data.type == 2)
            {
                char tmp[32];
                sprintf(tmp, "%e", ((const double*)data.data.data())[0]);
                expr += tmp;
            }
            else if (data.type == 4)
            {
                char tmp[32];
                sprintf(tmp, "%d", ((const int*)data.data.data())[0]);
                expr += tmp;
            }
            else if (data.type == 5)
            {
                int64_t v = ((const int64_t*)data.data.data())[0];
                if (v == std::numeric_limits<int64_t>::max()) v = INT_MAX;
                if (v == std::numeric_limits<int64_t>::min()) v = INT_MIN;

                char tmp[32];
                sprintf(tmp, "%d", (int)v);
                expr += tmp;
            }
            else if (data.type == 6)
            {
                char tmp[32];
                sprintf(tmp, "%d", ((const short*)data.data.data())[0]);
                expr += tmp;
            }
            else if (data.type == 7)
            {
                char tmp[32];
                sprintf(tmp, "%d", ((const signed char*)data.data.data())[0]);
                expr += tmp;
            }
            else if (data.type == 8)
            {
                char tmp[32];
                sprintf(tmp, "%u", ((const unsigned char*)data.data.data())[0]);
                expr += tmp;
            }
            else if (data.type == 9)
            {
                expr += ((const char*)data.data.data())[0] ? "True" : "False";
            }
            else
            {
                // unsupported type
                fprintf(stderr, "fuse expression got unsupported scalar type %d\n", data.type);
            }
        }
        else
        {
            goto DEFAULT;
        }
    }
    else if (op->type == "torch.unbind")
    {
        // track chain
        // pnnx.Attribute/foldable with 1-rank
        // torch.unbind to constant scalar
        Operand* operand2 = op->inputs[0];
        if (operand2->producer->type == "pnnx.Attribute")
        {
            const Attribute& data = operand2->producer->attrs["data"];

            if (data.shape.size() == 1 && data.type != -1)
            {
                // resolve scalar i
                int si = 0;
                for (size_t i = 0; i < op->outputs.size(); i++)
                {
                    if (op->outputs[i] == operand)
                    {
                        si = (int)i;
                        break;
                    }
                }

                if (data.type == 0)
                {
                    expr += "None";
                }
                else if (data.type == 1)
                {
                    char tmp[32];
                    sprintf(tmp, "%e", ((const float*)data.data.data())[si]);
                    expr += tmp;
                }
                else if (data.type == 2)
                {
                    char tmp[32];
                    sprintf(tmp, "%e", ((const double*)data.data.data())[si]);
                    expr += tmp;
                }
                else if (data.type == 4)
                {
                    char tmp[32];
                    sprintf(tmp, "%d", ((const int*)data.data.data())[si]);
                    expr += tmp;
                }
                else if (data.type == 5)
                {
                    int64_t v = ((const int64_t*)data.data.data())[si];
                    if (v == std::numeric_limits<int64_t>::max()) v = INT_MAX;
                    if (v == std::numeric_limits<int64_t>::min()) v = INT_MIN;

                    char tmp[32];
                    sprintf(tmp, "%d", (int)v);
                    expr += tmp;
                }
                else if (data.type == 6)
                {
                    char tmp[32];
                    sprintf(tmp, "%d", ((const short*)data.data.data())[si]);
                    expr += tmp;
                }
                else if (data.type == 7)
                {
                    char tmp[32];
                    sprintf(tmp, "%d", ((const signed char*)data.data.data())[si]);
                    expr += tmp;
                }
                else if (data.type == 8)
                {
                    char tmp[32];
                    sprintf(tmp, "%u", ((const unsigned char*)data.data.data())[si]);
                    expr += tmp;
                }
                else if (data.type == 9)
                {
                    expr += ((const char*)data.data.data())[si] ? "True" : "False";
                }
                else
                {
                    // unsupported type
                    fprintf(stderr, "fuse expression got unsupported scalar type %d\n", data.type);
                    goto DEFAULT;
                }
                return;
            }
        }

        goto DEFAULT;
    }
    else if (checksubgraph && operand_maybe_tensor(operand) && foldable_constants.find(operand->name) != foldable_constants.end())
    {
        // fprintf(stderr, "operand_is_foldable %s\n", operand->name.c_str());

        if (operand->shape.size() == 0 && operand->type != -1)
        {
            // fuse literal constant into expression
            if (operand->type == 0)
            {
                expr += "None";
            }
            else if (operand->type == 1)
            {
                float v;
                zip.read_file(operand->name, (char*)&v);

                char tmp[32];
                sprintf(tmp, "%e", v);
                expr += tmp;
            }
            else if (operand->type == 2)
            {
                double v;
                zip.read_file(operand->name, (char*)&v);

                char tmp[32];
                sprintf(tmp, "%e", v);
                expr += tmp;
            }
            else if (operand->type == 4)
            {
                int v;
                zip.read_file(operand->name, (char*)&v);

                char tmp[32];
                sprintf(tmp, "%d", v);
                expr += tmp;
            }
            else if (operand->type == 5)
            {
                int64_t v;
                zip.read_file(operand->name, (char*)&v);

                if (v == std::numeric_limits<int64_t>::max()) v = INT_MAX;
                if (v == std::numeric_limits<int64_t>::min()) v = INT_MIN;

                char tmp[32];
                sprintf(tmp, "%ld", v);
                expr += tmp;
            }
            else if (operand->type == 6)
            {
                short v;
                zip.read_file(operand->name, (char*)&v);

                char tmp[32];
                sprintf(tmp, "%d", v);
                expr += tmp;
            }
            else if (operand->type == 7)
            {
                signed char v;
                zip.read_file(operand->name, (char*)&v);

                char tmp[32];
                sprintf(tmp, "%d", v);
                expr += tmp;
            }
            else if (operand->type == 8)
            {
                unsigned char v;
                zip.read_file(operand->name, (char*)&v);

                char tmp[32];
                sprintf(tmp, "%u", v);
                expr += tmp;
            }
            else if (operand->type == 9)
            {
                char v;
                zip.read_file(operand->name, &v);

                expr += v ? "True" : "False";
            }
            else
            {
                // fprintf(stderr, "unknown foldable literal %s %d\n", operand->name.c_str(), operand->type);
                auto it = std::find(inputs.begin(), inputs.end(), operand);
                if (it == inputs.end())
                {
                    // tensor
                    char tmp[32];
                    sprintf(tmp, "@%d", (int)inputs.size());
                    expr += tmp;

                    inputs.push_back(operand);
                }
                else
                {
                    // tensor
                    char tmp[32];
                    sprintf(tmp, "@%d", (int)(it - inputs.begin()));
                    expr += tmp;
                }
            }
        }
        else
        {
            goto DEFAULT;
        }
    }
    else if (op->type == "prim::NumToTensor")
    {
        fuse_expression(graph, op->inputs[0], expr, inputs, foldable_constants, zip);
    }
    else if (op->type == "prim::ListConstruct")
    {
        expr += "[";
        for (int i = 0; i < (int)op->inputs.size() - 1; i++)
        {
            fuse_expression(graph, op->inputs[i], expr, inputs, foldable_constants, zip);
            expr += ",";
        }
        if (op->inputs.size() > 0)
        {
            fuse_expression(graph, op->inputs[op->inputs.size() - 1], expr, inputs, foldable_constants, zip);
        }
        expr += "]";
    }
    else if (op->type == "aten::size")
    {
        if (op->inputs.size() == 1)
        {
            fuse_expression(graph, op->inputs[0], expr, inputs, foldable_constants, zip);
        }
        else // if (op->inputs.size() == 2)
        {
            expr += "size(";
            fuse_expression(graph, op->inputs[0], expr, inputs, foldable_constants, zip);
            expr += ",";
            fuse_expression(graph, op->inputs[1], expr, inputs, foldable_constants, zip);
            expr += ")";
        }
    }
    else if (op->type == "Tensor.size")
    {
        if (op->has_param("dim") && op->params.at("dim").type == 2)
        {
            const int dim = op->params.at("dim").i;
            expr += "size(";
            fuse_expression(graph, op->inputs[0], expr, inputs, foldable_constants, zip);
            expr += ",";
            expr += std::to_string(dim);
            expr += ")";
        }
        else
        {
            fuse_expression(graph, op->inputs[0], expr, inputs, foldable_constants, zip);
        }
    }
    else if (op->type == "Tensor.slice" && !operand_maybe_tensor(operand))
    {
        int start = op->inputs.size() == 3 ? op->inputs[1]->producer->params.at("value").i : op->inputs[2]->producer->params.at("value").i;
        int end = op->inputs.size() == 3 ? op->inputs[2]->producer->params.at("value").i : op->inputs[3]->producer->params.at("value").i;

        // onnx style shape + slice chain
        const Operator* op_shape = op->inputs[0]->producer;
        for (int i = start; i < end; i++)
        {
            expr += "size(";
            fuse_expression(graph, op_shape->inputs[0], expr, inputs, foldable_constants, zip);
            expr += ",";
            expr += std::to_string(i);
            expr += ")";
            if (i + 1 != end)
                expr += ",";
        }
    }
    else if (op->type == "aten::Int")
    {
        expr += "int(";
        fuse_expression(graph, op->inputs[0], expr, inputs, foldable_constants, zip);
        expr += ")";
    }
    else if (op->type == "Tensor.to")
    {
        bool noop_type_cast = (op->outputs[0]->type != -1) && (op->inputs[0]->type == op->outputs[0]->type);
        if (noop_type_cast)
        {
            fuse_expression(graph, op->inputs[0], expr, inputs, foldable_constants, zip);
        }
        else if (!operand_maybe_tensor(operand))
        {
            std::string dtype = op->params.at("dtype").s;

            // torch.xxx
            expr += dtype + "(";
            fuse_expression(graph, op->inputs[0], expr, inputs, foldable_constants, zip);
            expr += ")";
        }
        else
        {
            goto DEFAULT;
        }
    }
    else if (op->type == "aten::detach" || op->type == "aten::ScalarImplicit")
    {
        fuse_expression(graph, op->inputs[0], expr, inputs, foldable_constants, zip);
    }
    else if (op->type == "aten::abs"
             || op->type == "aten::acos"
             || op->type == "aten::acosh"
             || op->type == "aten::asin"
             || op->type == "aten::asinh"
             || op->type == "aten::atan"
             || op->type == "aten::atanh"
             || op->type == "aten::ceil"
             || op->type == "aten::cos"
             || op->type == "aten::cosh"
             || op->type == "aten::erf"
             || op->type == "aten::exp"
             || op->type == "aten::floor"
             || op->type == "aten::log"
             || op->type == "aten::log10"
             || op->type == "aten::neg"
             || op->type == "aten::reciprocal"
             || op->type == "aten::round"
             || op->type == "aten::rsqrt"
             || op->type == "aten::sign"
             || op->type == "aten::sin"
             || op->type == "aten::sinh"
             || op->type == "aten::sqrt"
             || op->type == "aten::square"
             || op->type == "aten::tan"
             || op->type == "aten::tanh"
             || op->type == "aten::trunc")
    {
        std::string mathop = op->type.substr(6);

        expr += mathop;
        expr += "(";
        fuse_expression(graph, op->inputs[0], expr, inputs, foldable_constants, zip);
        expr += ")";
    }
    else if (op->type == "aten::atan2"
             || op->type == "aten::floor_divide"
             || op->type == "aten::fmod"
             || op->type == "aten::logaddexp"
             || op->type == "aten::max"
             || op->type == "aten::maximum"
             || op->type == "aten::min"
             || op->type == "aten::minimum"
             || op->type == "aten::mul"
             || op->type == "aten::pow"
             || op->type == "aten::remainder")
    {
        std::string mathop = op->type.substr(6);

        expr += mathop;
        expr += "(";
        fuse_expression(graph, op->inputs[0], expr, inputs, foldable_constants, zip);
        expr += ",";
        fuse_expression(graph, op->inputs[1], expr, inputs, foldable_constants, zip);
        expr += ")";
    }
    else if (op->type == "aten::__and__" || op->type == "aten::__or__" || op->type == "aten::__xor__" || op->type == "aten::__lshift__" || op->type == "aten::__rshift__")
    {
        std::string mathop = op->type.substr(8, op->type.size() - 10);

        expr += mathop;
        expr += "(";
        fuse_expression(graph, op->inputs[0], expr, inputs, foldable_constants, zip);
        expr += ",";
        fuse_expression(graph, op->inputs[1], expr, inputs, foldable_constants, zip);
        expr += ")";
    }
    else if (op->type == "aten::add" || op->type == "aten::sub")
    {
        std::string mathop = op->type.substr(6);

        expr += mathop;
        expr += "(";
        fuse_expression(graph, op->inputs[0], expr, inputs, foldable_constants, zip);
        expr += ",";

        std::string expr1;
        fuse_expression(graph, op->inputs[1], expr1, inputs, foldable_constants, zip);

        if (op->inputs.size() == 2)
        {
            expr += expr1;
        }
        else // if (op->inputs.size() == 3)
        {
            std::string expr2;
            fuse_expression(graph, op->inputs[2], expr2, inputs, foldable_constants, zip);

            if (expr2 == "1")
            {
                expr += expr1;
            }
            else
            {
                expr += ",";
                expr += "mul(";
                expr += expr1;
                expr += ",";
                expr += expr2;
                expr += ")";
            }
        }

        expr += ")";
    }
    else if (op->type == "aten::rsub")
    {
        expr += "sub(";
        std::string expr1;
        fuse_expression(graph, op->inputs[1], expr1, inputs, foldable_constants, zip);

        if (op->inputs.size() == 2)
        {
            expr += expr1;
        }
        else // if (op->inputs.size() == 3)
        {
            std::string expr2;
            fuse_expression(graph, op->inputs[2], expr2, inputs, foldable_constants, zip);

            if (expr2 == "1")
            {
                expr += expr1;
            }
            else
            {
                expr += ",";
                expr += "mul(";
                expr += expr1;
                expr += ",";
                expr += expr2;
                expr += ")";
            }
        }

        expr += ",";
        fuse_expression(graph, op->inputs[0], expr, inputs, foldable_constants, zip);
        expr += ")";
    }
    else if (op->type == "aten::div")
    {
        std::string rounding_mode;
        if (op->inputs.size() == 3)
            fuse_expression(graph, op->inputs[2], rounding_mode, inputs, foldable_constants, zip);

        if (rounding_mode == "trunc")
        {
            expr += "floor_divide";
        }
        else
        {
            expr += "div";
        }

        expr += "(";
        fuse_expression(graph, op->inputs[0], expr, inputs, foldable_constants, zip);
        expr += ",";
        fuse_expression(graph, op->inputs[1], expr, inputs, foldable_constants, zip);
        expr += ")";
    }
    else
    {
        goto DEFAULT;
    }

    return;

DEFAULT:
    auto it = std::find(inputs.begin(), inputs.end(), operand);
    if (it == inputs.end())
    {
        // tensor
        char tmp[32];
        sprintf(tmp, "@%d", (int)inputs.size());
        expr += tmp;

        inputs.push_back(operand);
    }
    else
    {
        // tensor
        char tmp[32];
        sprintf(tmp, "@%d", (int)(it - inputs.begin()));
        expr += tmp;
    }
}

void fuse_expression(Graph& graph, const std::set<std::string>& foldable_constants, const std::string& foldable_constants_zippath)
{
    StoreZipReader zip;
    zip.open(foldable_constants_zippath);

    int pnnx_expr_index = 0;

    for (;;)
    {
        bool need_fuse = false;

        // build expression via reverse order
        for (int i = (int)graph.ops.size() - 1; i >= 0; i--)
        {
            Operator* op = graph.ops[i];

            if (op->type == "prim::Constant")
            {
                need_fuse = true;
            }
            if (op->type == "prim::NumToTensor")
            {
                need_fuse = true;
            }
            if (op->type == "prim::ListConstruct")
            {
                need_fuse = true;
            }
            if (op->type == "aten::size")
            {
                need_fuse = true;
            }
            if (op->type == "aten::Int")
            {
                need_fuse = true;
            }
            if (op->type == "Tensor.size")
            {
                need_fuse = true;
            }
            if (op->type == "Tensor.to")
            {
                // fuse noop type cast only
                bool noop_to = (op->outputs[0]->type != -1) && (op->inputs[0]->type == op->outputs[0]->type);
                bool is_scalar = !operand_maybe_tensor(op->outputs[0]);
                need_fuse = noop_to || is_scalar;
            }
            if (op->type == "aten::detach" || op->type == "aten::ScalarImplicit")
            {
                need_fuse = true;
            }
            if (op->type == "aten::abs"
                    || op->type == "aten::acos"
                    || op->type == "aten::acosh"
                    || op->type == "aten::add"
                    || op->type == "aten::asin"
                    || op->type == "aten::asinh"
                    || op->type == "aten::atan"
                    || op->type == "aten::atanh"
                    || op->type == "aten::atan2"
                    || op->type == "aten::ceil"
                    || op->type == "aten::cos"
                    || op->type == "aten::cosh"
                    || op->type == "aten::div"
                    || op->type == "aten::erf"
                    || op->type == "aten::exp"
                    || op->type == "aten::floor"
                    || op->type == "aten::floor_divide"
                    || op->type == "aten::fmod"
                    || op->type == "aten::log"
                    || op->type == "aten::log10"
                    || op->type == "aten::logaddexp"
                    || op->type == "aten::max"
                    || op->type == "aten::maximum"
                    || op->type == "aten::min"
                    || op->type == "aten::minimum"
                    || op->type == "aten::mul"
                    || op->type == "aten::neg"
                    || op->type == "aten::pow"
                    || op->type == "aten::reciprocal"
                    || op->type == "aten::remainder"
                    || op->type == "aten::round"
                    || op->type == "aten::rsqrt"
                    || op->type == "aten::rsub"
                    || op->type == "aten::sign"
                    || op->type == "aten::sin"
                    || op->type == "aten::sinh"
                    || op->type == "aten::sqrt"
                    || op->type == "aten::square"
                    || op->type == "aten::sub"
                    || op->type == "aten::tan"
                    || op->type == "aten::tanh"
                    || op->type == "aten::trunc")
            {
                need_fuse = true;
            }
            if (op->type == "aten::__and__" || op->type == "aten::__or__" || op->type == "aten::__xor__" || op->type == "aten::__lshift__" || op->type == "aten::__rshift__")
            {
                need_fuse = true;
            }

            if (need_fuse)
            {
                std::string expr;
                std::vector<Operand*> inputs;
                fuse_expression(graph, op->outputs[0], expr, inputs, foldable_constants, zip, false);
                //                 fprintf(stderr, "expr = %s\n", expr.c_str());

                // lets rewrite graph
                char name[32];
                sprintf(name, "pnnx_expr_%d", pnnx_expr_index++);

                op->type = "pnnx.Expression";
                op->name = name;

                op->params.clear();
                op->attrs.clear();

                op->params["expr"] = expr;

                // fix input output
                for (Operand* operand : op->inputs)
                {
                    operand->consumers.erase(std::find(operand->consumers.begin(), operand->consumers.end(), op));
                }

                op->inputs = inputs;

                for (Operand* operand : op->inputs)
                {
                    operand->consumers.push_back(op);
                }

                break;
            }
        }

        if (!need_fuse)
            break;
    }

    zip.close();
}

} // namespace pnnx
