/*
 * Copyright (c) 2020-2022, Andreas Kling <andreas@ladybird.org>
 * Copyright (c) 2022-2024, Sam Atkins <sam@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Transformation.h"
#include <LibWeb/Painting/PaintableBox.h>

namespace Web::CSS {

Transformation::Transformation(TransformFunction function, Vector<NonnullRefPtr<CSSStyleValue>>&& values)
    : m_function(function)
    , m_values(move(values))
{
}

ErrorOr<Gfx::FloatMatrix4x4> Transformation::to_matrix(Optional<Painting::PaintableBox const&> paintable_box) const
{
    auto count = m_values.size();
    auto value = [&](size_t index, CSSPixels const& reference_length = 0) -> ErrorOr<float> {
        return m_values[index].visit(
            [&](CSS::LengthPercentage const& value) -> ErrorOr<float> {
                if (paintable_box.has_value())
                    return value.resolved(paintable_box->layout_node(), reference_length).to_px(paintable_box->layout_node()).to_float();
                if (value.is_length()) {
                    if (auto const& length = value.length(); length.is_absolute())
                        return length.absolute_length_to_px().to_float();
                }
                return Error::from_string_literal("Transform contains non absolute units");
            },
            [&](CSS::AngleOrCalculated const& value) -> ErrorOr<float> {
                if (paintable_box.has_value())
                    return value.resolved(paintable_box->layout_node()).to_radians();
                if (!value.is_calculated())
                    return value.value().to_radians();
                return Error::from_string_literal("Transform contains non absolute units");
            },
            [&](CSS::NumberPercentage const& value) -> ErrorOr<float> {
                if (value.is_percentage())
                    return value.percentage().as_fraction();
                return value.number().value();
            });
    };

    CSSPixels width = 1;
    CSSPixels height = 1;
    if (paintable_box.has_value()) {
        auto reference_box = paintable_box->transform_box_rect();
        width = reference_box.width();
        height = reference_box.height();
    }

    switch (m_function) {
    case TransformFunction::Perspective:
        // https://drafts.csswg.org/css-transforms-2/#perspective
        // Count is zero when null parameter
        if (count == 1) {
            // FIXME: Add support for the 'perspective-origin' CSS property.
            auto distance = TRY(get_value(0));
            return Gfx::FloatMatrix4x4(1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                0, 0, -1 / (distance <= 0 ? 1 : distance), 1);
        } else {
            return Gfx::FloatMatrix4x4(1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1);
        }
        break;
    case TransformFunction::Matrix:
        if (count == 6)
            return Gfx::FloatMatrix4x4(TRY(get_value(0)), TRY(get_value(2)), 0, TRY(get_value(4)),
                TRY(get_value(1)), TRY(get_value(3)), 0, TRY(get_value(5)),
                0, 0, 1, 0,
                0, 0, 0, 1);
        break;
    case TransformFunction::Matrix3d:
        if (count == 16)
            return Gfx::FloatMatrix4x4(TRY(get_value(0)), TRY(get_value(4)), TRY(get_value(8)), TRY(get_value(12)),
                TRY(get_value(1)), TRY(get_value(5)), TRY(get_value(9)), TRY(get_value(13)),
                TRY(get_value(2)), TRY(get_value(6)), TRY(get_value(10)), TRY(get_value(14)),
                TRY(get_value(3)), TRY(get_value(7)), TRY(get_value(11)), TRY(get_value(15)));
        break;
    case TransformFunction::Translate:
        if (count == 1)
            return Gfx::FloatMatrix4x4(1, 0, 0, TRY(get_value(0, width)),
                0, 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1);
        if (count == 2)
            return Gfx::FloatMatrix4x4(1, 0, 0, TRY(get_value(0, width)),
                0, 1, 0, TRY(get_value(1, height)),
                0, 0, 1, 0,
                0, 0, 0, 1);
        break;
    case TransformFunction::Translate3d:
        return Gfx::FloatMatrix4x4(1, 0, 0, TRY(get_value(0, width)),
            0, 1, 0, TRY(get_value(1, height)),
            0, 0, 1, TRY(get_value(2)),
            0, 0, 0, 1);
        break;
    case TransformFunction::TranslateX:
        if (count == 1)
            return Gfx::FloatMatrix4x4(1, 0, 0, TRY(get_value(0, width)),
                0, 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1);
        break;
    case TransformFunction::TranslateY:
        if (count == 1)
            return Gfx::FloatMatrix4x4(1, 0, 0, 0,
                0, 1, 0, TRY(get_value(0, height)),
                0, 0, 1, 0,
                0, 0, 0, 1);
        break;
    case TransformFunction::TranslateZ:
        if (count == 1)
            return Gfx::FloatMatrix4x4(1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, TRY(get_value(0)),
                0, 0, 0, 1);
        break;
    case TransformFunction::Scale:
        if (count == 1)
            return Gfx::FloatMatrix4x4(TRY(get_value(0)), 0, 0, 0,
                0, TRY(get_value(0)), 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1);
        if (count == 2)
            return Gfx::FloatMatrix4x4(TRY(get_value(0)), 0, 0, 0,
                0, TRY(get_value(1)), 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1);
        break;
    case TransformFunction::Scale3d:
        if (count == 3)
            return Gfx::FloatMatrix4x4(TRY(get_value(0)), 0, 0, 0,
                0, TRY(get_value(1)), 0, 0,
                0, 0, TRY(get_value(2)), 0,
                0, 0, 0, 1);
        break;
    case TransformFunction::ScaleX:
        if (count == 1)
            return Gfx::FloatMatrix4x4(TRY(get_value(0)), 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1);
        break;
    case TransformFunction::ScaleY:
        if (count == 1)
            return Gfx::FloatMatrix4x4(1, 0, 0, 0,
                0, TRY(get_value(0)), 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1);
        break;
    case TransformFunction::ScaleZ:
        if (count == 1)
            return Gfx::FloatMatrix4x4(1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, TRY(get_value(0)), 0,
                0, 0, 0, 1);
        break;
    case TransformFunction::Rotate3d:
        if (count == 4)
            return Gfx::rotation_matrix({ TRY(get_value(0)), TRY(get_value(1)), TRY(get_value(2)) }, TRY(get_value(3)));
        break;
    case TransformFunction::RotateX:
        if (count == 1)
            return Gfx::rotation_matrix({ 1.0f, 0.0f, 0.0f }, TRY(get_value(0)));
        break;
    case TransformFunction::RotateY:
        if (count == 1)
            return Gfx::rotation_matrix({ 0.0f, 1.0f, 0.0f }, TRY(get_value(0)));
        break;
    case TransformFunction::Rotate:
    case TransformFunction::RotateZ:
        if (count == 1)
            return Gfx::rotation_matrix({ 0.0f, 0.0f, 1.0f }, TRY(get_value(0)));
        break;
    case TransformFunction::Skew:
        if (count == 1)
            return Gfx::FloatMatrix4x4(1, tanf(TRY(get_value(0))), 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1);
        if (count == 2)
            return Gfx::FloatMatrix4x4(1, tanf(TRY(get_value(0))), 0, 0,
                tanf(TRY(get_value(1))), 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1);
        break;
    case TransformFunction::SkewX:
        if (count == 1)
            return Gfx::FloatMatrix4x4(1, tanf(TRY(get_value(0))), 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1);
        break;
    case TransformFunction::SkewY:
        if (count == 1)
            return Gfx::FloatMatrix4x4(1, 0, 0, 0,
                tanf(TRY(get_value(0))), 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1);
        break;
    }
    dbgln_if(LIBWEB_CSS_DEBUG, "FIXME: Unhandled transformation function {} with {} arguments", to_string(m_function), m_values.size());
    return Gfx::FloatMatrix4x4::identity();
}

}
